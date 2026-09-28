// FastFile - Shell icon and thumbnail extraction, caches, worker thread
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

bool CMainWnd::IsImageExtension(const std::wstring& name)
{
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = name.substr(dot);
    for (auto& ch : ext) ch = static_cast<wchar_t>(towlower(ch));
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg"
        || ext == L".bmp" || ext == L".gif" || ext == L".tif" || ext == L".tiff"
        || ext == L".webp" || ext == L".ico";
}

bool CMainWnd::IsVideoExtension(const std::wstring& name)
{
    const wchar_t* ext = ::PathFindExtensionW(name.c_str());
    if (!ext || !*ext) return false;
    static const wchar_t* kExts[] = {
        L".mp4", L".mkv", L".avi", L".wmv", L".mov", L".m4v",
        L".webm", L".flv", L".mpeg", L".mpg", L".ts", L".m2ts",
        L".3gp", L".asf", L".vob"
    };
    for (auto e : kExts) {
        if (_wcsicmp(ext, e) == 0) return true;
    }
    return false;
}

bool CMainWnd::EnsureGdiplus()
{
    using namespace Gdiplus;
    // Called from the UI thread *and* the thumbnail worker, so the one-time startup has to be
    // thread-safe: two racing GdiplusStartup calls leave GDI+ with a clobbered token and a
    // half-initialised state, which showed up as rare access violations inside GDI+.
    static std::once_flag once;
    static bool ready = false;
    std::call_once(once, [] {
        GdiplusStartupInput input;
        ULONG_PTR token = 0;
        ready = (GdiplusStartup(&token, &input, nullptr) == Ok);
    });
    return ready;
}

bool CMainWnd::GetPngEncoderClsid(CLSID* pClsid)
{
    if (!pClsid) return false;
    using namespace Gdiplus;
    // Both the UI thread and the thumbnail worker save PNGs, so resolve the encoder once
    // instead of enumerating GDI+ encoders (which is not cheap and is shared state) per save.
    static std::once_flag once;
    static CLSID png = {};
    static bool found = false;
    std::call_once(once, [] {
        UINT num = 0, size = 0;
        GetImageEncodersSize(&num, &size);
        if (size == 0) return;
        std::vector<BYTE> buf(size);
        auto* info = reinterpret_cast<ImageCodecInfo*>(buf.data());
        GetImageEncoders(num, size, info);
        for (UINT i = 0; i < num; ++i) {
            if (wcscmp(info[i].MimeType, L"image/png") == 0) {
                png = info[i].Clsid;
                found = true;
                return;
            }
        }
    });
    if (!found) return false;
    *pClsid = png;
    return true;
}

void CMainWnd::WipeDirectoryFiles(const std::wstring& dirNoSlash)
{
    if (dirNoSlash.empty()) return;
    WIN32_FIND_DATAW fd = {};
    const std::wstring pattern = dirNoSlash + L"\\*";
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        const std::wstring full = dirNoSlash + L"\\" + fd.cFileName;
        ::DeleteFileW(full.c_str());
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
}

// Render an HICON 1:1 into a top-down 32bpp BGRA buffer.
// DrawIconEx *scaling* is unfiltered (the driver does a plain StretchBlt), which is
// where the jagged list / tile / preview icons came from. So we always rasterise at
// the icon's own bitmap size and leave any resampling to GDI+ below.
bool CMainWnd::RenderIconToArgbBuffer(HICON hIcon, int w, int h, std::vector<BYTE>& out)
{
    out.clear();
    if (!hIcon || w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;

    HDC hdc = ::GetDC(nullptr);
    HDC mem = ::CreateCompatibleDC(hdc);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = ::CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) ::DeleteObject(dib);
        ::DeleteDC(mem);
        ::ReleaseDC(nullptr, hdc);
        return false;
    }
    HGDIOBJ old = ::SelectObject(mem, dib);
    // Zero-fill; DrawIconEx writes real per-pixel alpha. Do NOT force A=255.
    ::ZeroMemory(bits, static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    ::SetBkMode(mem, TRANSPARENT);
    ::DrawIconEx(mem, 0, 0, hIcon, w, h, 0, nullptr, DI_NORMAL);

    // Mask-style icons may leave A=0 on every pixel. Promote colored pixels to
    // opaque only; keep transparent holes (A=0). Never force A=255 on all pixels.
    {
        DWORD* px = static_cast<DWORD*>(bits);
        const int n = w * h;
        bool anyAlpha = false;
        for (int i = 0; i < n; ++i) {
            const BYTE a = static_cast<BYTE>((px[i] >> 24) & 0xFFu);
            if (a != 0) { anyAlpha = true; break; }
        }
        if (!anyAlpha) {
            for (int i = 0; i < n; ++i) {
                if ((px[i] & 0x00FFFFFFu) != 0)
                    px[i] |= 0xFF000000u;
            }
        }
    }

    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    out.assign(static_cast<const BYTE*>(bits), static_cast<const BYTE*>(bits) + bytes);

    ::SelectObject(mem, old);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, hdc);
    return true;
}

