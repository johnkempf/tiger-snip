#include "clipboard.h"
#include "windows_support.h"
#include "graphics.h"
#include <shellapi.h>
#include <fstream>
#include <bit>
#include <filesystem>
#include <climits>
#include <cstring>
#include <stdexcept>
#include <cwctype>

namespace snip
{
namespace
{
constexpr uint32_t AnnotationMagic = 0x4e415354;
std::vector<uint8_t> encodeAnnotation(const Annotation &item, Graphics &graphics)
{
    std::vector<uint8_t> bytes;
    auto put = [&](auto value) {
        const auto *data = reinterpret_cast<const uint8_t *>(&value);
        bytes.insert(bytes.end(), data, data + sizeof(value));
    };
    auto point = [&](Point p) { put(p.x); put(p.y); };
    put(AnnotationMagic); put(uint32_t{2}); put(uint32_t{0});
    put(static_cast<uint32_t>(item.kind)); put(item.color); put(uint32_t{item.style});
    const uint32_t flags = item.bold | (item.boxed << 1) | (item.textFrame << 2) |
                           (item.curveFlipped << 3) | (static_cast<bool>(item.raster) << 4) |
                           (static_cast<uint32_t>(item.textAlignment) << 5);
    put(flags); put(item.thickness); put(item.opacity); point(item.a); point(item.b);
    put(item.fontSize); put(item.textWidth); put(item.textHeight);
    put(static_cast<uint32_t>(item.points.size()));
    for (Point p : item.points) point(p);
    put(static_cast<uint32_t>(item.text.size()));
    const auto *text = reinterpret_cast<const uint8_t *>(item.text.data());
    bytes.insert(bytes.end(), text, text + item.text.size() * sizeof(wchar_t));
    put(static_cast<uint32_t>(item.cutouts.size()));
    for (Rect r : item.cutouts) { put(r.left); put(r.top); put(r.right); put(r.bottom); }
    const auto png = item.raster ? graphics.png(*item.raster) : std::vector<uint8_t>{};
    put(static_cast<uint32_t>(png.size()));
    bytes.insert(bytes.end(), png.begin(), png.end());
    const auto size = static_cast<uint32_t>(bytes.size());
    std::memcpy(bytes.data() + 8, &size, sizeof(size));
    return bytes;
}
struct AnnotationReader
{
    const uint8_t *data;
    size_t remaining;
    const uint8_t *take(size_t size)
    {
        if (size > remaining) throw std::runtime_error("Truncated annotation clipboard data.");
        const auto *result = data;
        data += size;
        remaining -= size;
        return result;
    }
    template<class T> T get()
    {
        T value;
        std::memcpy(&value, take(sizeof(value)), sizeof(value));
        return value;
    }
    float number()
    {
        const auto value = get<float>();
        if (!std::isfinite(value) || std::abs(value) > 10000000)
            throw std::runtime_error("Invalid annotation coordinates.");
        return value;
    }
    Point point() { return {number(), number()}; }
    uint32_t count(uint32_t maximum, size_t elementSize)
    {
        const auto count = get<uint32_t>();
        if (count > maximum || count > remaining / elementSize)
            throw std::runtime_error("Invalid annotation clipboard size.");
        return count;
    }
};
std::optional<Annotation> decodeAnnotation(const std::vector<uint8_t> &bytes, Graphics &graphics)
{
    try
    {
        AnnotationReader r{bytes.data(), bytes.size()};
        if (r.get<uint32_t>() != AnnotationMagic) return std::nullopt;
        const auto version = r.get<uint32_t>();
        if (version != 1 && version != 2) return std::nullopt;
        const auto size = r.get<uint32_t>();
        if (size < 12 || size > bytes.size()) return std::nullopt;
        r.remaining = size - 12; // GlobalAlloc may pad the clipboard allocation.
        Annotation item;
        const auto kind = r.get<uint32_t>();
        constexpr uint32_t styles[] = {1, 1, 3, 5, 6, 4, 5, 1, 1};
        if (kind >= std::size(styles)) return std::nullopt;
        item.kind = static_cast<Tool>(kind);
        item.color = r.get<uint32_t>();
        const auto style = r.get<uint32_t>(), flags = r.get<uint32_t>();
        if (item.color > 0xffffff || style >= styles[kind] ||
            (version == 1 ? flags > 31 : (flags > 127 || (flags >> 5) > 2))) return std::nullopt;
        item.textAlignment = static_cast<TextAlignment>(flags >> 5);
        item.style = static_cast<uint8_t>(style);
        item.bold = flags & 1; item.boxed = flags & 2; item.textFrame = flags & 4;
        item.curveFlipped = flags & 8;
        item.thickness = r.number(); item.opacity = r.number(); item.a = r.point(); item.b = r.point();
        item.fontSize = r.number(); item.textWidth = r.number(); item.textHeight = r.number();
        if (item.thickness < 0 || item.thickness > 10000 || item.opacity < 0 || item.opacity > 1 ||
            item.fontSize < 8 || item.fontSize > 144 || item.textWidth <= 0 || item.textHeight < 0)
            return std::nullopt;
        const auto points = r.count(1000000, sizeof(float) * 2);
        item.points.reserve(points);
        for (uint32_t i = 0; i < points; ++i) item.points.push_back(r.point());
        const auto length = r.count(1000000, sizeof(wchar_t));
        item.text.resize(length);
        std::memcpy(item.text.data(), r.take(length * sizeof(wchar_t)), length * sizeof(wchar_t));
        const auto cuts = r.count(10000, sizeof(float) * 4);
        for (uint32_t i = 0; i < cuts; ++i)
        {
            const Rect cut{r.number(), r.number(), r.number(), r.number()};
            if (cut.width() < 0 || cut.height() < 0) return std::nullopt;
            item.cutouts.push_back(cut);
        }
        const auto pngSize = r.count(512000000, 1);
        if (static_cast<bool>(flags & 16) != (pngSize > 0) ||
            (item.kind == Tool::Select) != (pngSize > 0)) return std::nullopt;
        if (pngSize)
        {
            const auto *png = r.take(pngSize);
            item.raster = std::make_shared<const Bitmap>(graphics.decode({png, png + pngSize}));
        }
        if (r.remaining || ((item.kind == Tool::Pen || item.kind == Tool::Highlight) && item.points.empty()))
            return std::nullopt;
        return item;
    }
    catch (const std::runtime_error &) { return std::nullopt; }
}
std::optional<Bitmap> clipboardDib(const std::vector<uint8_t> &bytes)
{
    if (bytes.size() < sizeof(BITMAPINFOHEADER))
        return std::nullopt;
    BITMAPINFOHEADER h{};
    std::memcpy(&h, bytes.data(), sizeof(h));
    if (h.biSize < sizeof(h) || h.biSize > bytes.size() || h.biWidth <= 0 ||
        h.biHeight == 0 || h.biHeight == LONG_MIN || h.biPlanes != 1 ||
        (h.biBitCount != 24 && h.biBitCount != 32) ||
        (h.biCompression != BI_RGB && h.biCompression != BI_BITFIELDS))
        return std::nullopt;
    const int height = std::abs(h.biHeight);
    if (static_cast<uint64_t>(h.biWidth) * height > 128000000)
        throw std::runtime_error("The clipboard image is too large.");
    size_t offset = h.biSize;
    uint32_t masks[4] = {0x00ff0000, 0x0000ff00, 0x000000ff, 0};
    if (h.biCompression == BI_BITFIELDS)
    {
        if (h.biBitCount != 32 || (h.biSize != 40 && h.biSize < 52))
            return std::nullopt;
        const size_t maskBytes = h.biSize >= 56 ? 16 : 12;
        if (bytes.size() < 40 + maskBytes)
            return std::nullopt;
        std::memcpy(masks, bytes.data() + 40, maskBytes);
        if (h.biSize == 40)
            offset += 12;
        if (!masks[0] || !masks[1] || !masks[2] || (masks[0] & masks[1]) ||
            (masks[0] & masks[2]) || (masks[1] & masks[2]) ||
            (masks[3] & (masks[0] | masks[1] | masks[2])))
            return std::nullopt;
    }
    if (h.biClrUsed > 256)
        return std::nullopt;
    offset += static_cast<size_t>(h.biClrUsed) * sizeof(RGBQUAD);
    const uint64_t stride = ((static_cast<uint64_t>(h.biWidth) * h.biBitCount + 31) / 32) * 4;
    if (offset > bytes.size() || stride * height > bytes.size() - offset)
        return std::nullopt;
    auto result = Bitmap::create(h.biWidth, height);
    auto channel = [](uint32_t pixel, uint32_t mask) -> uint8_t {
        if (!mask)
            return 255;
        const int shift = std::countr_zero(mask);
        return static_cast<uint8_t>((static_cast<uint64_t>((pixel & mask) >> shift) * 255) /
                                    (mask >> shift));
    };
    for (int y = 0; y < height; ++y)
    {
        const auto *row = bytes.data() + offset + stride * (h.biHeight < 0 ? y : height - 1 - y);
        for (int x = 0; x < h.biWidth; ++x)
        {
            auto *out = result.pixels.data() + (static_cast<size_t>(y) * h.biWidth + x) * 4;
            if (h.biBitCount == 24)
            {
                std::memcpy(out, row + x * 3, 3);
                out[3] = 255;
            }
            else
            {
                uint32_t pixel;
                std::memcpy(&pixel, row + x * 4, 4);
                out[0] = channel(pixel, masks[2]);
                out[1] = channel(pixel, masks[1]);
                out[2] = channel(pixel, masks[0]);
                out[3] = masks[3] ? channel(pixel, masks[3]) : 255;
            }
        }
    }
    return result;
}
}
std::optional<Annotation> readClipboardAnnotation(HWND owner, Graphics &graphics)
{
    std::vector<uint8_t> bytes;
    {
        if (!OpenClipboard(owner)) throwWindowsError("The clipboard is busy. Try pasting again.");
        struct Close { ~Close() { CloseClipboard(); } } close;
        HANDLE memory = GetClipboardData(RegisterClipboardFormatW(AnnotationClipboardFormat));
        const size_t size = memory ? GlobalSize(memory) : 0;
        if (!size || size > 512000000) return std::nullopt;
        const auto *data = static_cast<const uint8_t *>(GlobalLock(memory));
        if (!data) return std::nullopt;
        try { bytes.assign(data, data + size); }
        catch (...) { GlobalUnlock(memory); throw; }
        GlobalUnlock(memory);
    }
    return decodeAnnotation(bytes, graphics);
}
bool copyAnnotation(HWND owner, const Annotation &item, Graphics &graphics, ClipboardFailure *failure,
                    const Bitmap *backdrop)
{
    if (item.whiteout) return false;
    const auto metadata = encodeAnnotation(item, graphics);
    const auto image = graphics.annotationImage(item, backdrop);
    return copyBitmap(owner, image, graphics.png(image), failure, metadata);
}
std::optional<Bitmap> readClipboardBitmap(HWND owner, Graphics &graphics)
{
    std::vector<std::vector<uint8_t>> encoded;
    std::vector<std::vector<uint8_t>> dibs;
    std::vector<std::wstring> files;
    std::optional<Bitmap> native;
    {
        if (!OpenClipboard(owner))
            throwWindowsError("The clipboard is busy. Try pasting again.");
        struct Close { ~Close() { CloseClipboard(); } } close;
        auto read = [](UINT format) -> std::vector<uint8_t> {
            HANDLE memory = GetClipboardData(format);
            if (!memory)
                return {};
            const size_t size = GlobalSize(memory);
            if (!size || size > 512000000)
                return {};
            const auto *data = static_cast<const uint8_t *>(GlobalLock(memory));
            if (!data)
                return {};
            std::vector<uint8_t> result;
            try { result.assign(data, data + size); }
            catch (...) { GlobalUnlock(memory); throw; }
            GlobalUnlock(memory);
            return result;
        };
        for (const wchar_t *name : {L"PNG", L"image/png"})
            if (auto bytes = read(RegisterClipboardFormatW(name)); !bytes.empty())
                encoded.push_back(std::move(bytes));
        for (UINT format : {static_cast<UINT>(CF_DIBV5), static_cast<UINT>(CF_DIB)})
            if (auto bytes = read(format); !bytes.empty())
                dibs.push_back(std::move(bytes));
        if (auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP)))
        {
            const UINT count = std::min(DragQueryFileW(drop, UINT_MAX, nullptr, 0), 32U);
            for (UINT i = 0; i < count; ++i)
            {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(length + 1, L'\0');
                if (DragQueryFileW(drop, i, path.data(), length + 1))
                {
                    path.resize(length);
                    files.push_back(std::move(path));
                }
            }
        }
        // Device-dependent bitmaps are a last fallback for older clipboard producers.
        if (encoded.empty() && dibs.empty())
            if (auto bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP)))
            {
                BITMAP size{};
                if (GetObjectW(bitmap, sizeof(size), &size) && size.bmWidth > 0 && size.bmHeight > 0)
                {
                    auto result = Bitmap::create(size.bmWidth, size.bmHeight);
                    BITMAPINFO info{};
                    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    info.bmiHeader.biWidth = result.width;
                    info.bmiHeader.biHeight = -result.height;
                    info.bmiHeader.biPlanes = 1;
                    info.bmiHeader.biBitCount = 32;
                    HDC dc = GetDC(nullptr);
                    const int rows = dc ? GetDIBits(dc, bitmap, 0, result.height, result.pixels.data(),
                                                    &info, DIB_RGB_COLORS) : 0;
                    if (dc) ReleaseDC(nullptr, dc);
                    if (rows == result.height)
                    {
                        for (size_t i = 3; i < result.pixels.size(); i += 4)
                            result.pixels[i] = 255;
                        native = std::move(result);
                    }
                }
            }
    }
    for (const auto &bytes : encoded)
        try { return graphics.decode(bytes); }
        catch (const std::runtime_error &) { }
    for (const auto &bytes : dibs)
        if (auto result = clipboardDib(bytes))
            return result;
    if (native)
        return native;
    for (const auto &path : files)
    {
        std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
        if (!file)
            continue;
        const auto size = file.tellg();
        if (size <= 0 || size > 512000000)
            continue;
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char *>(bytes.data()), size))
            continue;
        try { return graphics.decode(bytes); }
        catch (const std::runtime_error &) { }
    }
    return std::nullopt;
}
void ClipboardTextHistory::record(const std::wstring &text)
{
    if (text.empty() || text.size() > MaxTextLength ||
        std::all_of(text.begin(), text.end(), [](wchar_t c) { return std::iswspace(c); }))
        return;
    const auto existing = std::find(items.begin(), items.end(), text);
    if (existing != items.end()) items.erase(existing);
    items.insert(items.begin(), text);
    if (items.size() > Capacity) items.resize(Capacity);
}
std::wstring clipboardHistoryLabel(const std::wstring &text)
{
    std::wstring result;
    bool space = false;
    size_t i = 0;
    for (; i < text.size() && result.size() < 80; ++i)
    {
        const wchar_t c = text[i];
        if (std::iswspace(c) || std::iswcntrl(c))
        {
            space = !result.empty();
            continue;
        }
        if (space) result += L' ';
        space = false;
        result += c;
    }
    if (i < text.size())
    {
        if (!result.empty() && result.back() >= 0xd800 && result.back() <= 0xdbff)
            result.pop_back();
        result += L"\u2026";
    }
    return result;
}
bool readClipboardHistoryText(HWND owner, std::wstring &text)
{
    text.clear();
    if (!OpenClipboard(owner)) return false;
    struct Close { ~Close() { CloseClipboard(); } } close;
    if (IsClipboardFormatAvailable(RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing")))
        return true;
    if (HANDLE flag = GetClipboardData(RegisterClipboardFormatW(L"CanIncludeInClipboardHistory")))
    {
        if (GlobalSize(flag) < sizeof(DWORD)) return true;
        const auto *value = static_cast<const DWORD *>(GlobalLock(flag));
        if (!value) return true;
        const bool excluded = *value == 0;
        GlobalUnlock(flag);
        if (excluded) return true;
    }
    HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    const size_t bytes = memory ? GlobalSize(memory) : 0;
    if (bytes < sizeof(wchar_t) || bytes > (ClipboardTextHistory::MaxTextLength + 1) * sizeof(wchar_t))
        return true;
    const auto *data = static_cast<const wchar_t *>(GlobalLock(memory));
    if (!data) return true;
    struct Unlock { HANDLE memory; ~Unlock() { GlobalUnlock(memory); } } unlock{memory};
    const auto *end = std::find(data, data + bytes / sizeof(wchar_t), L'\0');
    if (end != data + bytes / sizeof(wchar_t)) text.assign(data, end);
    return true;
}
bool copyText(HWND owner, const std::wstring &text, ClipboardFailure *failure)
{
    if (text.empty())
        return false;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory)
        throw std::runtime_error("Not enough memory to copy the text.");
    void *data = GlobalLock(memory);
    if (!data)
    {
        GlobalFree(memory);
        throwWindowsError("Cannot prepare the text clipboard.");
    }
    std::memcpy(data, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (!OpenClipboard(owner))
    {
        if (failure)
            *failure = {"Clipboard is busy. Try the text snip again.", GetLastError(), true};
        GlobalFree(memory);
        return false;
    }
    const bool success = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    if (!success && failure)
        *failure = {"Cannot copy text to the clipboard.", GetLastError(), false};
    CloseClipboard();
    if (!success)
        GlobalFree(memory);
    return success;
}

bool copyBitmap(HWND owner, const Bitmap &bitmap, const std::vector<uint8_t> &png,
                ClipboardFailure *failure, const std::vector<uint8_t> &annotation)
{
    const size_t imageSize = bitmap.pixels.size();
    auto allocate = [&](const void *header, size_t headerSize, const uint8_t *pixels,
                        size_t count) -> HGLOBAL {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, headerSize + count);
        if (!memory)
            return nullptr;
        auto data = static_cast<uint8_t *>(GlobalLock(memory));
        if (!data)
        {
            GlobalFree(memory);
            return nullptr;
        }
        if (headerSize)
            std::memcpy(data, header, headerSize);
        std::memcpy(data + headerSize, pixels, count);
        GlobalUnlock(memory);
        return memory;
    };
    BITMAPV5HEADER v5{};
    v5.bV5Size = sizeof(v5);
    v5.bV5Width = bitmap.width;
    v5.bV5Height = -bitmap.height;
    v5.bV5Planes = 1;
    v5.bV5BitCount = 32;
    v5.bV5Compression = BI_BITFIELDS;
    v5.bV5SizeImage = static_cast<DWORD>(imageSize);
    v5.bV5RedMask = 0x00ff0000;
    v5.bV5GreenMask = 0x0000ff00;
    v5.bV5BlueMask = 0x000000ff;
    v5.bV5AlphaMask = 0xff000000;
    v5.bV5CSType = LCS_sRGB;
    v5.bV5Intent = LCS_GM_IMAGES;
    BITMAPINFOHEADER dib{};
    dib.biSize = sizeof(dib);
    dib.biWidth = bitmap.width;
    dib.biHeight = -bitmap.height;
    dib.biPlanes = 1;
    dib.biBitCount = 32;
    dib.biCompression = BI_RGB;
    dib.biSizeImage = static_cast<DWORD>(imageSize);
    HGLOBAL hV5 = allocate(&v5, sizeof(v5), bitmap.pixels.data(), imageSize),
            hDib = allocate(&dib, sizeof(dib), bitmap.pixels.data(), imageSize),
            hPng = allocate(nullptr, 0, png.data(), png.size()),
            hAnnotation = annotation.empty() ? nullptr : allocate(nullptr, 0, annotation.data(), annotation.size());
    if (!hV5 || !hDib || !hPng || (!annotation.empty() && !hAnnotation))
    {
        if (hV5)
            GlobalFree(hV5);
        if (hDib)
            GlobalFree(hDib);
        if (hPng)
            GlobalFree(hPng);
        if (hAnnotation) GlobalFree(hAnnotation);
        throw std::runtime_error("Not enough memory to copy the image.");
    }
    UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    if (!OpenClipboard(owner))
    {
        if (failure)
            *failure = {"Cannot open the Windows clipboard. Another program may be using it.",
                        GetLastError(), true};
        GlobalFree(hV5);
        GlobalFree(hDib);
        GlobalFree(hPng);
        if (hAnnotation) GlobalFree(hAnnotation);
        return false;
    }
    if (!EmptyClipboard())
    {
        if (failure)
            *failure = {"Cannot prepare the Windows clipboard for the image.", GetLastError()};
        CloseClipboard();
        GlobalFree(hV5);
        GlobalFree(hDib);
        GlobalFree(hPng);
        if (hAnnotation) GlobalFree(hAnnotation);
        return false;
    }
    bool success = false;
    // Advertise the lossless alpha-preserving format first to clipboard consumers.
    if (pngFormat && SetClipboardData(pngFormat, hPng))
    {
        hPng = nullptr;
        success = true;
    }
    if (SetClipboardData(CF_DIBV5, hV5))
    {
        hV5 = nullptr;
        success = true;
    }
    if (SetClipboardData(CF_DIB, hDib))
    {
        hDib = nullptr;
        success = true;
    }
    if (hAnnotation)
    {
        if (SetClipboardData(RegisterClipboardFormatW(AnnotationClipboardFormat), hAnnotation))
            hAnnotation = nullptr;
        else
        {
            success = false;
            if (failure) failure->operation = "Cannot copy the editable annotation.";
        }
    }
    if (!success && failure)
        failure->code = GetLastError();
    CloseClipboard();
    if (hV5)
        GlobalFree(hV5);
    if (hDib)
        GlobalFree(hDib);
    if (hPng)
        GlobalFree(hPng);
    if (hAnnotation) GlobalFree(hAnnotation);
    return success;
}
} // namespace snip
