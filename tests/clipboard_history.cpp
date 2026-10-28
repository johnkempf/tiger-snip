// Use the real Windows listener and clipboard on a private window station.
#include "../src/main.cpp"
#include <iostream>
#include <cstring>

snip::Bitmap renderHistoryPopup(HWND window)
{
    RECT bounds{};
    GetClientRect(window, &bounds);
    auto result = snip::Bitmap::create(bounds.right, bounds.bottom);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = result.width;
    info.bmiHeader.biHeight = -result.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    HDC dc = CreateCompatibleDC(nullptr);
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !bitmap || !pixels) throw std::runtime_error("Cannot render history popup.");
    const auto previous = SelectObject(dc, bitmap);
    SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    GdiFlush();
    std::memcpy(result.pixels.data(), pixels, result.pixels.size());
    for (size_t i = 3; i < result.pixels.size(); i += 4) result.pixels[i] = 255;
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return result;
}

// Windows requires an interactive station for global keybinds. Keep these on a
// hidden STATIC owner, without attaching a clipboard listener to the user's desktop.
void checkHistoryKeybinds()
{
    auto require = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };
    const WORD areaKey = MAKEWORD(VK_F20, HOTKEYF_CONTROL | HOTKEYF_ALT),
        allKey = MAKEWORD(VK_F21, HOTKEYF_CONTROL | HOTKEYF_ALT),
        textKey = MAKEWORD(VK_F22, HOTKEYF_CONTROL | HOTKEYF_ALT),
        historyKey = MAKEWORD(VK_F23, HOTKEYF_CONTROL | HOTKEYF_ALT),
        blockedKey = MAKEWORD(VK_F24, HOTKEYF_CONTROL | HOTKEYF_ALT);
    require(registerShortcuts(areaKey, allKey, false, nullptr, textKey, historyKey),
        "Cannot register all four keybinds.");
    require(!registerShortcuts(areaKey, allKey, false, nullptr, textKey, areaKey) &&
        app.historyHotkey == historyKey && app.historyHotkeyRegistered,
        "Duplicate history keybind discarded the existing binding.");
    require(RegisterHotKey(app.window, 99, hotkeyModifiers(blockedKey), LOBYTE(blockedKey)),
        "Cannot occupy a test keybind.");
    const bool rejected = !registerShortcuts(areaKey, allKey, false, nullptr, textKey, blockedKey);
    UnregisterHotKey(app.window, 99);
    require(rejected && app.historyHotkey == historyKey && app.historyHotkeyRegistered &&
        app.hotkeyRegistered && app.instantHotkeyRegistered && app.textHotkeyRegistered,
        "History keybind conflict did not roll back all four registrations.");
    app.settingsPanelOpen = true;
    command(SettingsPageFirst + 2);
    require(std::wstring(SettingsPages[2]) == L"Keybinds", "Capture settings was not renamed Keybinds.");
    const auto layout = settingsPanelLayout();
    require(std::any_of(layout.controls.begin(), layout.controls.end(), [](const auto &control) {
        return control.command == SettingsHistoryKey && control.title == L"Open clipboard history";
    }), "Keybinds settings is missing the clipboard history selector.");
    command(SettingsHistoryKey);
    settingsPanelShortcut(blockedKey);
    require(!app.settingsRecording && app.historyHotkey == blockedKey &&
        preferenceUInt(app.iniPath, L"Settings", L"HistoryHotkey", 0) == blockedKey &&
        historyMenuLabel().find(hotkeyName(blockedKey)) != std::wstring::npos,
        "Edited history keybind did not register, persist or update its label.");
    command(SettingsHistoryKey);
    settingsPanelShortcut(0);
    require(!app.historyHotkey && !app.historyHotkeyRegistered &&
        preferenceUInt(app.iniPath, L"Settings", L"HistoryHotkey", 1) == 0,
        "History keybind could not be disabled and saved.");
    command(SettingsHistoryKey);
    settingsPanelShortcut(historyKey);
    closeSettingsPanel();
    require(registerShortcuts(0, 0, false, nullptr, 0, 0), "Cannot release test keybinds.");
}