// GDI+ HighQualityBicubic resample of a packed top-down 32bpp ARGB buffer.
bool CMainWnd::ResizeArgbBuffer(const std::vector<BYTE>& src, int sw, int sh,
    int dw, int dh, std::vector<BYTE>& dst)
{
    using namespace Gdiplus;
    dst.clear();
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return false;
    if (src.size() < static_cast<size_t>(sw) * static_cast<size_t>(sh) * 4u) return false;
    if (!EnsureGdiplus()) return false;

    Bitmap s(sw, sh, sw * 4, PixelFormat32bppARGB,
        const_cast<BYTE*>(src.data()));
    if (s.GetLastStatus() != Ok) return false;
    Bitmap d(dw, dh, PixelFormat32bppARGB);
    if (d.GetLastStatus() != Ok) return false;

    {
        Graphics g(&d);
        if (g.GetLastStatus() != Ok) return false;
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.SetCompositingQuality(CompositingQualityHighQuality);
        g.Clear(Color(0, 0, 0, 0));
        g.DrawImage(&s, Rect(0, 0, dw, dh), 0, 0, sw, sh, UnitPixel);
    }

    BitmapData bd = {};
    Rect lockRc(0, 0, dw, dh);
    if (d.LockBits(&lockRc, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok)
        return false;
    const BYTE* p = static_cast<const BYTE*>(bd.Scan0);
    dst.assign(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4u, 0);
    for (int y = 0; y < dh; ++y) {
        ::memcpy(dst.data() + static_cast<size_t>(y) * static_cast<size_t>(dw) * 4u,
            p + static_cast<ptrdiff_t>(y) * bd.Stride,
            static_cast<size_t>(dw) * 4u);
    }
    d.UnlockBits(&bd);
    return true;
}

bool CMainWnd::SaveIconToPng(HICON hIcon, const std::wstring& pngPath, int cx, int cy)
{
    if (!hIcon || cx <= 0 || cy <= 0 || pngPath.empty()) return false;
    using namespace Gdiplus;
    if (!EnsureGdiplus()) return false;

    // Native size of the icon bitmap. Rendering 1:1 and letting GDI+ resample keeps
    // edges smooth when the Shell has no image list at exactly the requested size
    // (e.g. 192px tiles come from the 384px JUMBO list).
    int natW = cx, natH = cy;
    {
        ICONINFO ii = {};
        if (::GetIconInfo(hIcon, &ii)) {
            BITMAP bm = {};
            if (ii.hbmColor && ::GetObject(ii.hbmColor, sizeof(bm), &bm) == sizeof(bm)
                && bm.bmWidth > 0 && bm.bmHeight > 0) {
                natW = bm.bmWidth;
                natH = bm.bmHeight;
            }
            if (ii.hbmColor) ::DeleteObject(ii.hbmColor);
            if (ii.hbmMask) ::DeleteObject(ii.hbmMask);
        }
    }
    if (natW <= 0 || natH <= 0 || natW > 8192 || natH > 8192) {
        natW = cx;
        natH = cy;
    }

    std::vector<BYTE> px;
    if (!RenderIconToArgbBuffer(hIcon, natW, natH, px)) return false;

    if (natW != cx || natH != cy) {
        std::vector<BYTE> scaled;
        if (ResizeArgbBuffer(px, natW, natH, cx, cy, scaled) && !scaled.empty()) {
            px.swap(scaled);
        } else if (!RenderIconToArgbBuffer(hIcon, cx, cy, px)) {
            return false; // fall back to plain DrawIconEx scaling
        }
    }

    // Bind scan0 so PNG encoder keeps true alpha (DuiLib needs A<255 somewhere for AlphaBlend).
    Bitmap bmp(cx, cy, cx * 4, PixelFormat32bppARGB, px.data());
    if (bmp.GetLastStatus() != Ok) return false;
    CLSID clsidPng = {};
    if (!GetPngEncoderClsid(&clsidPng)) return false;
    return bmp.Save(pngPath.c_str(), &clsidPng, nullptr) == Ok;
}

// Trim fully transparent borders from a thumb PNG. Shell/GDI+ thumbs are produced inside
// a square box with transparent bands, and cropping those lets the icon view draw each
// picture at its own aspect ratio.
bool CMainWnd::CropPngToContentAlpha(const std::wstring& pngPath)
{
    if (pngPath.empty()) return false;
    using namespace Gdiplus;
    if (!EnsureGdiplus()) return false;

    int cw = 0, ch = 0, cx = 0, cy = 0;
    {
        Bitmap bmp(pngPath.c_str());
        if (bmp.GetLastStatus() != Ok) return false;
        const int w = bmp.GetWidth();
        const int h = bmp.GetHeight();
        if (w <= 0 || h <= 0) return false;
        BitmapData bd = {};
        Rect rc(0, 0, w, h);
        if (bmp.LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok)
            return false;
        int minX = w, minY = h, maxX = -1, maxY = -1;
        for (int y = 0; y < h; ++y) {
            const BYTE* row = static_cast<const BYTE*>(bd.Scan0)
                + static_cast<ptrdiff_t>(y) * bd.Stride;
            for (int x = 0; x < w; ++x) {
                if (row[x * 4 + 3] >= 8) {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
        bmp.UnlockBits(&bd);
        if (maxX < 0) return false;                       // fully transparent
        if (minX == 0 && minY == 0 && maxX == w - 1 && maxY == h - 1)
            return true;                                  // nothing to trim
        cx = minX; cy = minY;
        cw = maxX - minX + 1;
        ch = maxY - minY + 1;
    }

    const std::wstring tmp = pngPath + L".crop.png";
    bool written = false;
    {
        Bitmap src(pngPath.c_str());
        if (src.GetLastStatus() != Ok) return false;
        Bitmap dst(cw, ch, PixelFormat32bppARGB);
        if (dst.GetLastStatus() != Ok) return false;
        {
            Graphics g(&dst);
            if (g.GetLastStatus() != Ok) return false;
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            g.Clear(Color(0, 0, 0, 0));
            g.DrawImage(&src, Rect(0, 0, cw, ch), cx, cy, cw, ch, UnitPixel);
        }
        CLSID clsidPng = {};
        if (!GetPngEncoderClsid(&clsidPng)) return false;
        ::DeleteFileW(tmp.c_str());
        written = (dst.Save(tmp.c_str(), &clsidPng, nullptr) == Ok);
    }
    if (!written) { ::DeleteFileW(tmp.c_str()); return false; }
    ::DeleteFileW(pngPath.c_str());
    if (!::MoveFileW(tmp.c_str(), pngPath.c_str())) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool CMainWnd::SaveImageThumbnailPng(const std::wstring& srcPath, const std::wstring& pngPath, int cx, int cy)
{
    using namespace Gdiplus;
    if (!EnsureGdiplus()) return false;

    Bitmap src(srcPath.c_str());
    if (src.GetLastStatus() != Ok)
        return false;

    Bitmap dst(cx, cy, PixelFormat32bppARGB);
    Graphics g(&dst);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    // Transparent letterbox (PNG true alpha) — DuiLib AlphaBlend; no white halo / black pocket.
    g.Clear(Color(0, 0, 0, 0));

    const int sw = src.GetWidth();
    const int sh = src.GetHeight();
    if (sw <= 0 || sh <= 0) return false;
    double scale = (std::min)(static_cast<double>(cx) / sw, static_cast<double>(cy) / sh);
    int dw = static_cast<int>(sw * scale);
    int dh = static_cast<int>(sh * scale);
    int ox = (cx - dw) / 2;
    int oy = (cy - dh) / 2;
    g.DrawImage(&src, ox, oy, dw, dh);

    CLSID clsidPng = {};
    if (!GetPngEncoderClsid(&clsidPng)) return false;
    return dst.Save(pngPath.c_str(), &clsidPng, nullptr) == Ok;
}

std::wstring CMainWnd::PeekCachedIconBmp(const std::wstring& path, bool isDir, int cx)
{
    if (cx < 16) cx = 16;
    if (cx > 256) cx = 256;
    wchar_t cacheKey[32] = {};
    swprintf_s(cacheKey, L"@%d", cx);
    std::wstring key = path + cacheKey;

    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }

    size_t h = std::hash<std::wstring>{}(key);
    wchar_t name[80] = {};
    swprintf_s(name, L"%08X_%s_%d_v8.png", static_cast<unsigned>(h & 0xFFFFFFFF), isDir ? L"d" : L"f", cx);
    std::wstring bmpPath = m_iconCacheDir + name;
    if (::PathFileExistsW(bmpPath.c_str())) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    return {};
}

std::wstring CMainWnd::GetShellIconBmp(const std::wstring& path, bool isDir, int cx)
{
    if (cx < 16) cx = 16;
    if (cx > 256) cx = 256;

    wchar_t cacheKey[32] = {};
    swprintf_s(cacheKey, L"@%d", cx);
    std::wstring key = path + cacheKey;

    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }

    size_t h = std::hash<std::wstring>{}(key);
    wchar_t name[80] = {};
    swprintf_s(name, L"%08X_%s_%d_v8.png", static_cast<unsigned>(h & 0xFFFFFFFF), isDir ? L"d" : L"f", cx);
    std::wstring bmpPath = m_iconCacheDir + name;

    if (::PathFileExistsW(bmpPath.c_str())) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }

    // Content thumbs (transparent letterbox PNG) only for real image/video files.
    // Chrome/tree/details/folder/drive: HICON only — never SIIGBF_ICONONLY (black pocket).
    const bool wantContentThumb = (cx > 32) && !isDir
        && (IsImageExtension(path) || IsVideoExtension(path));
    if (wantContentThumb && ::PathFileExistsW(path.c_str())
        && ExtractShellItemImage(path, cx, cx, bmpPath)) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }

    if (ExtractShellIconSized(path, isDir, cx, bmpPath)) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }

    if (wantContentThumb && IsImageExtension(path)) {
        if (SaveImageThumbnailPng(path, bmpPath, cx, cx)) {
            std::lock_guard<std::mutex> lock(m_iconCacheMutex);
            m_iconCache[key] = bmpPath;
            return bmpPath;
        }
    }
    return {};
}

