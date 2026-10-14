// Real clipboard and editor pointer handlers on a private desktop; user clipboard is untouched.
#include "../src/main.cpp"
#include <iostream>
#include <cstring>

bool cropMenuVerified = false;
bool menuHelpVerified = false;
void CALLBACK verifyMenuHelp(HWND hwnd, UINT, UINT_PTR id, DWORD)
{
    KillTimer(hwnd, id);
    buildButtons();
    menuHelpVerified = app.menuDepth > 0 && app.tooltipSuppressed && !IsWindowVisible(app.tooltip);
    EndMenu();
}
void CALLBACK verifyCropMenu(HWND hwnd, UINT, UINT_PTR id, DWORD)
{
    KillTimer(hwnd, id);
    cropMenuVerified = app.shapeMenu && GetMenuItemCount(app.shapeMenu) == 2 &&
                       GetMenuItemID(app.shapeMenu, 0) == CropImageChoice &&
                       GetMenuItemID(app.shapeMenu, 1) == SelectPixelsChoice;
    for (int i = 0; cropMenuVerified && i < 2; ++i)
    {
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STATE;
        RECT r{};
        cropMenuVerified = GetMenuItemInfoW(app.shapeMenu, i, TRUE, &item) &&
                           (item.fType & MFT_OWNERDRAW) && item.dwItemData &&
                           ((item.fState & MFS_CHECKED) != 0) == (i == (app.cropSelectPreferred ? 1 : 0)) &&
                           GetMenuItemRect(hwnd, app.shapeMenu, i, &r) &&
                           r.bottom - r.top == static_cast<LONG>(48 * app.dpi);
    }
    buildButtons(); // A repaint during a native menu loop must not reactivate hover help.
    cropMenuVerified = cropMenuVerified && app.menuDepth > 0 && app.tooltipSuppressed &&
                       !IsWindowVisible(app.tooltip);
    EndMenu();
}

