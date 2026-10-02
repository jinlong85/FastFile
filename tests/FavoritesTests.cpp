#include "FavoriteStarUI.h"
#include "FavoritesJson.h"
#include <iostream>
#include <algorithm>

class TestStar : public CFavoriteStarUI {
public:
    void Hot(bool hot) { m_uButtonState = hot ? UISTATE_HOT : 0; }
};

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << "FAIL " << message << '\n'; ++failures; }
    };
    const std::vector<std::wstring> paths{L"C:\\", L"D:\\中文\\folder", L"\\\\server\\share", L"D:\\quote\"\t\n", L"D:\\emoji\xd83d\xde00"};
    std::string json = FavoritesJson::Encode(paths);
    std::vector<std::wstring> decoded;
    check(FavoritesJson::Decode(std::wstring(json.begin(), json.end()), decoded) && decoded == paths,
        "JSON preserves order, Chinese, Unicode, roots, UNC and escapes");
    check(FavoritesJson::Decode(L"[]", decoded) && decoded.empty(), "empty favorites");
    for (const auto* invalid : {L"[\"a\",]", L"[\"a\"]junk", L"[\"\\x\"]", L"[\"\\u00zz\"]", L"[\"\\u0000\"]", L"[\"unterminated]"})
        check(!FavoritesJson::Decode(invalid, decoded), "reject malformed JSON");

    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token;
    check(Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok, "GDI+ startup");
    for (int size : {32, 48, 64}) {
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = size;
        bi.bmiHeader.biHeight = -size;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
        auto old = SelectObject(dc, bitmap);
        TestStar star;
        star.SetFixedWidth(size);
        star.SetFixedHeight(size);
        star.SetPos({0, 0, size, size}, false);
        auto paint = [&]() {
            auto* data = static_cast<DWORD*>(pixels);
            std::fill(data, data + size * size, 0x00ffffff);
            star.PaintStatusImage(dc);
            GdiFlush();
        };
        auto count = [&](DWORD color) {
            int count = 0;
            auto* data = static_cast<DWORD*>(pixels);
            for (int i = 0; i < size * size; ++i) if ((data[i] & 0xffffff) == color) ++count;
            return count;
        };
        auto grayStroke = [&](int gray) {
            // A one-pixel diagonal can consist entirely of antialiased pixels;
            // allow its coverage to blend the requested ink with the white surface.
            int count = 0;
            auto* data = static_cast<DWORD*>(pixels);
            for (int i = 0; i < size * size; ++i) {
                int r = (data[i] >> 16) & 255, g = (data[i] >> 8) & 255, b = data[i] & 255;
                if (r == g && g == b && r >= gray && r <= gray + 50) ++count;
            }
            return count;
        };
        paint();
        check(grayStroke(92) > 0, "normal outline uses #5C5C5C with antialiasing");
        check((static_cast<DWORD*>(pixels)[size / 2 * size + size / 2] & 0xffffff) == 0xffffff, "normal star is hollow");
        int left = size, right = 0, top = size, bottom = 0;
        for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x)
            if ((static_cast<DWORD*>(pixels)[y * size + x] & 0xffffff) != 0xffffff) {
                left = (std::min)(left, x); right = (std::max)(right, x);
                top = (std::min)(top, y); bottom = (std::max)(bottom, y);
            }
        check(right - left + 1 <= size / 2 && bottom - top + 1 <= size / 2, "16 logical icon inside 32 logical hit area at 100/150/200 percent");
        check(left >= size / 4 - 1 && right < size * 3 / 4 + 1, "star horizontally centered");
        star.Hot(true); paint();
        check(grayStroke(26) > 0 && count(0xe8e8e8) > 0, "hover dark outline and pale background");
        check((static_cast<DWORD*>(pixels)[0] & 0xffffff) == 0xffffff, "hover corner remains rounded");
        star.Hot(false); star.SetPinned(true); paint();
        check(star.IsPinned() && count(0xc7a300) > 0, "pinned uses #C7A300");
        check((static_cast<DWORD*>(pixels)[size / 2 * size + size / 2] & 0xffffff) == 0xc7a300, "pinned star is solid");
        star.SetPinned(false); paint();
        check(!star.IsPinned() && count(0xc7a300) == 0, "unpin restores hollow state");
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    }
    Gdiplus::GdiplusShutdown(token);
    return failures ? 1 : 0;
}
