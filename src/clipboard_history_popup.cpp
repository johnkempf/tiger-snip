#include "clipboard_history_popup.h"
#include "clipboard.h"
#include "windows_support.h"
#include <windowsx.h>

namespace snip
{
namespace { constexpr wchar_t PopupClass[] = L"TigerSnip.ClipboardHistory.1"; }
ClipboardHistoryPopup::~ClipboardHistoryPopup()
{
    close(false);
    if (font_) DeleteObject(font_);
    if (titleFont_) DeleteObject(titleFont_);
}
void ClipboardHistoryPopup::close(bool restoreFocus)
{
    const bool foreground = window_ && GetForegroundWindow() == window_;
    if (window_) DestroyWindow(std::exchange(window_, nullptr));
    entries_.clear();
    previews_.clear();
    if (restoreFocus && foreground && IsWindow(returnWindow_))
        SetForegroundWindow(returnWindow_);
}
void ClipboardHistoryPopup::show(HWND owner, const std::vector<std::wstring> &entries,
                                  POINT anchor, bool dark, Color accent,
                                  UINT copyMessage, UINT clearMessage, const std::wstring &shortcut)
{
    close();
    owner_ = owner;
    returnWindow_ = GetForegroundWindow();
    entries_ = entries;
    previews_.clear();
    for (const auto &entry : entries_) previews_.push_back(clipboardHistoryLabel(entry));
    dark_ = dark; accent_ = accent;
    shortcut_ = shortcut;
    keyboardSelection_ = false;
    copyMessage_ = copyMessage; clearMessage_ = clearMessage;
    scroll_ = selected_ = 0; hover_ = -1;
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = procedure;
    cls.lpszClassName = PopupClass;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.style = CS_DROPSHADOW;
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throwWindowsError("Cannot register clipboard history.");
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor))
        throwWindowsError("Cannot position clipboard history.");
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, PopupClass, L"Clipboard history",
                              WS_POPUP, anchor.x, anchor.y, 1, 1, owner, nullptr, cls.hInstance, this);
    if (!window_) throwWindowsError("Cannot open clipboard history.");
    dpi_ = GetDpiForWindow(window_) / 96.0f;
    if (font_) DeleteObject(font_);
    if (titleFont_) DeleteObject(titleFont_);
    font_ = CreateFontW(-static_cast<int>(15 * dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    titleFont_ = CreateFontW(-static_cast<int>(15 * dpi_), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    const int maxRows = std::max(1, static_cast<int>((monitor.rcWork.bottom - monitor.rcWork.top) / dpi_ - 140) / 42);
    rows_ = std::min({10, maxRows, std::max(1, static_cast<int>(entries_.size()))});
    const int width = std::min(static_cast<int>(440 * dpi_), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
    const int height = static_cast<int>((74 + rows_ * 42 + 40) * dpi_);
    const int x = std::clamp(anchor.x + static_cast<int>(12 * dpi_), monitor.rcWork.left,
                              std::max(monitor.rcWork.left, monitor.rcWork.right - width));
    const int y = std::clamp(anchor.y + static_cast<int>(12 * dpi_), monitor.rcWork.top,
                              std::max(monitor.rcWork.top, monitor.rcWork.bottom - height));
    SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    SetForegroundWindow(window_);
    SetFocus(window_);
}
int ClipboardHistoryPopup::hit(POINT p) const
{
    RECT r{}; GetClientRect(window_, &r);
    const float x = p.x / dpi_, y = p.y / dpi_, width = r.right / dpi_;
    if (x >= width - 42 && y >= 8 && y < 40) return -2;
    if (!entries_.empty() && y >= 74 + rows_ * 42 && y < 114 + rows_ * 42 && x >= 8 && x < 132)
        return -3;
    if (x < 8 || x > width - 8 || y < 74 || y >= 74 + rows_ * 42) return -1;
    const int index = scroll_ + static_cast<int>((y - 74) / 42);
    return index < static_cast<int>(entries_.size()) ? index : -1;
}
void ClipboardHistoryPopup::scroll(int amount)
{
    scroll_ = std::clamp(scroll_ + amount, 0, std::max(0, static_cast<int>(entries_.size()) - rows_));
    hover_ = -1;
    InvalidateRect(window_, nullptr, FALSE);
}
void ClipboardHistoryPopup::choose(int index)
{
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    // The owner consumes this pointer synchronously before the snapshot is replaced.
    if (SendMessageW(owner_, copyMessage_, 0, reinterpret_cast<LPARAM>(&entries_[index])))
        close();
}
void ClipboardHistoryPopup::paint()
{
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(window_, &ps);
    struct End { HWND window; PAINTSTRUCT &ps; ~End() { EndPaint(window, &ps); } } end{window_, ps};
    RECT r{};
    GetClientRect(window_, &r);
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom);
    if (!memory || !bitmap)
    {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        draw(dc);
        return;
    }
    const auto previous = SelectObject(memory, bitmap);
    struct Release
    {
        HDC dc; HBITMAP bitmap; HGDIOBJ previous;
        ~Release() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
    } release{memory, bitmap, previous};
    draw(memory);
    BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
           ps.rcPaint.bottom - ps.rcPaint.top, memory, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
}
void ClipboardHistoryPopup::draw(HDC dc)
{
    RECT r{}; GetClientRect(window_, &r);
    const auto background = dark_ ? RGB(30, 34, 43) : RGB(255, 255, 255);
    const auto foreground = dark_ ? RGB(236, 239, 245) : RGB(32, 38, 46);
    const auto muted = dark_ ? RGB(178, 187, 202) : RGB(101, 111, 128);
    auto fill = [&](RECT box, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &box, b); DeleteObject(b); };
    fill(r, background);
    HBRUSH border = CreateSolidBrush(dark_ ? RGB(74, 80, 94) : RGB(211, 216, 224));
    FrameRect(dc, &r, border); DeleteObject(border);
    SetBkMode(dc, TRANSPARENT);
    auto previous = SelectObject(dc, titleFont_);
    auto text = [&](const std::wstring &value, int left, int top, int right, int bottom, COLORREF color) {
        RECT box{static_cast<LONG>(left * dpi_), static_cast<LONG>(top * dpi_),
                  static_cast<LONG>(right * dpi_), static_cast<LONG>(bottom * dpi_)};
        SetTextColor(dc, color);
        DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &box,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    };
    const int width = static_cast<int>(r.right / dpi_);
    const auto hoverColor = dark_ ? RGB(55, 60, 74) : RGB(240, 237, 252);
    if (hover_ == -2)
        fill({static_cast<LONG>((width - 42) * dpi_), static_cast<LONG>(8 * dpi_),
              static_cast<LONG>((width - 8) * dpi_), static_cast<LONG>(40 * dpi_)}, hoverColor);
    text(L"Clipboard history", 16, 12, width - 48, 36, dark_ ? foreground : accent_);
    text(L"\u00D7", width - 32, 10, width - 12, 38, muted);
    SelectObject(dc, font_);
    text(L"Click to copy" + (shortcut_.empty() ? std::wstring{} : L" \u00B7 " + shortcut_),
         16, 38, width - 16, 64, muted);
    if (entries_.empty())
        text(L"Copy text in any app to see it here.", 16, 74, width - 16, 116, muted);
    for (int row = 0; row < rows_ && scroll_ + row < static_cast<int>(entries_.size()); ++row)
    {
        const int index = scroll_ + row, top = 74 + row * 42;
        if (index == hover_ || (keyboardSelection_ && index == selected_))
            fill({static_cast<LONG>(8 * dpi_), static_cast<LONG>(top * dpi_),
                    r.right - static_cast<LONG>(8 * dpi_), static_cast<LONG>((top + 40) * dpi_)},
                 hoverColor);
        text(std::to_wstring(index + 1), 16, top, 44, top + 40, muted);
        text(previews_[index], 48, top, width - 24, top + 40, foreground);
    }
    const int footer = 74 + rows_ * 42;
    if (hover_ == -3)
        fill({static_cast<LONG>(8 * dpi_), static_cast<LONG>(footer * dpi_),
              static_cast<LONG>(132 * dpi_), static_cast<LONG>((footer + 38) * dpi_)}, hoverColor);
    text(L"Clear history", 16, footer, 132, footer + 38, entries_.empty() ? muted : foreground);
    const auto count = entries_.empty() ? L"0 / 20" : std::to_wstring(scroll_ + 1) + L"\u2013" +
        std::to_wstring(std::min(scroll_ + rows_, static_cast<int>(entries_.size()))) + L" of " +
        std::to_wstring(entries_.size()) + (static_cast<int>(entries_.size()) > rows_ ? L" \u00B7 Scroll" : L"");
    text(count, 146, footer, width - 16, footer + 38, muted);
    SelectObject(dc, previous);
}
LRESULT CALLBACK ClipboardHistoryPopup::procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept
{
    auto self = reinterpret_cast<ClipboardHistoryPopup *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        self = static_cast<ClipboardHistoryPopup *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wp, lp);
    try
    {
        switch (message)
        {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: self->paint(); return 0;
        case WM_PRINTCLIENT: self->draw(reinterpret_cast<HDC>(wp)); return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) self->close(false);
            return 0;
        case WM_CLOSE: self->close(); return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            const int hover = self->hit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (hover != self->hover_ || self->keyboardSelection_)
            {
                self->hover_ = hover;
                self->keyboardSelection_ = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            self->hover_ = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            const int index = self->hit({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (index == -2) self->close();
            else if (index == -3) { SendMessageW(self->owner_, self->clearMessage_, 0, 0); self->close(); }
            else self->choose(index);
            return 0;
        }
        case WM_MOUSEWHEEL:
            self->scroll(-GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 3); return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) self->close();
            else if (wp == VK_RETURN) self->choose(self->selected_);
            else if (wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END || wp == VK_PRIOR || wp == VK_NEXT)
            {
                self->keyboardSelection_ = true;
                const int amount = wp == VK_UP ? -1 : wp == VK_DOWN ? 1 : wp == VK_PRIOR ? -self->rows_ : self->rows_;
                self->selected_ = std::clamp(wp == VK_HOME ? 0 : wp == VK_END ? static_cast<int>(self->entries_.size()) - 1 :
                    self->selected_ + amount, 0, std::max(0, static_cast<int>(self->entries_.size()) - 1));
                if (self->selected_ < self->scroll_) self->scroll(self->selected_ - self->scroll_);
                else if (self->selected_ >= self->scroll_ + self->rows_)
                    self->scroll(self->selected_ - self->scroll_ - self->rows_ + 1);
                self->hover_ = -1; InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        default: break;
        }
    }
    catch (...) { self->close(); }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}
