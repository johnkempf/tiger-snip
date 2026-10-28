// Render capture guidance and the native cursor in memory; no live app or clipboard.
#include "../src/main.cpp"
#include <cstring>
#include <iostream>

int wmain()
{
    auto require = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };
    HDC dc = nullptr;
    HBITMAP surface = nullptr;
    HFONT oversized = nullptr;
    HGDIOBJ previousBitmap = nullptr, previousFont = nullptr;
    bool com = false;
    int result = 0;
    try
    {
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Cannot initialize test COM.");
        com = true;
        app.graphics.initialize();
        const auto pixels = captureCursorPixels();
        const auto cursor = currentCaptureCursor();
        ICONINFO icon{};
        require(GetIconInfo(cursor, &icon), "Cannot inspect capture cursor.");
        const bool precise = !icon.fIcon && icon.xHotspot == 20 && icon.yHotspot == 20;
        DeleteObject(icon.hbmColor);
        DeleteObject(icon.hbmMask);
        require(precise, "Capture cursor hotspot is offset from the target pixel.");
        auto alpha = [&](int x, int y) { return pixels.pixels[(y * pixels.width + x) * 4 + 3]; };
        for (int y = 19; y <= 21; ++y)
            for (int x = 19; x <= 21; ++x)
                require(!alpha(x, y), "Capture cursor obscures the target pixel.");
        auto thickness = [&](int distance) {
            int count = 0;
            for (int y = 0; y < pixels.height; ++y) count += alpha(20 + distance, y) != 0;
            return count;
        };
        require(thickness(3) == 1 && thickness(9) == 3 && thickness(15) == 5,
                "Capture cursor does not taper to a one-pixel tip.");
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
                require(alpha(x, y) == alpha(y, x) && alpha(x, y) == alpha(40 - x, 40 - y),
                        "Capture cursor arms are not aligned symmetrically.");
        for (uint8_t background : {uint8_t{0}, uint8_t{255}})
        {
            auto source = Bitmap::create(41, 41);
            for (size_t i = 0; i < source.pixels.size(); i += 4)
            {
                for (int channel = 0; channel < 3; ++channel) source.pixels[i + channel] = background;
                source.pixels[i + 3] = 255;
            }
            const auto drawn = compositeCursor(source, cursor, 0, 0);
            require(drawn.pixels != source.pixels, "Crosshair disappears on a light/dark image.");
            for (int y = 19; y <= 21; ++y)
                for (int x = 19; x <= 21; ++x)
                {
                    const size_t offset = (y * 41 + x) * 4;
                    require(std::memcmp(drawn.pixels.data() + offset, source.pixels.data() + offset, 3) == 0,
                            "Native cursor composition covers the target image edge.");
                }
        }
        constexpr int width = 1024, height = 800;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *bits = nullptr;
        dc = CreateCompatibleDC(nullptr);
        surface = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        require(dc && surface && bits, "Cannot create capture-label test surface.");
        previousBitmap = SelectObject(dc, surface);
        std::memset(bits, 48, width * height * 4);
        oversized = CreateFontW(-120, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
        previousFont = SelectObject(dc, oversized);
        const std::array<std::wstring, 4> labels{
            L"Drag to select an area  |  Esc to cancel", L"Drag to select text  |  Esc to cancel",
            L"12345 x 6789   |   Esc to cancel", L"Copy text: 12345 x 6789 | Esc to cancel"};
        const RECT bounds{8, 8, width - 8, height - 8};
        for (UINT dpi : {96U, 144U, 192U})
            for (size_t row = 0; row < labels.size(); ++row)
            {
                const LONG y = 24 + static_cast<LONG>((dpi - 96) / 48) * 250 + static_cast<LONG>(row) * 58;
                const auto box = drawCaptureLabel(dc, labels[row], {80, y}, bounds, dpi);
                require(box.bottom - box.top <= MulDiv(32, dpi, 96),
                        "Capture guidance inherited an oversized font.");
                require(GetCurrentObject(dc, OBJ_FONT) == oversized,
                        "Capture guidance leaks its font into subsequent drawing.");
                HFONT expected = CreateFontW(-MulDiv(13, dpi, 96), 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
                SelectObject(dc, expected);
                SIZE textSize{};
                GetTextExtentPoint32W(dc, labels[row].c_str(), static_cast<int>(labels[row].size()), &textSize);
                SelectObject(dc, oversized);
                DeleteObject(expected);
                require(box.right - box.left >= textSize.cx + MulDiv(20, dpi, 96) &&
                        box.bottom - box.top >= textSize.cy + MulDiv(12, dpi, 96),
                        "Capture guidance clips its full text at a supported display scale.");
                const int saved = SaveDC(dc);
                IntersectClipRect(dc, 0, 0, 0, 0); // Edge checks must not overwrite the preview rows.
                for (POINT corner : {POINT{-500, -500}, POINT{width + 500, height + 500}})
                {
                    const auto edge = drawCaptureLabel(dc, labels[row], corner, bounds, dpi);
                    require(edge.left >= bounds.left && edge.top >= bounds.top &&
                            edge.right <= bounds.right && edge.bottom <= bounds.bottom,
                            "Capture guidance extends beyond the monitor edge.");
                }
                RestoreDC(dc, saved);
            }
        GdiFlush();
        auto preview = Bitmap::create(width, height);
        std::memcpy(preview.pixels.data(), bits, preview.pixels.size());
        for (size_t i = 3; i < preview.pixels.size(); i += 4) preview.pixels[i] = 255;
        preview = compositeCursor(preview, cursor, 30, 30);
        saveBytes(L"capture-overlay-preview.png", app.graphics.png(preview));
        saveBytes(L"capture-cursor.png", app.graphics.png(pixels));
        std::cout << "PASS: image/text guidance fits at 100/150/200% DPI despite an oversized inherited font; "
                     "monitor-edge clamping; font restoration; native cursor hotspot, taper, clear target and light/dark contrast.\n";
    }
    catch (const std::exception &failure) { std::cerr << "FAIL: " << failure.what() << '\n'; result = 1; }
    if (dc && previousFont) SelectObject(dc, previousFont);
    if (oversized) DeleteObject(oversized);
    if (dc && previousBitmap) SelectObject(dc, previousBitmap);
    if (surface) DeleteObject(surface);
    if (dc) DeleteDC(dc);
    if (app.captureCursor) DestroyCursor(std::exchange(app.captureCursor, nullptr));
    if (com) CoUninitialize();
    return result;
}
