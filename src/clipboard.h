#pragma once
#include "model.h"
#include <windows.h>
namespace snip
{
class Graphics;
inline constexpr wchar_t AnnotationClipboardFormat[] = L"Tiger Snip.Annotation.v1";
std::optional<Annotation> readClipboardAnnotation(HWND owner, Graphics &graphics);
std::optional<Bitmap> readClipboardBitmap(HWND owner, Graphics &graphics);
struct ClipboardFailure
{
    const char *operation = "Cannot publish the image to the Windows clipboard.";
    DWORD code = ERROR_GEN_FAILURE;
    bool unavailable = false;
};
bool copyBitmap(HWND owner, const Bitmap &bitmap, const std::vector<uint8_t> &png,
                ClipboardFailure *failure = nullptr, const std::vector<uint8_t> &annotation = {});
bool copyAnnotation(HWND owner, const Annotation &item, Graphics &graphics,
                    ClipboardFailure *failure = nullptr, const Bitmap *backdrop = nullptr);
bool copyText(HWND owner, const std::wstring &text, ClipboardFailure *failure = nullptr);
// Returns false only when the clipboard is busy and the caller should retry.
// Empty, non-text, oversized and explicitly excluded clipboard data are ignored.
bool readClipboardHistoryText(HWND owner, std::wstring &text);
class ClipboardTextHistory
{
public:
    static constexpr size_t Capacity = 20;
    static constexpr size_t MaxTextLength = 1024 * 1024;
    void record(const std::wstring &text);
    void clear() { items.clear(); }
    const std::vector<std::wstring> &entries() const { return items; }
private:
    std::vector<std::wstring> items;
};
std::wstring clipboardHistoryLabel(const std::wstring &text);
} // namespace snip