int wmain()
{
    const auto originalStation = GetProcessWindowStation();
    const auto originalDesktop = GetThreadDesktop(GetCurrentThreadId());
    HWINSTA station = nullptr;
    HDESK desktop = nullptr;
    HWND source = nullptr;
    bool com = false;
    int result = 0;
    auto require = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };
    try
    {
        app.instance = GetModuleHandleW(nullptr);
        app.smoke = true;
        app.softwareRendering = true;
        app.iniPath = (std::filesystem::current_path() / L"history-keybinds-test.ini").wstring();
        std::exception_ptr keybindFailure;
        std::thread keybinds([&] {
            bool initialized = false;
            try
            {
                check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Cannot initialize keybind COM.");
                initialized = true;
                app.window = CreateWindowExW(0, L"STATIC", L"History keybind test", WS_POPUP,
                    0, 0, 1000, 700, nullptr, nullptr, app.instance, nullptr);
                require(app.window != nullptr, "Cannot create hidden keybind test owner.");
                checkHistoryKeybinds();
            }
            catch (...) { keybindFailure = std::current_exception(); }
            if (app.window) DestroyWindow(std::exchange(app.window, nullptr));
            if (initialized) CoUninitialize();
        });
        keybinds.join();
        if (keybindFailure) std::rethrow_exception(keybindFailure);
        station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        require(station && SetProcessWindowStation(station), "Cannot isolate clipboard history.");
        desktop = CreateDesktopW(L"TigerSnipHistoryTest", nullptr, nullptr, 0,
            DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS |
                DESKTOP_HOOKCONTROL,
            nullptr);
        require(desktop && SetThreadDesktop(desktop), "Cannot attach history test desktop.");
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Cannot initialize COM.");
        com = true;
        app.instance = GetModuleHandleW(nullptr);
        app.smoke = true;
        app.softwareRendering = true;
        app.iniPath = (std::filesystem::current_path() / L"history-test.ini").wstring();
        registerClasses();
        require(CreateWindowExW(0, MainClass, L"History test", WS_OVERLAPPEDWINDOW,
            0, 0, 1000, 700, nullptr, createMenu(), app.instance, nullptr), "Cannot create history owner.");
        source = CreateWindowExW(0, L"STATIC", L"Another application", WS_POPUP,
            0, 0, 32, 32, nullptr, nullptr, app.instance, nullptr);
        require(source && app.historyListening, "Clipboard listener was not attached.");
        auto pump = [] {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            { TranslateMessage(&message); DispatchMessageW(&message); }
        };
        auto publish = [&](const std::wstring &text) {
            require(copyText(source, text), "Another application could not copy text.");
            pump();
        };
        for (int i = 0; i < 25; ++i) publish(L"PART-" + std::to_wstring(i));
        require(app.clipboardHistory.entries().size() == 20 &&
            app.clipboardHistory.entries().front() == L"PART-24" &&
            app.clipboardHistory.entries().back() == L"PART-5", "Listener did not retain the latest 20 copies.");
        publish(L"PART-10");
        require(app.clipboardHistory.entries().size() == 20 &&
            app.clipboardHistory.entries().front() == L"PART-10", "Duplicate text did not move to the top.");
        publish(L" \r\n\t");
        require(app.clipboardHistory.entries().front() == L"PART-10", "Blank text entered history.");
        auto image = Bitmap::create(8, 8);
        require(copyBitmap(source, image, app.graphics.png(image)), "Cannot publish an image.");
        pump();
        require(app.clipboardHistory.entries().front() == L"PART-10", "An image changed text history.");
        for (const auto *format : {L"ExcludeClipboardContentFromMonitorProcessing", L"CanIncludeInClipboardHistory"})
        {
            require(copyText(source, L"excluded-secret"), "Cannot publish excluded text.");
            require(OpenClipboard(source), "Cannot mark excluded text.");
            HGLOBAL flag = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
            const bool set = flag && SetClipboardData(RegisterClipboardFormatW(format), flag);
            CloseClipboard();
            require(set, "Cannot publish exclusion flag.");
            pump();
            require(app.clipboardHistory.entries().front() == L"PART-10", "Explicitly excluded text entered history.");
        }
        publish(std::wstring(ClipboardTextHistory::MaxTextLength + 1, L'x'));
        require(app.clipboardHistory.entries().front() == L"PART-10", "Oversized clipboard was retained.");
        require(copyText(source, L"retry-after-busy"), "Cannot copy retry text.");
        require(OpenClipboard(source), "Cannot lock the test clipboard.");
        collectClipboardHistory();
        const bool retried = app.historyRetries > 0;
        CloseClipboard();
        require(retried, "Busy clipboard did not schedule a retry.");
        SendMessageW(app.window, WM_TIMER, ClipboardHistoryTimer, 0);
        require(app.clipboardHistory.entries().front() == L"retry-after-busy", "Busy clipboard retry lost text.");
        const std::wstring exact = L"  PART-&123\r\n\u03A9\tsecond line  ";
        publish(exact);
        // The global picker must stay usable while the editor is hidden or minimized.
        for (int state : {SW_SHOW, SW_HIDE, SW_SHOWMINIMIZED})
        {
            ShowWindow(app.window, state);
            SendMessageW(app.window, WM_HOTKEY, app.historyHotkeyId, 0);
            const HWND picker = app.historyPopup.window();
            RECT bounds{};
            // Visibility is suppressed on this noninteractive desktop; the
            // picker must still have its full geometry and not be iconic.
            require(picker && !IsIconic(picker) &&
                GetWindowRect(picker, &bounds) && bounds.right - bounds.left >= 400,
                "Background history shortcut opened a minimized picker.");
            require((state != SW_SHOWMINIMIZED || IsIconic(app.window)) &&
                (state != SW_HIDE || !IsWindowVisible(app.window)),
                "Opening history unexpectedly restored the editor.");
            SendMessageW(picker, WM_KEYDOWN, VK_ESCAPE, 0);
            require(!app.historyPopup.window(), "Background picker did not dismiss.");
        }
        ShowWindow(app.window, SW_HIDE);
        command(ClipboardHistory);
        require(IsWindow(app.historyPopup.window()), "The toolbar command did not open a picker.");
        // A copy while the picker is open must not change the meaning of its rows.
        publish(L"copied-while-picker-open");
        const HWND popup = app.historyPopup.window();
        const auto popupDpi = GetDpiForWindow(popup) / 96.0f;
        const auto idle = renderHistoryPopup(popup);
        SendMessageW(popup, WM_MOUSEMOVE, 0,
            MAKELPARAM(static_cast<int>(70 * popupDpi), static_cast<int>(90 * popupDpi)));
        const auto hovered = renderHistoryPopup(popup);
        require(hovered.pixels != idle.pixels, "History rows have no hover highlight.");
        ValidateRect(popup, nullptr);
        SendMessageW(popup, WM_MOUSEMOVE, 0,
            MAKELPARAM(static_cast<int>(71 * popupDpi), static_cast<int>(91 * popupDpi)));
        require(!GetUpdateRect(popup, nullptr, FALSE), "Moving within one option repaints the popup unnecessarily.");
        SendMessageW(popup, WM_MOUSEMOVE, 0,
            MAKELPARAM(static_cast<int>(30 * popupDpi), idle.height - static_cast<int>(20 * popupDpi)));
        const auto clearHover = renderHistoryPopup(popup);
        const size_t footerPixel = (static_cast<size_t>(idle.height - static_cast<int>(30 * popupDpi)) * idle.width +
                                   static_cast<int>(10 * popupDpi)) * 4;
        require(std::memcmp(idle.pixels.data() + footerPixel, clearHover.pixels.data() + footerPixel, 3) != 0,
            "Clear history has no hover highlight.");
        SendMessageW(popup, WM_MOUSELEAVE, 0, 0);
        require(renderHistoryPopup(popup).pixels == idle.pixels, "Hover highlight remains after the pointer leaves.");
        saveBytes(L"history-popup-idle.png", app.graphics.png(idle));
        saveBytes(L"history-popup-hover.png", app.graphics.png(hovered));
        saveBytes(L"history-popup-clear-hover.png", app.graphics.png(clearHover));
        SendMessageW(popup, WM_LBUTTONUP, 0,
            MAKELPARAM(static_cast<int>(70 * popupDpi), static_cast<int>(90 * popupDpi)));
        std::wstring copied;
        require(readClipboardHistoryText(source, copied) && copied == exact,
            "Picker click did not restore the exact snapshotted Unicode/multiline text.");
        require(!app.historyPopup.window() && app.clipboardHistory.entries().front() == exact,
            "Picker did not close or move the chosen text to the top.");
        require(IsWindow(app.textNotice.window()), "Recopy did not show text-copy feedback.");
        SendMessageW(app.window, WM_HOTKEY, app.historyHotkeyId, 0);
        require(IsWindow(app.historyPopup.window()), "History shortcut did not open the picker.");
        SendMessageW(app.historyPopup.window(), WM_KEYDOWN, VK_END, 0);
        SendMessageW(app.historyPopup.window(), WM_KEYDOWN, VK_RETURN, 0);
        require(readClipboardHistoryText(source, copied) && copied == L"PART-8",
            "Keyboard scrolling could not recopy the oldest item.");
        for (bool classic : {false, true})
            for (bool fullscreen : {false, true})
                for (unsigned collapsed : {0U, 1U, 7U})
                {
                    app.classicUI = classic; app.fullScreen = fullscreen; app.collapsedRows = collapsed;
                    buildButtons();
                    auto icon = std::find_if(app.buttons.begin(), app.buttons.end(), [](const Button &b) {
                        return b.command == ClipboardHistory;
                    });
                    require(icon != app.buttons.end() && enabled(ClipboardHistory), "History icon disappeared with no image.");
                    for (const auto &button : app.buttons)
                        if (button.command != ClipboardHistory)
                            require(icon->rect.right <= button.rect.left || icon->rect.left >= button.rect.right ||
                                    icon->rect.bottom <= button.rect.top || icon->rect.top >= button.rect.bottom,
                                "The history icon overlaps another control.");
                }
        app.classicUI = false; app.fullScreen = false; app.collapsedRows = 0;
        SendMessageW(app.window, HistoryClearMessage, 0, 0);
        pump();
        require(app.clipboardHistory.entries().empty() && readClipboardHistoryText(source, copied) && copied == L"PART-8",
            "Clear history changed the clipboard or immediately re-added its old text.");
        command(ClipboardHistory);
        require(app.historyPopup.window(), "Empty history could not be opened.");
        SendMessageW(app.historyPopup.window(), WM_KEYDOWN, VK_ESCAPE, 0);
        require(!app.historyPopup.window(), "Escape did not dismiss history.");
        publish(L"clear-on-exit");
        command(Exit);
        require(app.clipboardHistory.entries().empty() && !app.historyListening && !app.historyPopup.window(),
            "Exit left history or its listener behind.");
        std::cout << "PASS: external clipboard listener; latest 20; deduplication; blanks/images/exclusions/size limit; "
            "busy retry; stable picker rows; exact Unicode/multiline recopy; feedback; shortcut with visible/hidden/minimized editor; keyboard scroll; "
            "always-visible icon in both layouts; hover/clear highlight and stable repaint; "
            "Keybinds settings; four keybind conflicts/rollback/edit/disable/persistence; clear; Escape; Exit cleanup.\n";
    }
    catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; result = 1; }
    if (app.window && IsWindow(app.window)) command(Exit);
    if (source) DestroyWindow(source);
    if (com) CoUninitialize();
    SetThreadDesktop(originalDesktop);
    SetProcessWindowStation(originalStation);
    if (desktop) CloseDesktop(desktop);
    if (station) CloseWindowStation(station);
    return result;
}