std::wstring CMainWnd::GetShellFileIconBmp(const std::wstring& path, bool isDir, int cx)
{
    if (cx < 16) cx = 16;
    if (cx > 256) cx = 256;

    wchar_t cacheKey[32] = {};
    swprintf_s(cacheKey, L"@%d", cx);
    std::wstring key = path + L"#ico" + cacheKey;

    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }

    size_t h = std::hash<std::wstring>{}(key);
    wchar_t name[80] = {};
    swprintf_s(name, L"%08X_%s_%d_ico_v8.png", static_cast<unsigned>(h & 0xFFFFFFFF), isDir ? L"d" : L"f", cx);
    std::wstring bmpPath = m_iconCacheDir + name;

    if (::PathFileExistsW(bmpPath.c_str())) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }

    if (ExtractShellIconSized(path, isDir, cx, bmpPath)) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    return {};
}

bool CMainWnd::SaveHBitmapToPng(HBITMAP hbm, const std::wstring& pngPath)
{
    if (!hbm || pngPath.empty()) return false;
    using namespace Gdiplus;
    if (!EnsureGdiplus()) return false;
    Bitmap bmp(hbm, nullptr);
    if (bmp.GetLastStatus() != Ok) return false;
    CLSID clsidPng = {};
    if (!GetPngEncoderClsid(&clsidPng)) return false;
    return bmp.Save(pngPath.c_str(), &clsidPng, nullptr) == Ok;
}