int wmain()
{
    const auto originalStation = GetProcessWindowStation();
    const auto originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    HWINSTA station = nullptr;
    HDESK desktop = nullptr;
    bool com = false;
    int result = 0;
    try
    {
        auto require = [](bool ok, const char *message) {
            if (!ok) throw std::runtime_error(message);
        };
        station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        require(station && SetProcessWindowStation(station), "Cannot isolate image editing station.");
        desktop = CreateDesktopW(L"TigerSnipImageTest", nullptr, nullptr, 0,
                                 DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS |
                                     DESKTOP_WRITEOBJECTS, nullptr);
        require(desktop && SetThreadDesktop(desktop), "Cannot isolate image editing desktop.");
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Cannot initialize COM.");
        com = true;
        app.instance = GetModuleHandleW(nullptr);
        app.smoke = app.softwareRendering = true;
        app.iniPath = (std::filesystem::current_path() / L"image-settings.ini").wstring();
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        registerClasses();
        require(CreateWindowExW(0, MainClass, L"Image editing test", WS_OVERLAPPEDWINDOW, 0, 0,
                                1280, 900, nullptr, createMenu(), app.instance, nullptr),
                "Cannot create image editor test window.");
        app.windowedMenu = GetMenu(app.window);
        ShowWindow(app.window, SW_SHOWNOACTIVATE);
        auto original = Bitmap::create(640, 420);
        for (int y = 0; y < original.height; ++y)
            for (int x = 0; x < original.width; ++x)
            {
                const size_t i = (static_cast<size_t>(y) * original.width + x) * 4;
                original.pixels[i] = static_cast<uint8_t>(40 + x % 120);
                original.pixels[i + 1] = static_cast<uint8_t>(60 + y % 140);
                original.pixels[i + 2] = 120;
                original.pixels[i + 3] = 255;
            }
        auto pointer = [](Point p) {
            p = app.view.toScreen(p) * app.dpi;
            return MAKELPARAM(static_cast<int>(std::round(p.x)), static_cast<int>(std::round(p.y)));
        };
        auto drag = [&](Point from, Point to, bool cancel = false) {
            mouseDown(pointer(from));
            mouseMove(pointer(to));
            if (cancel) processKey(VK_ESCAPE);
            else mouseUp(pointer(to));
            require(app.drag == Drag::None && !app.document.editing() && GetCapture() != app.window,
                    "Image gesture leaked capture or a transaction.");
        };
        auto publish = [&](UINT format, const std::vector<uint8_t> &bytes) {
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
            require(memory != nullptr, "Cannot allocate clipboard fixture.");
            auto data = GlobalLock(memory);
            require(data != nullptr, "Cannot lock clipboard fixture.");
            std::memcpy(data, bytes.data(), bytes.size());
            GlobalUnlock(memory);
            require(OpenClipboard(app.window), "Cannot open private clipboard.");
            const bool success = EmptyClipboard() && SetClipboardData(format, memory);
            CloseClipboard();
            if (!success) GlobalFree(memory);
            require(success, "Cannot publish private clipboard fixture.");
        };
        for (bool classic : {false, true})
            for (float dpi : {1.0f, 1.5f, 2.0f})
            {
                app.classicUI = classic;
                app.dpi = dpi;
                app.collapsedRows = 0;
                SetWindowPos(app.window, nullptr, 0, 0, static_cast<int>(1280 * dpi),
                             static_cast<int>(900 * dpi), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                app.document.clear();
                app.image = original;
                app.cropSource = {};
                app.appliedCrop.reset();
                app.exportOptions = {};
                app.fit = true;
                resetPreview();
                Annotation inside;
                inside.kind = Tool::Arrow;
                inside.a = {100, 90};
                inside.b = {160, 125};
                inside.thickness = 4;
                Annotation outside = inside;
                outside.a = {500, 300};
                outside.b = {580, 340};
                app.document.items = {inside, outside};
                updateView();
                buildButtons();
                require(std::any_of(app.buttons.begin(), app.buttons.end(),
                                    [](const Button &b) { return b.command == CropModeMenu; }),
                        "Crop dropdown is missing in an editor layout.");
                cropMenuVerified = false;
                require(SetTimer(app.window, 2000, 100, verifyCropMenu), "Cannot inspect crop dropdown.");
                const auto cropButton = std::find_if(app.buttons.begin(), app.buttons.end(),
                                                     [](const Button &b) { return b.command == CropModeMenu; });
                const auto menuPoint = MAKELPARAM(
                    static_cast<int>((cropButton->rect.left + cropButton->rect.right) / 2 * dpi),
                    static_cast<int>((cropButton->rect.top + cropButton->rect.bottom) / 2 * dpi));
                mouseDown(menuPoint);
                mouseUp(menuPoint);
                require(cropMenuVerified, "Crop dropdown does not offer Crop and Select area.");
                const auto before = app.graphics.flatten(app.image, app.document.items);
                command(SelectPixelsChoice);
                drag({80, 60}, {200, 160}, true);
                require(app.document.items == std::vector<Annotation>{inside, outside} &&
                            app.image.pixels == original.pixels,
                        "Canceling a rectangular selection changed the screenshot.");
                command(SelectPixelsChoice);
                drag({80, 60}, {200, 160});
                require(imageSelected() && app.document.items.size() == 4 &&
                            app.document.items[2].whiteout &&
                            app.graphics.flatten(app.image, app.document.items).pixels == before.pixels,
                        "Lifting an area lost pixels or visible annotations.");
                const auto selectedPiece = app.document.items.back();
                const auto r = selectedPiece.bounds();
                const Point center{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
                drag(center, center + Point{220, 100});
                const auto moved = app.document.items.back();
                require(length(moved.a - (selectedPiece.a + Point{220, 100})) < 2 &&
                            moved.raster == selectedPiece.raster,
                        "Moving a selected area changed its immutable pixels.");
                const auto movedImage = app.graphics.flatten(app.image, app.document.items);
                require(movedImage.sample({130, 110}) == rgb(255, 255, 255) &&
                            app.document.hit({130, 110}, 0) == -1 &&
                            !app.document.items[0].cutouts.empty() && app.document.items[1] == outside,
                        "Moving did not leave white behind or changed objects outside the area.");
                require(app.document.undo() &&
                            app.graphics.flatten(app.image, app.document.items).pixels == before.pixels &&
                            app.document.undo() && app.document.items.size() == 2,
                        "Lift and move did not undo independently.");
                require(app.document.redo() && app.document.redo(), "Image edit redo failed.");
                app.document.selected = 3;
                const auto resizeBefore = app.document.items.back();
                drag(resizeBefore.b, resizeBefore.b + Point{40, 40});
                require(app.document.items.back().bounds().width() > resizeBefore.bounds().width() + 35 &&
                            app.document.items.back().raster == resizeBefore.raster,
                        "Corner resizing resampled or lost the original image.");
                require(app.document.undo(), "Image resize undo failed.");
                app.document.selected = 3;
                const auto sideBefore = app.document.items.back();
                const Point side{sideBefore.b.x, (sideBefore.a.y + sideBefore.b.y) / 2};
                drag(side, side + Point{30, 0});
                require(std::abs(app.document.items.back().bounds().height() - sideBefore.bounds().height()) < 2,
                        "Side handle stretched the other image dimension.");
                require(app.document.undo(), "Side resize undo failed.");
                app.document.selected = 3;
                drag(center + Point{220, 100}, center + Point{250, 140}, true);
                require(app.document.items.back() == sideBefore, "Canceled image move changed placement.");
                const auto exported = renderedExport();
                require(app.graphics.decode(app.graphics.png(exported)).pixels == exported.pixels,
                        "Selected image export lost pixels.");
                applyCrop({250, 140, 450, 290});
                require(app.image.width == 200 && app.image.height == 150,
                        "Cropping with image pieces failed.");
                command(Undo);
                require(app.image.pixels == original.pixels &&
                            renderedExport().pixels == exported.pixels,
                        "Undo crop did not restore image pieces and source holes.");
                app.document.selected = 3;
                saveBytes(std::wstring(classic ? L"image-classic-" : L"image-side-") +
                              std::to_wstring(static_cast<int>(dpi * 100)) + L".png",
                          app.graphics.png(renderEditorPreview()));
            }
        app.dpi = 1;
        app.classicUI = false;
        for (int menu : {CircleStyleMenu, ArrowStyleMenu, CheckStyleMenu, LineStyleMenu,
                         TextSizeMenu, CaptureMenu})
        {
            menuHelpVerified = false;
            require(SetTimer(app.window, 2001, 100, verifyMenuHelp), "Cannot inspect dropdown help.");
            command(menu);
            require(menuHelpVerified && app.menuDepth == 0 && app.tooltipSuppressed,
                    "A clicked dropdown reactivated its explanation tooltip.");
        }
        app.image = original;
        app.document.clear();
        app.cropSource = {};
        app.appliedCrop.reset();
        resetPreview();
        updateView();
        {
            Annotation crossing;
            crossing.kind = Tool::Line;
            crossing.color = rgb(0, 80, 255);
            crossing.thickness = 4;
            crossing.a = {80, 100};
            crossing.b = {220, 100};
            app.document.items = {crossing};
            liftSelection({120, 90, 160, 110});
            app.document.begin();
            app.document.items[0].move({0, 100});
            app.document.items.back().move({240, 120});
            app.document.commit();
            const auto fragments = renderedExport();
            require(fragments.sample({100, 200}) == crossing.color &&
                        fragments.sample({140, 200}) == original.sample({140, 200}) &&
                        fragments.sample({140, 100}) == rgb(255, 255, 255) &&
                        !app.document.items[0].hit({140, 200}, 0),
                    "Moving the remaining annotation restored a fragment removed by selection.");
            auto resized = app.document.items[0];
            resized.resize({80, 190, 220, 210}, {80, 290, 360, 330});
            require(!resized.hit({200, 310}, 0), "Resizing detached an annotation's removed fragment.");
            require(app.document.undo() && app.document.undo() &&
                        app.document.items == std::vector<Annotation>{crossing},
                    "Undo did not restore a partially selected annotation.");
            app.document.clear();
        }
        auto png = Bitmap::create(32, 24);
        for (int y = 4; y < 20; ++y)
            for (int x = 4; x < 28; ++x)
            {
                const size_t i = (static_cast<size_t>(y) * png.width + x) * 4;
                png.pixels[i + 2] = 255;
                png.pixels[i + 3] = x < 16 ? 255 : 128;
            }
        require(copyBitmap(app.window, png, app.graphics.png(png)), "Cannot copy transparent fixture.");
        const auto sequence = GetClipboardSequenceNumber();
        BYTE keyboard[256]{}, ctrl[256]{};
        require(GetKeyboardState(keyboard), "Cannot inspect keyboard state.");
        std::copy(std::begin(keyboard), std::end(keyboard), std::begin(ctrl));
        ctrl[VK_CONTROL] = 0x80;
        require(SetKeyboardState(ctrl), "Cannot simulate Ctrl+V.");
        processKey('V');
        SetKeyboardState(keyboard);
        require(imageSelected() && app.document.items.size() == 1 &&
                    app.document.items.back().raster->pixels == png.pixels &&
                    GetClipboardSequenceNumber() == sequence,
                "Ctrl+V did not preserve PNG transparency or modified the clipboard.");
        const auto pasted = app.document.items.back();
        auto flat = renderedExport();
        require(flat.sample(pasted.a + Point{1, 1}) == original.sample(pasted.a + Point{1, 1}) &&
                    flat.sample(pasted.a + Point{8, 8}) == rgb(255, 0, 0),
                "Pasted transparent background or opaque image pixels rendered incorrectly.");
        command(Paste);
        require(app.document.items.size() == 2 && app.document.undo() && app.document.items.size() == 1 &&
                    app.document.undo() && app.document.items.empty() && app.document.redo(),
                "Repeated image pasting did not support independent undo and redo.");
        auto shortcut = [&](WPARAM key) {
            require(SetKeyboardState(ctrl), "Cannot simulate clipboard shortcut.");
            processKey(key);
            SetKeyboardState(keyboard);
        };
        app.document.clear();
        Annotation arrow;
        arrow.kind = Tool::Arrow;
        arrow.style = 2;
        arrow.curveFlipped = true;
        arrow.thickness = 6;
        arrow.opacity = .5f;
        arrow.color = rgb(20, 180, 80);
        arrow.a = {80, 80};
        arrow.b = {220, 140};
        arrow.cutouts = {{100, 95, 120, 110}};
        app.document.items = {arrow};
        app.document.selected = 0;
        app.dirty = true;
        const auto copyView = app.view;
        const auto toolStyles = app.styles;
        shortcut('C');
        const auto editable = readClipboardAnnotation(app.window, app.graphics);
        const auto transparent = readClipboardBitmap(app.window, app.graphics);
        require(editable && *editable == arrow && transparent && transparent->width < app.image.width &&
                    transparent->height < app.image.height && transparent->pixels[3] == 0 &&
                    app.document.items == std::vector<Annotation>{arrow} && app.document.selected == 0 &&
                    app.dirty && app.view.origin == copyView.origin && app.view.scale == copyView.scale,
                "Ctrl+C did not copy only the annotation or changed the document.");
        // The clipboard contains a snapshot, independent of subsequent source edits.
        app.document.items[0].color = rgb(200, 0, 0);
        shortcut('V');
        auto expected = arrow;
        expected.move({24, 24});
        require(app.document.items.size() == 2 && app.document.items.back() == expected &&
                    app.document.selected == 1 && app.styles == toolStyles,
                "Ctrl+V lost editable arrow geometry, style, cutouts, opacity or color.");
        shortcut('V');
        expected = arrow;
        expected.move({48, 48});
        require(app.document.items.back() == expected && app.document.undo() &&
                    app.document.items.size() == 2 && app.document.redo() && app.document.items.size() == 3,
                "Repeated annotation paste or its undo/redo failed.");
        app.document.selected = 2;
        const auto endpoint = app.document.items.back().b;
        drag(endpoint, endpoint + Point{40, 30});
        require(app.document.items.back().kind == Tool::Arrow &&
                    length(app.document.items.back().b - endpoint - Point{40, 30}) < 2 &&
                    app.document.undo(), "Pasted arrow was not editable through its endpoint handles.");
        app.document.selected = 1;
        const auto whole = renderedExport();
        command(Copy); // Toolbar/context Copy always publishes the whole image, even with a selection.
        const auto wholeClipboard = readClipboardBitmap(app.window, app.graphics);
        require(wholeClipboard && wholeClipboard->pixels == whole.pixels &&
                    !readClipboardAnnotation(app.window, app.graphics),
                "Toolbar Copy retained the annotation payload or omitted screenshot pixels.");
        shortcut('V');
        require(imageSelected() && app.document.items.back().raster->pixels == whole.pixels,
                "Whole-image clipboard data incorrectly pasted a stale annotation.");
        command(SelectTool);
        shortcut('C');
        const auto noSelection = readClipboardBitmap(app.window, app.graphics);
        require(noSelection && noSelection->pixels == renderedExport().pixels &&
                    !readClipboardAnnotation(app.window, app.graphics),
                "Ctrl+C with no selection did not copy the entire screenshot.");
        Annotation text;
        text.kind = Tool::Text;
        text.text = L"Dimension: \u00b10.05\nKeep editable";
        text.fontSize = 30;
        text.textAlignment = TextAlignment::Right;
        text.bold = text.boxed = text.textFrame = true;
        text.textWidth = 240;
        text.textHeight = 70;
        text.a = {60, 190};
        app.graphics.measureText(text);
        app.document.clear();
        app.document.items = {text};
        app.document.selected = 0;
        shortcut('C');
        shortcut('V');
        expected = text;
        expected.move({24, 24});
        require(app.document.items.back() == expected,
                "Copied text lost its Unicode content, font, bold, box or text frame.");
        for (Tool kind : {Tool::Pen, Tool::Highlight, Tool::Circle, Tool::Rectangle, Tool::Check, Tool::Line})
        {
            Annotation item;
            item.kind = kind;
            item.a = {40, 40};
            item.b = {180, 120};
            item.points = {{40, 40}, {100, 80}, {180, 120}};
            item.style = kind == Tool::Line ? 3 : kind == Tool::Rectangle ? 4 : 0;
            require(copyAnnotation(app.window, item, app.graphics), "Cannot copy annotation type.");
            const auto restored = readClipboardAnnotation(app.window, app.graphics);
            require(restored && *restored == item, "Annotation type did not round-trip as editable data.");
        }
        app.document.clear();
        app.image = original;
        app.exportOptions = {};
        Annotation glass;
        glass.kind = Tool::Rectangle;
        glass.style = 4;
        glass.a = {60, 40}; glass.b = {260, 180};
        app.document.items = {glass};
        app.document.selected = 0;
        shortcut('C');
        const auto glassPreview = readClipboardBitmap(app.window, app.graphics);
        const auto glassMetadata = readClipboardAnnotation(app.window, app.graphics);
        require(glassPreview && glassPreview->pixels == app.graphics.annotationImage(glass, &original).pixels &&
                    glassPreview->pixels[3] == 0 && glassMetadata && *glassMetadata == glass,
                "Selected glass copy lost the blurred backdrop, rounded alpha, or editable effect.");
        shortcut('V');
        require(app.document.items.size() == 2 && app.document.items.back().redaction() &&
                    !app.document.items.back().raster && app.document.items.back().a == Point{84, 64},
                "Pasted glass was flattened instead of remaining an editable backdrop effect.");
        // Version 1 annotations remain readable; unsupported alignment flags are rejected.
        Annotation legacy = text;
        legacy.textAlignment = TextAlignment::Left;
        require(copyAnnotation(app.window, legacy, app.graphics), "Cannot prepare legacy annotation.");
        require(OpenClipboard(app.window), "Cannot inspect annotation clipboard.");
        const auto legacyMemory = GetClipboardData(RegisterClipboardFormatW(AnnotationClipboardFormat));
        const auto legacySize = GlobalSize(legacyMemory);
        const auto *legacyData = static_cast<const uint8_t *>(GlobalLock(legacyMemory));
        require(legacyData && legacySize >= 28, "Missing editable clipboard payload.");
        std::vector<uint8_t> legacyBytes(legacyData, legacyData + legacySize);
        GlobalUnlock(legacyMemory);
        CloseClipboard();
        const uint32_t version1 = 1;
        std::memcpy(legacyBytes.data() + 4, &version1, sizeof(version1));
        publish(RegisterClipboardFormatW(AnnotationClipboardFormat), legacyBytes);
        const auto oldAnnotation = readClipboardAnnotation(app.window, app.graphics);
        require(oldAnnotation && *oldAnnotation == legacy, "Legacy clipboard annotations no longer paste.");
        const uint32_t version2 = 2, invalidAlignment = 3U << 5;
        std::memcpy(legacyBytes.data() + 4, &version2, sizeof(version2));
        std::memcpy(legacyBytes.data() + 24, &invalidAlignment, sizeof(invalidAlignment));
        publish(RegisterClipboardFormatW(AnnotationClipboardFormat), legacyBytes);
        require(!readClipboardAnnotation(app.window, app.graphics), "Invalid text alignment was accepted.");
        const auto raster = rasterPiece(png, {260, 200, 292, 224});
        app.document.clear();
        app.document.items = {raster};
        app.document.selected = 0;
        shortcut('C');
        shortcut('V');
        require(imageSelected() && app.document.items.size() == 2 &&
                    app.document.items.back().raster->pixels == png.pixels &&
                    !app.document.items.back().whiteout && app.document.items.back().a == Point{284, 224},
                "Copying an image piece lost transparency, created a white hole, or lost placement.");
        publish(RegisterClipboardFormatW(AnnotationClipboardFormat), {1, 2, 3, 4});
        const auto beforeInvalid = app.document.items;
        command(Paste);
        require(!readClipboardAnnotation(app.window, app.graphics) && app.document.items == beforeInvalid,
                "Malformed annotation clipboard data modified the document.");
        require(copyText(app.window, L"ordinary text"), "Cannot prepare text-only clipboard.");
        const auto unchanged = app.document.items;
        command(Paste);
        require(app.document.items == unchanged && !app.document.editing(),
                "A text-only clipboard created an image or changed the document.");
        // DIBV5 alone must retain alpha; ordinary bottom-up 24-bit DIBs must reverse rows.
        BITMAPV5HEADER v5{};
        v5.bV5Size = sizeof(v5);
        v5.bV5Width = png.width;
        v5.bV5Height = -png.height;
        v5.bV5Planes = 1;
        v5.bV5BitCount = 32;
        v5.bV5Compression = BI_BITFIELDS;
        v5.bV5RedMask = 0x00ff0000;
        v5.bV5GreenMask = 0x0000ff00;
        v5.bV5BlueMask = 0x000000ff;
        v5.bV5AlphaMask = 0xff000000;
        std::vector<uint8_t> bytes(sizeof(v5) + png.pixels.size());
        std::memcpy(bytes.data(), &v5, sizeof(v5));
        std::copy(png.pixels.begin(), png.pixels.end(), bytes.begin() + sizeof(v5));
        publish(CF_DIBV5, bytes);
        const auto alphaDib = readClipboardBitmap(app.window, app.graphics);
        require(alphaDib && alphaDib->pixels == png.pixels,
                "DIBV5-only paste lost alpha.");
        BITMAPINFOHEADER dib{};
        dib.biSize = sizeof(dib);
        dib.biWidth = 2;
        dib.biHeight = 2;
        dib.biPlanes = 1;
        dib.biBitCount = 24;
        bytes.assign(sizeof(dib) + 16, 0);
        std::memcpy(bytes.data(), &dib, sizeof(dib));
        bytes[sizeof(dib)] = 255; // bottom-left blue
        bytes[sizeof(dib) + 8 + 2] = 255; // top-left red
        publish(CF_DIB, bytes);
        const auto bottomUp = readClipboardBitmap(app.window, app.graphics);
        require(bottomUp && bottomUp->sample({0, 0}) == rgb(255, 0, 0) &&
                    bottomUp->sample({0, 1}) == rgb(0, 0, 255) && bottomUp->pixels[3] == 255,
                "Bottom-up 24-bit DIB orientation or opacity failed.");
        bytes.resize(sizeof(dib) + 1);
        publish(CF_DIB, bytes);
        require(!readClipboardBitmap(app.window, app.graphics), "Truncated DIB was accepted.");
        const auto imageFile = (std::filesystem::current_path() / L"clipboard-arrow.png").wstring();
        saveBytes(imageFile, app.graphics.png(png));
        DROPFILES drop{};
        drop.pFiles = sizeof(drop);
        drop.fWide = TRUE;
        bytes.assign(sizeof(drop) + (imageFile.size() + 2) * sizeof(wchar_t), 0);
        std::memcpy(bytes.data(), &drop, sizeof(drop));
        std::memcpy(bytes.data() + sizeof(drop), imageFile.data(), imageFile.size() * sizeof(wchar_t));
        publish(CF_HDROP, bytes);
        const auto filePaste = readClipboardBitmap(app.window, app.graphics);
        require(filePaste && filePaste->pixels == png.pixels, "Explorer image-file paste failed.");
        saveBytes(L"pasted-transparent-image.png", app.graphics.png(flat));
        command(Exit);
        std::cout << "PASS: both Crop dropdowns; rectangular lifting including annotations, white source, "
                     "move/corner/side resize/cancel/undo/redo/crop/export at 100/150/200% DPI in both layouts; "
                     "Ctrl+C selected editable annotations / whole image with no selection, toolbar whole-image copy, "
                     "transparent previews, editable paste/offset/undo/redo, all annotation types and Unicode text; "
                     "Ctrl+V PNG alpha, repeated paste, text-only no-op, DIBV5 alpha, bottom-up 24-bit DIB, "
                     "truncated DIB rejection and Explorer file paste. User clipboard untouched.\n";
    }
    catch (const std::exception &failure)
    {
        std::cerr << "FAIL: " << failure.what() << "\n";
        result = 1;
    }
    if (app.window && IsWindow(app.window)) command(Exit);
    if (com) CoUninitialize();
    if (originalDesktop) SetThreadDesktop(originalDesktop);
    if (desktop) CloseDesktop(desktop);
    if (originalStation) SetProcessWindowStation(originalStation);
    if (station) CloseWindowStation(station);
    return result;
}
