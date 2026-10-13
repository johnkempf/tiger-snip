#pragma once
#include "model.h"
#include <windows.h>
namespace snip
{
class Graphics;
std::optional<Bitmap> readClipboardBitmap(HWND owner, Graphics &graphics);
struct ClipboardFailure
{
    const char *operation = "Cannot publish the image to the Windows clipboard.";
    DWORD code = ERROR_GEN_FAILURE;
    bool unavailable = false;
};
bool copyBitmap(HWND owner, const Bitmap &bitmap, const std::vector<uint8_t> &png,
                ClipboardFailure *failure = nullptr);
bool copyText(HWND owner, const std::wstring &text, ClipboardFailure *failure = nullptr);
} // namespace snip