bool CMainWnd::LetterboxHBitmapToPng(HBITMAP hbm, int cx, int cy, const std::wstring& pngPath)
{
    if (!hbm || cx <= 0 || cy <= 0 || pngPath.empty()) return false;
    using namespace Gdiplus;
    if (!EnsureGdiplus()) return false;

    Bitmap src(hbm, nullptr);
    if (src.GetLastStatus() != Ok) return false;
    const int sw = src.GetWidth();
    const int sh = src.GetHeight();
    if (sw <= 0 || sh <= 0) return false;

    Bitmap dst(cx, cy, PixelFormat32bppARGB);
    Graphics g(&dst);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    // Transparent letterbox (or ColorContent white) — PNG true alpha for AlphaBlend.
    g.Clear(Color(0, 0, 0, 0));
    const double scale = (std::min)(static_cast<double>(cx) / sw, static_cast<double>(cy) / sh);
    const int dw = (std::max)(1, static_cast<int>(sw * scale));
    const int dh = (std::max)(1, static_cast<int>(sh * scale));
    const int ox = (cx - dw) / 2;
    const int oy = (cy - dh) / 2;
    g.DrawImage(&src, ox, oy, dw, dh);

    CLSID clsidPng = {};
    if (!GetPngEncoderClsid(&clsidPng)) return false;
    return dst.Save(pngPath.c_str(), &clsidPng, nullptr) == Ok;
}

bool CMainWnd::ExtractShellItemImage(const std::wstring& path, int cx, int cy, const std::wstring& pngPath)
{
    // Content thumbs only (image/video). Never SIIGBF_ICONONLY — folders/drives use HICON.
    if (path.empty() || cx <= 0 || cy <= 0 || pngPath.empty()) return false;
    IShellItem* psi = nullptr;
    HRESULT hr = ::SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&psi));
    if (FAILED(hr) || !psi) return false;

    IShellItemImageFactory* pFactory = nullptr;
    hr = psi->QueryInterface(IID_PPV_ARGS(&pFactory));
    psi->Release();
    if (FAILED(hr) || !pFactory) return false;

    SIZE sz = { cx, cy };
    HBITMAP hbm = nullptr;
    hr = pFactory->GetImage(sz, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK, &hbm);
    pFactory->Release();
    if (FAILED(hr) || !hbm) return false;

    const bool ok = LetterboxHBitmapToPng(hbm, cx, cy, pngPath);
    ::DeleteObject(hbm);
    if (!ok) return false;
    // Tighten: the icon view fits the thumb by its own aspect ratio.
    CropPngToContentAlpha(pngPath);
    return true;
}

bool CMainWnd::ExtractShellIconSized(const std::wstring& path, bool isDir, int cx, const std::wstring& bmpPath)
{
    if (cx < 8) cx = 8;
    if (cx > 512) cx = 512;

    SHFILEINFOW sfi = {};
    // Prefer real path lookup so Known Folders (Desktop/Documents/Downloads)
    // keep their special Shell icons. USEFILEATTRIBUTES only as fallback.
    UINT flags = SHGFI_SYSICONINDEX;
    DWORD attrs = 0;
    DWORD_PTR ok = 0;
    if (::PathFileExistsW(path.c_str())) {
        ok = ::SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), flags);
    }
    if (!ok) {
        flags = SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES;
        attrs = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        ok = ::SHGetFileInfoW(path.c_str(), attrs, &sfi, sizeof(sfi), flags);
    }

    // Pick the system image list by its *actual* icon size. The old fixed thresholds
    // (16/32/48/256) grabbed SHIL_LARGE - 48px at 150% DPI - for a 24px slot, and SHIL_JUMBO
    // (384px) for a 72px slot, so every icon was squashed by DrawIconEx without filtering.
    // The real sizes scale with DPI (24 / 48 / 72 / 384 here), so an exact match usually
    // exists and no resampling is needed at all.
    static const int kLists[] = { SHIL_SMALL, SHIL_LARGE, SHIL_EXTRALARGE, SHIL_JUMBO };
    IImageList* best = nullptr;
    int bestSize = 0;
    for (int shil : kLists) {
        IImageList* piml = nullptr;
        if (FAILED(::SHGetImageList(shil, IID_IImageList, reinterpret_cast<void**>(&piml))) || !piml)
            continue;
        int w = 0, h = 0;
        if (FAILED(piml->GetIconSize(&w, &h)) || w <= 0) {
            piml->Release();
            continue;
        }
        const bool better = (best == nullptr)
            || (bestSize < cx && w > bestSize)                 // grow towards the target size
            || (w >= cx && (bestSize < cx || w < bestSize));   // then the smallest that covers it
        if (better) {
            if (best) best->Release();
            best = piml;
            bestSize = w;
        } else {
            piml->Release();
        }
    }

    if (best) {
        HICON hIcon = nullptr;
        HRESULT hr = best->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon);
        best->Release();
        if (SUCCEEDED(hr) && hIcon) {
            bool saved = SaveIconToPng(hIcon, bmpPath, cx, cx);
            ::DestroyIcon(hIcon);
            if (saved) return true;
        }
    }

    sfi = {};
    flags = SHGFI_ICON | ((cx <= 16) ? SHGFI_SMALLICON : SHGFI_LARGEICON);
    if (isDir) flags |= SHGFI_USEFILEATTRIBUTES;
    ok = ::SHGetFileInfoW(path.c_str(), isDir ? FILE_ATTRIBUTE_DIRECTORY : 0,
        &sfi, sizeof(sfi), flags);
    if (!ok || !sfi.hIcon)
        return false;
    bool saved = SaveIconToPng(sfi.hIcon, bmpPath, cx, cx);
    ::DestroyIcon(sfi.hIcon);
    return saved;
}

