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

namespace snip
{
namespace
{
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
                ClipboardFailure *failure)
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
            hPng = allocate(nullptr, 0, png.data(), png.size());
    if (!hV5 || !hDib || !hPng)
    {
        if (hV5)
            GlobalFree(hV5);
        if (hDib)
            GlobalFree(hDib);
        if (hPng)
            GlobalFree(hPng);
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
    if (!success && failure)
        failure->code = GetLastError();
    CloseClipboard();
    if (hV5)
        GlobalFree(hV5);
    if (hDib)
        GlobalFree(hDib);
    if (hPng)
        GlobalFree(hPng);
    return success;
}
} // namespace snip
