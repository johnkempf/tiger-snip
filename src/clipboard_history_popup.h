#pragma once
#include "model.h"
#include <windows.h>

namespace snip
{
// A snapshot keeps rows stable if another app copies text while the picker is open.
class ClipboardHistoryPopup
{
    HWND window_ = nullptr, owner_ = nullptr, returnWindow_ = nullptr;
    HFONT font_ = nullptr, titleFont_ = nullptr;
    std::vector<std::wstring> entries_, previews_;
    std::wstring shortcut_;
    UINT copyMessage_ = 0, clearMessage_ = 0;
    bool dark_ = false, keyboardSelection_ = false;
    Color accent_ = 0;
    float dpi_ = 1;
    int rows_ = 1, scroll_ = 0, selected_ = 0, hover_ = -1;
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM) noexcept;
    void draw(HDC);
    void paint();
    void scroll(int);
    void choose(int);
    int hit(POINT) const;
public:
    ~ClipboardHistoryPopup();
    void close(bool restoreFocus = true);
    void show(HWND owner, const std::vector<std::wstring> &entries, POINT anchor,
              bool dark, Color accent, UINT copyMessage, UINT clearMessage,
              const std::wstring &shortcut);
    HWND window() const { return window_; }
};
}