void CMainWnd::ApplyControlForeIcon(CControlUI* ctrl, const std::wstring& bmp,
    int iconPx, int destX, int destY, bool clearText)
{
    if (!ctrl || bmp.empty() || iconPx <= 0) return;
    CDuiString imgAttr;
    imgAttr.Format(_T("file='%s' dest='%d,%d,%d,%d'"),
        bmp.c_str(), destX, destY, destX + iconPx, destY + iconPx);
    ctrl->SetAttribute(_T("foreimage"), imgAttr.GetData());
    ctrl->SetAttribute(_T("hotforeimage"), imgAttr.GetData());
    if (clearText)
        ctrl->SetText(_T(""));
}

bool CMainWnd::ExtractStockIconSized(int siid, int cx, const std::wstring& bmpPath)
{
    if (cx < 16) cx = 16;
    SHSTOCKICONINFO sii = {};
    sii.cbSize = sizeof(sii);
    // Prefer ICONLOCATION + sized extract for DPI-correct glyphs
    if (SUCCEEDED(::SHGetStockIconInfo(static_cast<SHSTOCKICONID>(siid), SHGSI_ICONLOCATION, &sii))
        && sii.szPath[0] != L'\0') {
        HICON hIcon = nullptr;
        if (SUCCEEDED(::SHDefExtractIconW(sii.szPath, sii.iIcon, 0, &hIcon, nullptr, static_cast<UINT>(cx)))
            && hIcon) {
            const bool saved = SaveIconToPng(hIcon, bmpPath, cx, cx);
            ::DestroyIcon(hIcon);
            if (saved) return true;
        }
    }
    sii = {};
    sii.cbSize = sizeof(sii);
    const UINT fl = SHGSI_ICON | ((cx <= 16) ? SHGSI_SMALLICON : SHGSI_LARGEICON);
    if (FAILED(::SHGetStockIconInfo(static_cast<SHSTOCKICONID>(siid), fl, &sii)) || !sii.hIcon)
        return false;
    const bool saved = SaveIconToPng(sii.hIcon, bmpPath, cx, cx);
    ::DestroyIcon(sii.hIcon);
    return saved;
}

bool CMainWnd::ExtractModuleIconSized(const wchar_t* moduleFile, int index, int cx, const std::wstring& bmpPath)
{
    if (!moduleFile || index < 0 || cx <= 0) return false;
    wchar_t sys[MAX_PATH] = {};
    UINT n = ::GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    std::wstring full = sys;
    if (!full.empty() && full.back() != L'\\') full.push_back(L'\\');
    full += moduleFile;

    HICON hIcon = nullptr;
    UINT got = ::PrivateExtractIconsW(full.c_str(), index, cx, cx, &hIcon, nullptr, 1, LR_DEFAULTCOLOR);
    if (got == 0 || !hIcon) {
        // Fallback via ExtractIconEx (avoid identifier "small" — Windows headers macro it).
        HICON hLarge = nullptr;
        HICON hSmall = nullptr;
        if (::ExtractIconExW(full.c_str(), index, &hLarge, &hSmall, 1) > 0) {
            if (cx <= 16) {
                hIcon = hSmall ? hSmall : hLarge;
            } else {
                hIcon = hLarge ? hLarge : hSmall;
            }
            if (hSmall && hSmall != hIcon) ::DestroyIcon(hSmall);
            if (hLarge && hLarge != hIcon) ::DestroyIcon(hLarge);
        }
    }
    if (!hIcon) return false;
    const bool saved = SaveIconToPng(hIcon, bmpPath, cx, cx);
    ::DestroyIcon(hIcon);
    return saved;
}

bool CMainWnd::RenderGlyphToPng(wchar_t glyph, int px, COLORREF color, const std::wstring& pngPath)
{
    if (px <= 0 || pngPath.empty()) return false;
    if (!EnsureGdiplus()) return false;
    using namespace Gdiplus;

    // Draw the glyph white-on-black into a 32bpp DIB, then treat the red channel as coverage
    // and rebuild the pixels as straight-alpha ARGB in the requested colour.
    HDC hdc = ::GetDC(nullptr);
    if (!hdc) return false;
    HDC mem = ::CreateCompatibleDC(hdc);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = px;
    bi.bmiHeader.biHeight = -px;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = ::CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) ::DeleteObject(dib);
        ::DeleteDC(mem);
        ::ReleaseDC(nullptr, hdc);
        return false;
    }
    ::ZeroMemory(bits, static_cast<size_t>(px) * static_cast<size_t>(px) * 4u);
    HGDIOBJ oldBmp = ::SelectObject(mem, dib);

    LOGFONTW lf = {};
    lf.lfHeight = -px;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = ANTIALIASED_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe MDL2 Assets");
    HFONT font = ::CreateFontIndirectW(&lf);
    HGDIOBJ oldFont = font ? ::SelectObject(mem, font) : nullptr;
    ::SetBkMode(mem, TRANSPARENT);
    ::SetTextColor(mem, RGB(255, 255, 255));
    RECT rc = { 0, 0, px, px };
    ::DrawTextW(mem, &glyph, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    bool ok = false;
    {
        Bitmap out(px, px, PixelFormat32bppARGB);
        Rect lock(0, 0, px, px);
        BitmapData bd = {};
        if (out.GetLastStatus() == Ok
            && out.LockBits(&lock, ImageLockModeWrite, PixelFormat32bppARGB, &bd) == Ok) {
            const DWORD* src = static_cast<const DWORD*>(bits);
            BYTE* dst = static_cast<BYTE*>(bd.Scan0);
            const BYTE cr = GetRValue(color), cg = GetGValue(color), cb = GetBValue(color);
            for (int y = 0; y < px; ++y) {
                BYTE* row = dst + static_cast<size_t>(y) * static_cast<size_t>(bd.Stride);
                for (int x = 0; x < px; ++x) {
                    const BYTE coverage = static_cast<BYTE>(src[y * px + x] & 0xFFu);
                    row[x * 4 + 0] = cb;
                    row[x * 4 + 1] = cg;
                    row[x * 4 + 2] = cr;
                    row[x * 4 + 3] = coverage;
                }
            }
            out.UnlockBits(&bd);
            CLSID clsidPng = {};
            ok = GetPngEncoderClsid(&clsidPng) && out.Save(pngPath.c_str(), &clsidPng, nullptr) == Ok;
        }
    }

    if (oldFont) ::SelectObject(mem, oldFont);
    if (font) ::DeleteObject(font);
    ::SelectObject(mem, oldBmp);
    ::DeleteObject(dib);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, hdc);
    return ok;
}

std::wstring CMainWnd::GetGlyphIconBmp(wchar_t glyph, int px, COLORREF color)
{
    if (px < 8) px = 8;
    if (px > 128) px = 128;
    wchar_t keybuf[96] = {};
    swprintf_s(keybuf, L"glyph:%04X@%d#%06X", static_cast<unsigned>(glyph), px,
        static_cast<unsigned>(color & 0x00FFFFFFu));
    const std::wstring key = keybuf;

    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }

    size_t h = std::hash<std::wstring>{}(key);
    wchar_t name[96] = {};
    swprintf_s(name, L"gly_%08X_%04X_%d_v1.png", static_cast<unsigned>(h & 0xFFFFFFFFu),
        static_cast<unsigned>(glyph), px);
    std::wstring bmpPath = m_iconCacheDir + name;
    if (::PathFileExistsW(bmpPath.c_str())) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    if (RenderGlyphToPng(glyph, px, color, bmpPath)) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    return {};
}

std::wstring CMainWnd::GetStockIconBmp(int siid, int cx)
{
    if (cx < 16) cx = 16;
    if (cx > 256) cx = 256;
    wchar_t keybuf[64] = {};
    swprintf_s(keybuf, L"stock:%d@%d", siid, cx);
    const std::wstring key = keybuf;

    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }

    size_t h = std::hash<std::wstring>{}(key);
    wchar_t name[80] = {};
    swprintf_s(name, L"stk_%08X_%d_%d_v8.png", static_cast<unsigned>(h & 0xFFFFFFFFu), siid, cx);
    std::wstring bmpPath = m_iconCacheDir + name;
    if (::PathFileExistsW(bmpPath.c_str())) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    if (ExtractStockIconSized(siid, cx, bmpPath)) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    return {};
}

std::wstring CMainWnd::GetModuleIconBmp(const wchar_t* moduleFile, int index, int cx)
{
    if (!moduleFile || index < 0) return {};
    if (cx < 16) cx = 16;
    if (cx > 256) cx = 256;
    wchar_t keybuf[128] = {};
    swprintf_s(keybuf, L"mod:%s#%d@%d", moduleFile, index, cx);
    const std::wstring key = keybuf;

    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }

    size_t h = std::hash<std::wstring>{}(key);
    wchar_t name[96] = {};
    swprintf_s(name, L"mod_%08X_%d_%d_v8.png", static_cast<unsigned>(h & 0xFFFFFFFFu), index, cx);
    std::wstring bmpPath = m_iconCacheDir + name;
    if (::PathFileExistsW(bmpPath.c_str())) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    if (ExtractModuleIconSized(moduleFile, index, cx, bmpPath)) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    return {};
}

void CMainWnd::ApplyChromeShellIcons()
{
    // Toolbar glyphs: UiTokens::ToolbarIconPx (Win11 command-bar density).
    const int iconPx = DpiScale(UiTokens::ToolbarIconPx);
    const int navIconPx = DpiScale(UiTokens::NavIconPx); // favorites / tree stay 16
    if (iconPx <= 0) return;

    auto applyBtn = [&](LPCTSTR name, const std::wstring& bmp, bool clearText) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c || bmp.empty()) return;
        int bw = c->GetFixedWidth();
        int bh = c->GetFixedHeight();
        if (bw <= 0) bw = DpiScale(UiTokens::ToolbarBtnW);
        if (bh <= 0) bh = DpiScale(UiTokens::ToolbarBtnH);
        int x = (bw - iconPx) / 2;
        int y = (bh - iconPx) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        ApplyControlForeIcon(c, bmp, iconPx, x, y, clearText);
        c->Invalidate();
    };

    auto applyFav = [&](LPCTSTR name, const std::wstring& bmp) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c || bmp.empty()) return;
        const int pad = DpiScale(UiTokens::NavIconPad);
        int bh = c->GetFixedHeight();
        if (bh <= 0) bh = DpiScale(UiTokens::NavRowH);
        int y = (bh - navIconPx) / 2;
        if (y < 0) y = 0;
        ApplyControlForeIcon(c, bmp, navIconPx, pad, y, false);
        CDuiString tp;
        tp.Format(_T("%d,0,%d,0"), pad + navIconPx + DpiScale(UiTokens::NavIconTextGap), DpiScale(UiTokens::NavTextPadR));
        c->SetAttribute(_T("textpadding"), tp.GetData());
        c->Invalidate();
    };

    auto applyFluent = [&](LPCTSTR name, wchar_t glyph) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) return;
        wchar_t text[2] = { glyph, L'\0' };
        c->SetAttribute(_T("foreimage"), _T(""));
        c->SetAttribute(_T("hotforeimage"), _T(""));
        c->SetAttribute(_T("font"), _T("6"));
        c->SetAttribute(_T("textpadding"), _T("0,0,0,0"));
        c->SetText(text);
        c->Invalidate();
    };

    // Command-bar buttons that show an icon *and* a label ("新建 ⌄"): the glyph becomes a
    // bitmap so the label keeps the UI font, and textpadding keeps the two from overlapping.
    // (These used to be two adjacent buttons - a glyph button plus a text button - which made
    // the hover highlight cover only half of the visual button.)
    auto applyGlyphLabel = [&](LPCTSTR name, wchar_t glyph) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) return;
        const int px = DpiScale(UiTokens::ToolbarGlyphPx);
        std::wstring bmp = GetGlyphIconBmp(glyph, px, RGB(0x1A, 0x1A, 0x1A));
        if (bmp.empty()) return;
        int bh = c->GetFixedHeight();
        if (bh <= 0) bh = DpiScale(UiTokens::CmdBtnH);
        const int padL = DpiScale(UiTokens::ToolbarIconPad);
        int y = (bh - px) / 2;
        if (y < 0) y = 0;
        ApplyControlForeIcon(c, bmp, px, padL, y, false);
        CDuiString tp;
        tp.Format(_T("%d,0,%d,0"),
            padL + px + DpiScale(UiTokens::SpaceXs), DpiScale(UiTokens::SpaceSm));
        c->SetAttribute(_T("textpadding"), tp);
        c->Invalidate();
    };

    // Windows built-in Segoe MDL2 glyphs keep the command bar visually aligned
    // with Explorer without copying icons or using legacy coloured shell32 art.
    applyFluent(_T("btn_back"), 0xE0A6);
    applyFluent(_T("btn_forward"), 0xE0AB);
    applyFluent(_T("btn_up"), 0xE74A);
    applyFluent(_T("btn_refresh"), 0xE72C);
    applyFluent(_T("btn_cut"), 0xE8C6);
    applyFluent(_T("btn_copy"), 0xE8C8);
    applyFluent(_T("btn_paste"), 0xE77F);
    applyFluent(_T("btn_rename"), 0xE8AC);
    applyFluent(_T("btn_share"), 0xE72D);
    applyFluent(_T("btn_delete"), 0xE74D);
    applyFluent(_T("btn_more"), 0xE712);
    applyFluent(_T("btn_toggle_preview"), 0xE7F4);
    applyFluent(_T("btn_newfolder"), 0xE710);

    // Single-button 新建 / 排序 / 查看: glyph bitmap + label + chevron.
    applyGlyphLabel(_T("btn_new"), 0xE710);
    applyGlyphLabel(_T("btn_sort"), 0xE8CB);
    applyGlyphLabel(_T("btn_view_menu"), 0xE80D);

    // Keep hidden legacy view buttons iconized for UpdateViewModeButtons
    applyBtn(_T("btn_view_xlarge"), GetModuleIconBmp(L"shell32.dll", 257, iconPx), true);
    applyBtn(_T("btn_view_large"), GetModuleIconBmp(L"shell32.dll", 257, iconPx), true);
    applyBtn(_T("btn_view_medium"), GetStockIconBmp(SIID_IMAGEFILES, iconPx), true);
    applyBtn(_T("btn_view_list"), GetModuleIconBmp(L"shell32.dll", 253, iconPx), true);
    applyBtn(_T("btn_view_details"), GetModuleIconBmp(L"shell32.dll", 253, iconPx), true);
    applyBtn(_T("btn_view_tiles"), GetStockIconBmp(SIID_STACK, iconPx), true);

    // --- Quick Access favorites: real known-folder / stock This PC icons ---
    applyFav(_T("fav_thispc"), GetStockIconBmp(SIID_DESKTOPPC, navIconPx));
    {
        std::wstring docs = GetKnownFolderPath(CSIDL_PERSONAL);
        applyFav(_T("fav_documents"), docs.empty()
            ? GetStockIconBmp(SIID_FOLDER, navIconPx)
            : GetShellIconBmp(docs, true, navIconPx));
    }
    {
        std::wstring desk = GetKnownFolderPath(CSIDL_DESKTOPDIRECTORY);
        applyFav(_T("fav_desktop"), desk.empty()
            ? GetStockIconBmp(SIID_DESKTOPPC, navIconPx)
            : GetShellIconBmp(desk, true, navIconPx));
    }
    {
        std::wstring down = GetDownloadsPath();
        applyFav(_T("fav_downloads"), down.empty()
            ? GetStockIconBmp(SIID_FOLDER, navIconPx)
            : GetShellIconBmp(down, true, navIconPx));
    }

    m_PaintManager.NeedUpdate();
}

void CMainWnd::ApplyTreeNodeIcon(CTreeNodeUI* node, const std::wstring& path)
{
    if (!node) return;
    COptionUI* item = node->GetItemButton();
    if (!item) return;

    const int iconPx = DpiScale(16);
    std::wstring bmp;
    if (IsThisPcPath(path)) {
        bmp = GetStockIconBmp(SIID_DESKTOPPC, iconPx);
    } else if (path.size() >= 2 && path[1] == L':' && (path.size() == 2
        || (path.size() <= 3 && (path.back() == L'\\' || path.back() == L'/')))) {
        // Drive root eg. C: or C:/
        std::wstring drive = path;
        if (drive.back() != L'\\' && drive.back() != L'/')
            drive.push_back(L'\\');
        bmp = GetShellIconBmp(drive, false, iconPx);
        if (bmp.empty())
            bmp = GetStockIconBmp(SIID_DRIVEFIXED, iconPx);
    } else if (!path.empty() && path != kPendingMarker) {
        bmp = GetShellIconBmp(path, true, iconPx);
        if (bmp.empty())
            bmp = GetStockIconBmp(SIID_FOLDER, iconPx);
    }
    if (bmp.empty()) return;

    const int pad = DpiScale(UiTokens::NavIconPad);
    int bh = node->GetFixedHeight();
    if (bh <= 0) bh = DpiScale(UiTokens::TreeRowH);
    int y = (bh - iconPx) / 2;
    if (y < 0) y = 0;
    ApplyControlForeIcon(item, bmp, iconPx, pad, y, false);
    CDuiString tp;
    tp.Format(_T("%d,0,%d,0"), pad + iconPx + DpiScale(UiTokens::NavIconTextGap), DpiScale(UiTokens::NavTextPadR));
    item->SetAttribute(_T("textpadding"), tp.GetData());
}

void CMainWnd::RefreshTreeShellIcons()
{
    if (!m_pDirTree) return;
    std::vector<CTreeNodeUI*> stack;
    const int n = m_pDirTree->GetCount();
    for (int i = 0; i < n; ++i) {
        CControlUI* p = m_pDirTree->GetItemAt(i);
        if (p && p->GetInterface(DUI_CTR_TREENODE))
            stack.push_back(static_cast<CTreeNodeUI*>(p));
    }
    while (!stack.empty()) {
        CTreeNodeUI* node = stack.back();
        stack.pop_back();
        if (!node) continue;
        CDuiString ud = node->GetUserData();
        if (!ud.IsEmpty() && ud != CDuiString(kPendingMarker))
            ApplyTreeNodeIcon(node, ud.GetData());
        const int cc = node->GetCountChild();
        for (int i = 0; i < cc; ++i) {
            CTreeNodeUI* c = node->GetChildNode(i);
            if (c) stack.push_back(c);
        }
    }
}

void CMainWnd::StartThumbWorker()
{
    if (m_thumbThread.joinable()) return;
    m_thumbStop.store(false);
    m_thumbThread = std::thread(&CMainWnd::ThumbWorkerMain, this);
}

void CMainWnd::StopThumbWorker()
{
    m_thumbStop.store(true);
    m_thumbCv.notify_all();
    if (!m_thumbThread.joinable())
        return;
    // Extracting a video/thumbnail through the Shell can block for many seconds. Never
    // stall the shutdown on it: wait briefly, then let the worker die with the process
    // (the window object is process-lifetime, see wWinMain).
    if (::WaitForSingleObject(m_thumbThread.native_handle(), 1200) == WAIT_OBJECT_0)
        m_thumbThread.join();
    else
        m_thumbThread.detach();
}

void CMainWnd::CancelThumbJobs()
{
    m_thumbGeneration.fetch_add(1);
    std::lock_guard<std::mutex> lock(m_thumbMutex);
    m_thumbQueue.clear();
}

void CMainWnd::EnqueueThumbJob(const ThumbJob& job)
{
    {
        std::lock_guard<std::mutex> lock(m_thumbMutex);
        m_thumbQueue.push_back(job);
    }
    m_thumbCv.notify_one();
}

void CMainWnd::OnThumbReadyMessage(LPARAM lParam)
{
    auto* payload = reinterpret_cast<ThumbReadyPayload*>(lParam);
    if (!payload) return;

    const bool okGen = (payload->generation == m_thumbGeneration.load());
    CControlUI* tile = nullptr;
    if (okGen && m_pIconTiles && payload->index >= 0) {
        if (m_iconVirtMode) {
            const int local = payload->index - m_virtFirstIndex;
            if (local >= 0 && local < m_pIconTiles->GetCount())
                tile = m_pIconTiles->GetItemAt(local);
        } else if (payload->index < m_pIconTiles->GetCount()) {
            tile = m_pIconTiles->GetItemAt(payload->index);
        }
    }
    if (tile) {
        ApplyTileIconImage(tile, payload->bmpPath,
            payload->tileW, payload->tileH, payload->iconPx,
            payload->listMode, payload->tilesMode);
        tile->Invalidate();
    }
    delete payload;
}

void CMainWnd::ThumbWorkerMain(CMainWnd* self)
{
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    while (true) {
        ThumbJob job;
        {
            std::unique_lock<std::mutex> lock(self->m_thumbMutex);
            self->m_thumbCv.wait(lock, [&] {
                return self->m_thumbStop.load() || !self->m_thumbQueue.empty();
            });
            if (self->m_thumbStop.load() && self->m_thumbQueue.empty())
                break;
            if (self->m_thumbQueue.empty())
                continue;
            job = std::move(self->m_thumbQueue.front());
            self->m_thumbQueue.pop_front();
        }

        if (job.generation != self->m_thumbGeneration.load())
            continue;

        // Shell handed us an STA thread; without draining its message queue an out-of-proc
        // thumbnail handler can deadlock or call back into a queue nobody services.
        {
            MSG msg = {};
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
        std::wstring bmp = self->GetShellIconBmp(job.path, job.isDir, job.iconPx);
        if (bmp.empty())
            continue;
        if (job.generation != self->m_thumbGeneration.load())
            continue;
        if (!self->m_hWnd || !::IsWindow(self->m_hWnd))
            break;

        auto* payload = new (std::nothrow) ThumbReadyPayload();
        if (!payload) continue;
        payload->generation = job.generation;
        payload->index = job.index;
        payload->bmpPath = std::move(bmp);
        payload->iconPx = job.iconPx;
        payload->tileW = job.tileW;
        payload->tileH = job.tileH;
        payload->listMode = job.listMode;
        payload->tilesMode = job.tilesMode;

        if (!::PostMessageW(self->m_hWnd, kMsgThumbReady, 0, reinterpret_cast<LPARAM>(payload)))
            delete payload;
    }
    ::CoUninitialize();
}
