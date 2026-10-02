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

// ---- Persistent icon cache ------------------------------------------------------------
// The PNG cache survives relaunches. Invalidation is by name, not by wiping:
//  * kIconCacheVersion names the directory; a format change bumps it and the
//    background maintenance removes the old directories / legacy flat files.
//  * file / folder entries hash the source's size and last-write time into the name,
//    so an edited file never reuses a previous launch's image.
//  * items without a stable stamp (drive roots, virtual items) and preview scratch
//    images are session files ("s_<tag>_"), removed by the next launch.
// FASTFILE_ICON_CACHE_DIR redirects the root (regression tests use their own folder
// so a test run never touches the user's cache).
const wchar_t* const CMainWnd::kIconCacheVersion = L"v9";

std::wstring CMainWnd::ResolveIconCacheRoot()
{
    std::wstring root;
    wchar_t env[MAX_PATH * 2] = {};
    const DWORD n = ::GetEnvironmentVariableW(L"FASTFILE_ICON_CACHE_DIR", env, _countof(env));
    if (n > 0 && n < _countof(env)) {
        root = env;
    } else {
        wchar_t tmp[MAX_PATH] = {};
        ::GetTempPathW(MAX_PATH, tmp);
        root = tmp;
        if (!root.empty() && root.back() != L'\\') root.push_back(L'\\');
        root += L"FastFileIconCache";
    }
    while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
    return root;
}

void CMainWnd::InitIconCache()
{
    m_iconCacheRoot = ResolveIconCacheRoot();
    FILETIME now = {};
    ::GetSystemTimeAsFileTime(&now);
    m_iconCacheSessionStart = (static_cast<ULONGLONG>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    wchar_t tag[32] = {};
    swprintf_s(tag, L"%08lX%08lX", ::GetCurrentProcessId(), static_cast<unsigned long>(m_iconCacheSessionStart >> 20));
    m_iconSessionTag = tag;
    const std::wstring dir = m_iconCacheRoot + L"\\" + kIconCacheVersion;
    ::SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    m_iconCacheDir = dir + L"\\";
    // Trim off the UI thread; it only touches files of other versions, other sessions,
    // or entries older than the age cap, never this session's fresh files.
    const std::wstring root = m_iconCacheRoot;
    const ULONGLONG start = m_iconCacheSessionStart;
    std::thread([root, start]() {
        ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        MaintainIconCache(root, kIconCacheVersion, start, kIconCacheMaxAgeDays, kIconCacheMaxBytes);
    }).detach();
}

CMainWnd::IconCacheTrimResult CMainWnd::MaintainIconCache(const std::wstring& root,
    const std::wstring& version, ULONGLONG sessionStart, int maxAgeDays, ULONGLONG maxBytes)
{
    IconCacheTrimResult result;
    if (root.empty()) return result;
    auto isCacheImage = [](const wchar_t* name) {
        const wchar_t* ext = ::PathFindExtensionW(name);
        return _wcsicmp(ext, L".png") == 0 || _wcsicmp(ext, L".bmp") == 0;
    };
    auto stamp = [](const FILETIME& t) { return (static_cast<ULONGLONG>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    WIN32_FIND_DATAW fd = {};
    // Legacy flat layout (<= v8, written straight into the root) and other versions.
    HANDLE h = ::FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring full = root + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                const bool versionDir = name.size() > 1 && (name[0] == L'v' || name[0] == L'V')
                    && std::all_of(name.begin() + 1, name.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
                if (versionDir && _wcsicmp(name.c_str(), version.c_str()) != 0) {
                    WipeDirectoryFiles(full);
                    if (::RemoveDirectoryW(full.c_str())) ++result.removedVersionDirs;
                }
            } else if (isCacheImage(fd.cFileName) && ::DeleteFileW(full.c_str())) {
                ++result.removedLegacy;
            }
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    // Current version: session leftovers, age cap, then size cap (oldest first).
    const std::wstring dir = root + L"\\" + version;
    struct Entry { std::wstring path; ULONGLONG time; ULONGLONG bytes; };
    std::vector<Entry> entries;
    const ULONGLONG ageLimit = static_cast<ULONGLONG>(maxAgeDays) * 24ull * 3600ull * 10000000ull;
    h = ::FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring full = dir + L"\\" + fd.cFileName;
            const ULONGLONG written = stamp(fd.ftLastWriteTime);
            const bool session = wcsncmp(fd.cFileName, L"s_", 2) == 0;
            if (session && written < sessionStart) {
                if (::DeleteFileW(full.c_str())) ++result.removedSession;
                continue;
            }
            if (!session && maxAgeDays > 0 && sessionStart > written && sessionStart - written > ageLimit) {
                if (::DeleteFileW(full.c_str())) ++result.removedAged;
                continue;
            }
            const ULONGLONG bytes = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            entries.push_back({full, written, bytes});
            result.keptBytes += bytes;
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    if (maxBytes > 0 && result.keptBytes > maxBytes) {
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.time < b.time; });
        const ULONGLONG target = maxBytes / 4 * 3;
        for (const Entry& e : entries) {
            if (result.keptBytes <= target) break;
            if (e.time >= sessionStart) continue; // written by this launch
            if (::DeleteFileW(e.path.c_str())) { result.keptBytes -= e.bytes; ++result.removedOverCap; }
        }
    }
    return result;
}

std::wstring CMainWnd::IconCacheLeaf(const std::wstring& key, const std::wstring& source, const wchar_t* tail) const
{
    std::wstring stamped = key;
    bool stable = false;
    // Drive roots can be a different medium next launch; virtual items have no stamp.
    if (source.size() > 3 && source.compare(0, 2, L"::") != 0) {
        WIN32_FILE_ATTRIBUTE_DATA data = {};
        if (::GetFileAttributesExW(source.c_str(), GetFileExInfoStandard, &data)) {
            wchar_t buf[64] = {};
            swprintf_s(buf, L"|%08lX%08lX|%08lX%08lX", data.nFileSizeHigh, data.nFileSizeLow,
                data.ftLastWriteTime.dwHighDateTime, data.ftLastWriteTime.dwLowDateTime);
            stamped += buf;
            stable = true;
        }
    } else if (source.empty()) {
        stable = true; // stock / glyph / command art: the key fully describes the image
    }
    const unsigned long long h = static_cast<unsigned long long>(std::hash<std::wstring>{}(stamped));
    wchar_t name[128] = {};
    if (stable)
        swprintf_s(name, L"%016llX_%s", h, tail);
    else
        swprintf_s(name, L"s_%s_%016llX_%s", m_iconSessionTag.c_str(), h, tail);
    return name;
}

std::wstring CMainWnd::SessionCacheFile(const wchar_t* leaf) const
{
    return m_iconCacheDir + L"s_" + m_iconSessionTag + L"_" + leaf;
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

    wchar_t tail[32] = {};
    swprintf_s(tail, L"%s_%d.png", isDir ? L"d" : L"f", cx);
    std::wstring bmpPath = m_iconCacheDir + IconCacheLeaf(key, path, tail);
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

    wchar_t tail[32] = {};
    swprintf_s(tail, L"%s_%d.png", isDir ? L"d" : L"f", cx);
    std::wstring bmpPath = m_iconCacheDir + IconCacheLeaf(key, path, tail);

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

    wchar_t tail[32] = {};
    swprintf_s(tail, L"%s_%d_ico.png", isDir ? L"d" : L"f", cx);
    std::wstring bmpPath = m_iconCacheDir + IconCacheLeaf(key, path, tail);

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

// Localized shell display name for a path (SIGDN_NORMALDISPLAY). Known folders come back in
// the UI language - "D:\Users\...\Pictures" shows as "图片" - which is what Explorer's
// navigation pane does. Empty result means "fall back to the leaf name".
std::wstring CMainWnd::GetShellDisplayName(const std::wstring& path) const
{
    if (path.empty()) return std::wstring();
    IShellItem* psi = nullptr;
    if (FAILED(::SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&psi))) || !psi)
        return std::wstring();
    std::wstring name;
    PWSTR psz = nullptr;
    if (SUCCEEDED(psi->GetDisplayName(SIGDN_NORMALDISPLAY, &psz)) && psz) {
        name = psz;
        ::CoTaskMemFree(psz);
    }
    psi->Release();
    return name;
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

namespace {

// Windows icon fonts: Segoe Fluent Icons (Windows 11, C:\Windows\Fonts\SegoeIcons.ttf) is the
// Explorer command-bar set; Segoe MDL2 Assets (segmdl2.ttf) carries the same code points on
// Windows 10. Both put the em box at the cell origin (ascent = em, descent = 0).
int CALLBACK IconFontFound(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found)
{
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

const wchar_t* CommandIconFace()
{
    static int which = -1;
    if (which < 0) {
        which = 0;
        HDC dc = ::GetDC(nullptr);
        auto has = [&](const wchar_t* face) {
            LOGFONTW lf = {};
            lf.lfCharSet = DEFAULT_CHARSET;
            wcscpy_s(lf.lfFaceName, face);
            bool found = false;
            ::EnumFontFamiliesExW(dc, &lf, IconFontFound, reinterpret_cast<LPARAM>(&found), 0);
            return found;
        };
        if (dc) {
            if (has(L"Segoe Fluent Icons")) which = 1;
            else if (has(L"Segoe MDL2 Assets")) which = 2;
            ::ReleaseDC(nullptr, dc);
        }
    }
    return which == 1 ? L"Segoe Fluent Icons" : which == 2 ? L"Segoe MDL2 Assets" : nullptr;
}

// Rasterizes one icon-font glyph white-on-black with GDI and max-merges its coverage into
// `cov` (w x h, one byte per pixel). (x, y) is the top-left of the em box, emPx its size.
bool AddGlyphCoverage(const wchar_t* face, wchar_t glyph, int emPx, int x, int y,
                      int w, int h, std::vector<BYTE>& cov)
{
    if (!face || emPx <= 0 || w <= 0 || h <= 0) return false;
    if (cov.size() != size_t(w) * size_t(h)) cov.assign(size_t(w) * size_t(h), 0);
    HDC screen = ::GetDC(nullptr);
    if (!screen) return false;
    HDC mem = ::CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (dib && bits && mem) {
        ::ZeroMemory(bits, size_t(w) * size_t(h) * 4u);
        HGDIOBJ oldBmp = ::SelectObject(mem, dib);
        LOGFONTW lf = {};
        lf.lfHeight = -emPx;
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = ANTIALIASED_QUALITY;
        wcscpy_s(lf.lfFaceName, face);
        HFONT font = ::CreateFontIndirectW(&lf);
        HGDIOBJ oldFont = font ? ::SelectObject(mem, font) : nullptr;
        ::SetBkMode(mem, TRANSPARENT);
        ::SetTextColor(mem, RGB(255, 255, 255));
        ::SetTextAlign(mem, TA_TOP | TA_LEFT | TA_NOUPDATECP);
        ok = ::TextOutW(mem, x, y, &glyph, 1) != FALSE;
        ::GdiFlush();
        const DWORD* src = static_cast<const DWORD*>(bits);
        for (size_t i = 0, n = size_t(w) * size_t(h); i < n; ++i) {
            const DWORD p = src[i];
            const BYTE c = (std::max)({ BYTE(p & 0xFF), BYTE((p >> 8) & 0xFF), BYTE((p >> 16) & 0xFF) });
            if (c > cov[i]) cov[i] = c;
        }
        if (oldFont) ::SelectObject(mem, oldFont);
        if (font) ::DeleteObject(font);
        ::SelectObject(mem, oldBmp);
    }
    if (dib) ::DeleteObject(dib);
    if (mem) ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);
    return ok;
}

// Box-filters an s-times supersampled coverage buffer down to (w, h).
std::vector<float> DownsampleCoverage(const std::vector<BYTE>& hi, int w, int h, int s)
{
    std::vector<float> out(size_t(w) * size_t(h), 0.0f);
    if (hi.size() != size_t(w) * size_t(h) * size_t(s) * size_t(s)) return out;
    const int hw = w * s;
    const float norm = 1.0f / (255.0f * float(s * s));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned sum = 0;
            for (int j = 0; j < s; ++j) {
                const BYTE* row = &hi[size_t(y * s + j) * size_t(hw) + size_t(x * s)];
                for (int i = 0; i < s; ++i) sum += row[i];
            }
            out[size_t(y) * size_t(w) + size_t(x)] = float(sum) * norm;
        }
    return out;
}


} // namespace

bool CMainWnd::RenderGlyphToPng(wchar_t glyph, int px, COLORREF color, const std::wstring& pngPath)
{
    if (px <= 0 || pngPath.empty()) return false;
    if (!EnsureGdiplus()) return false;
    const wchar_t* face = CommandIconFace();
    if (!face) return false;
    // Rasterize the em box 4x supersampled (white on black), box-filter to the exact size and
    // rebuild straight-alpha pixels in the requested colour.
    constexpr int s = 4;
    std::vector<BYTE> hi;
    if (!AddGlyphCoverage(face, glyph, px * s, 0, 0, px * s, px * s, hi)) return false;
    const auto cov = DownsampleCoverage(hi, px, px, s);
    const Gdiplus::ARGB rgb = (Gdiplus::ARGB(GetRValue(color)) << 16)
        | (Gdiplus::ARGB(GetGValue(color)) << 8) | Gdiplus::ARGB(GetBValue(color));
    std::vector<Gdiplus::ARGB> pixels(cov.size(), 0);
    for (size_t i = 0; i < cov.size(); ++i)
        if (cov[i] > 0.0f) pixels[i] = (Gdiplus::ARGB(std::lround(cov[i] * 255.0f)) << 24) | rgb;
    return SaveArgbPng(pixels, px, px, pngPath);
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
    swprintf_s(name, L"gly_%08X_%04X_%d_v2.png", static_cast<unsigned>(h & 0xFFFFFFFFu),
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

    // Stamp with the module file when it is a real path (an updated exe changes its icon).
    wchar_t modulePath[MAX_PATH] = {};
    std::wstring source;
    if (::SearchPathW(nullptr, moduleFile, nullptr, MAX_PATH, modulePath, nullptr)) source = modulePath;
    wchar_t tail[48] = {};
    swprintf_s(tail, L"mod_%d_%d.png", index, cx);
    std::wstring bmpPath = m_iconCacheDir + IconCacheLeaf(key, source, tail);
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

namespace {

// Ids for the two-tone command-bar icons (see GetCommandIconBmp).
enum CommandIcon {
    CmdIconNew = 0, CmdIconCut, CmdIconCopy, CmdIconPaste, CmdIconRename,
    CmdIconShare, CmdIconDelete, CmdIconSort, CmdIconView, CmdIconMore, CmdIconSettings
};

// Command-bar ink is #1A1A1A. Disabled icons receive 40% overall alpha
// after rendering, so intersecting strokes do not become more opaque.
// `soft` is the lighter secondary stroke (clipboard sheet, bin slots); `ink` is used for the
// near-black "more" dots, which Explorer always draws dark; `tint` is the translucent fill
// Explorer puts behind the accent shapes (the front copy sheet, the pasted page).
struct CmdPalette {
    Gdiplus::ARGB outline;
    Gdiplus::ARGB accent;
    Gdiplus::ARGB soft;
    Gdiplus::ARGB ink;
    Gdiplus::ARGB tint;
};
const CmdPalette kCmdLit = { 0xFF1A1A1A, 0xFF1A1A1A, 0xFF1A1A1A, 0xFF1A1A1A, 0x261A1A1A };

// Icons are authored on a 20x20 grid and scaled to the requested pixel size.
void DrawCommandIcon(Gdiplus::Graphics& g, int kind, float px)
{
    using namespace Gdiplus;
    const float s = px / 20.0f;
    auto X = [s](float v) { return v * s; };
    // Fallback art only (no icon font): Explorer's measured stroke is ~1 logical px at 16px.
    const float w = (std::max)(1.0f, px / 16.0f);
    const float corner = 1.6f;                      // rounded-rect radius on the grid
    const CmdPalette pal = kCmdLit;

    auto stroke = [&](ARGB color, float x1, float y1, float x2, float y2) {
        Pen p(Color(color), w);
        p.SetStartCap(LineCapRound);
        p.SetEndCap(LineCapRound);
        g.DrawLine(&p, X(x1), X(y1), X(x2), X(y2));
    };
    auto strokeW = [&](ARGB color, float x1, float y1, float x2, float y2, float weight) {
        Pen p(Color(color), (std::max)(1.0f, w * weight));
        p.SetStartCap(LineCapRound);
        p.SetEndCap(LineCapRound);
        g.DrawLine(&p, X(x1), X(y1), X(x2), X(y2));
    };
    auto poly = [&](ARGB color, const float* xy, int count) {
        PointF pts[8] = {};
        for (int i = 0; i < count && i < 8; ++i)
            pts[i] = PointF(X(xy[i * 2]), X(xy[i * 2 + 1]));
        Pen p(Color(color), w);
        p.SetStartCap(LineCapRound);
        p.SetEndCap(LineCapRound);
        p.SetLineJoin(LineJoinRound);
        g.DrawLines(&p, pts, count);
    };
    auto tri = [&](ARGB color, float x1, float y1, float x2, float y2, float x3, float y3) {
        PointF pts[3] = { PointF(X(x1), X(y1)), PointF(X(x2), X(y2)), PointF(X(x3), X(y3)) };
        SolidBrush brush{ Color(color) };
        g.FillPolygon(&brush, pts, 3);
    };
    auto rectPath = [&](GraphicsPath& path, float l, float t, float r, float b) {
        const float d = X(corner * 2.0f);
        path.AddArc(X(l), X(t), d, d, 180, 90);
        path.AddArc(X(r) - d, X(t), d, d, 270, 90);
        path.AddArc(X(r) - d, X(b) - d, d, d, 0, 90);
        path.AddArc(X(l), X(b) - d, d, d, 90, 90);
        path.CloseFigure();
    };
    auto box = [&](ARGB color, float l, float t, float r, float b) {
        GraphicsPath path;
        rectPath(path, l, t, r, b);
        Pen p(Color(color), w);
        p.SetLineJoin(LineJoinRound);
        g.DrawPath(&p, &path);
    };
    // Explorer fills the accent sheet with a translucent tint instead of leaving it hollow.
    auto boxFill = [&](ARGB strokeColor, ARGB fillColor, float l, float t, float r, float b) {
        GraphicsPath path;
        rectPath(path, l, t, r, b);
        SolidBrush brush{ Color(fillColor) };
        g.FillPath(&brush, &path);
        Pen p(Color(strokeColor), w);
        p.SetLineJoin(LineJoinRound);
        g.DrawPath(&p, &path);
    };
    auto ring = [&](ARGB color, float cx, float cy, float radius, bool filled) {
        const float d = X(radius * 2.0f);
        const float x = X(cx) - d / 2.0f;
        const float y = X(cy) - d / 2.0f;
        if (filled) {
            SolidBrush brush{ Color(color) };
            g.FillEllipse(&brush, (INT)x, (INT)y, (INT)d, (INT)d);
        } else {
            Pen p(Color(color), w);
            g.DrawEllipse(&p, x, y, d, d);
        }
    };
    auto ringTint = [&](ARGB strokeColor, ARGB fillColor, float cx, float cy, float radius,
                        float weight = 1.0f) {
        const float d = X(radius * 2.0f);
        const float x = X(cx) - d / 2.0f;
        const float y = X(cy) - d / 2.0f;
        SolidBrush brush{ Color(fillColor) };
        g.FillEllipse(&brush, (INT)x, (INT)y, (INT)d, (INT)d);
        Pen p(Color(strokeColor), (std::max)(1.0f, w * weight));
        g.DrawEllipse(&p, x, y, d, d);
    };

    switch (kind) {
    case CmdIconNew:      // grey ring + blue plus
        ring(pal.outline, 10, 10, 7.6f, false);
        stroke(pal.accent, 10, 5.7f, 10, 14.3f);
        stroke(pal.accent, 5.7f, 10, 14.3f, 10);
        break;
    case CmdIconCut:      // grey blades + blue handles
        stroke(pal.outline, 4.6f, 3.0f, 12.0f, 12.2f);
        stroke(pal.outline, 15.4f, 3.0f, 8.0f, 12.2f);
        ringTint(pal.accent, pal.tint, 5.6f, 15.2f, 2.9f, 0.85f);
        ringTint(pal.accent, pal.tint, 14.4f, 15.2f, 2.9f, 0.85f);
        break;
    case CmdIconCopy:     // grey sheet behind, blue sheet in front
        box(pal.outline, 3.0f, 7.2f, 12.2f, 16.6f);
        boxFill(pal.accent, pal.tint, 7.8f, 3.4f, 17.0f, 12.8f);
        break;
    case CmdIconPaste:    // grey clipboard + blue sheet
        box(pal.outline, 3.4f, 4.6f, 13.6f, 18.0f);
        box(pal.outline, 6.4f, 2.6f, 10.6f, 5.4f);
        boxFill(pal.accent, pal.tint, 9.4f, 7.4f, 16.4f, 16.4f);
        break;
    case CmdIconRename:   // grey box + blue "A" and caret
        box(pal.outline, 2.4f, 4.2f, 14.6f, 15.8f);
        stroke(pal.accent, 5.5f, 13.2f, 8.2f, 6.8f);
        stroke(pal.accent, 10.9f, 13.2f, 8.2f, 6.8f);
        stroke(pal.accent, 6.6f, 10.9f, 9.8f, 10.9f);
        stroke(pal.accent, 17.4f, 2.4f, 17.4f, 17.6f);
        stroke(pal.accent, 15.9f, 2.4f, 18.9f, 2.4f);
        stroke(pal.accent, 15.9f, 17.6f, 18.9f, 17.6f);
        break;
    case CmdIconShare: {  // grey box + blue arrow leaving it
        box(pal.outline, 3.0f, 6.6f, 13.6f, 17.4f);
        const float curve[] = { 6.8f, 12.6f, 10.6f, 9.4f, 13.2f, 6.8f, 15.8f, 4.8f };
        poly(pal.accent, curve, 4);
        tri(pal.accent, 11.9f, 3.3f, 17.9f, 4.3f, 16.8f, 10.1f);   // solid arrow head
        break; }
    case CmdIconDelete:   // grey bin: straight rim, tapered body, two slots
        stroke(pal.outline, 3.4f, 6.2f, 16.6f, 6.2f);
        stroke(pal.outline, 8.1f, 6.2f, 8.1f, 4.0f);
        stroke(pal.outline, 8.1f, 4.0f, 11.9f, 4.0f);
        stroke(pal.outline, 11.9f, 4.0f, 11.9f, 6.2f);
        stroke(pal.outline, 4.6f, 6.2f, 6.1f, 17.6f);
        stroke(pal.outline, 15.4f, 6.2f, 13.9f, 17.6f);
        stroke(pal.outline, 6.1f, 17.6f, 13.9f, 17.6f);
        stroke(pal.soft, 8.6f, 9.4f, 8.6f, 14.6f);
        stroke(pal.soft, 11.4f, 9.4f, 11.4f, 14.6f);
        break;
    case CmdIconSort:     // dark grey up arrow + accent blue down arrow
        stroke(pal.outline, 6.4f, 16.4f, 6.4f, 4.6f);
        stroke(pal.outline, 3.2f, 7.9f, 6.4f, 4.3f);
        stroke(pal.outline, 9.6f, 7.9f, 6.4f, 4.3f);
        stroke(pal.accent, 13.6f, 3.6f, 13.6f, 15.4f);
        stroke(pal.accent, 10.2f, 11.8f, 13.6f, 15.8f);
        stroke(pal.accent, 17.0f, 11.8f, 13.6f, 15.8f);
        break;
    case CmdIconView:     // Explorer's 查看 glyph: four rules that thicken downwards
        strokeW(pal.outline, 3.2f, 4.6f, 16.8f, 4.6f, 0.72f);
        strokeW(pal.outline, 3.2f, 8.4f, 16.8f, 8.4f, 0.88f);
        strokeW(pal.outline, 3.2f, 12.4f, 16.8f, 12.4f, 1.02f);
        strokeW(pal.outline, 3.2f, 16.4f, 16.8f, 16.4f, 1.16f);
        break;
    case CmdIconSettings: {
        PointF gear[24];
        for(int i=0;i<24;++i) {
            const float angle=float(i*3.141592653589793/12);
            const float radius=(i%4==0 || i%4==3)?8.0f:6.4f;
            gear[i]=PointF(X(10+radius*std::cos(angle)),X(10+radius*std::sin(angle)));
        }
        Pen pen(Color(pal.ink),w);pen.SetLineJoin(LineJoinRound);g.DrawPolygon(&pen,gear,24);
        ring(pal.ink,10,10,2.6f,false);break;
    }
    default:                        // more: three ink dots
        ring(pal.ink, 4.4f, 10, 1.5f, true);
        ring(pal.ink, 10, 10, 1.5f, true);
        ring(pal.ink, 15.6f, 10, 1.5f, true);
        break;
    }
}

// ---- Two-tone glyph command icons (cmd-bar alignment pass) ------------------------------
// The grey layer is the Segoe Fluent Icons glyph itself (the same art Explorer's command bar
// uses). The accent part of each glyph is selected with a clip region on the glyph's 16-unit
// design grid and drawn in the Windows accent blue; for glyphs whose accent touches the grey
// strokes (Cut), the grey layer is additionally cut back by one design unit around the
// region so a ~1 logical px gap separates the two layers, like Explorer.
struct CmdShape {
    char kind;      // 'c' circle (cx, cy, r) | 'r' rect (l, t, r, b) | 'p' polygon (n points)
    int n;
    float v[16];
};
struct CmdGlyphSpec {
    int kind;
    wchar_t glyph;
    bool knockout;  // cut the grey layer back around the accent region
    int count;
    CmdShape shapes[4];
};
const CmdGlyphSpec kCmdGlyphs[] = {
    { CmdIconNew,    0xECC8, false, 1, { { 'c', 3, { 7.5f, 7.5f, 4.6f } } } },
    { CmdIconCut,    0xE8C6, true,  2, { { 'c', 3, { 4.0f, 13.0f, 3.05f } }, { 'c', 3, { 12.0f, 13.0f, 3.05f } } } },
    { CmdIconCopy,   0xE8C8, true,  1, { { 'r', 4, { 5.9f, 0.9f, 15.1f, 12.1f } } } },
    { CmdIconPaste,  0xE77F, true,  1, { { 'r', 4, { 6.9f, 4.9f, 15.1f, 16.0f } } } },
    { CmdIconRename, 0xE8AC, false, 4, { { 'r', 4, { 2.1f, 3.9f, 8.9f, 11.1f } },
                                         { 'r', 4, { 9.55f, 0.0f, 11.45f, 16.0f } },
                                         { 'r', 4, { 7.8f, 0.0f, 13.2f, 1.3f } },
                                         { 'r', 4, { 7.8f, 14.7f, 13.2f, 16.0f } } } },
    { CmdIconShare,  0xE72D, true,  1, { { 'p', 8, { 3.0f, 11.6f, 3.0f, 7.5f, 7.2f, 4.2f, 9.6f, 3.6f,
                                                     9.6f, 0.0f, 16.0f, 0.0f, 16.0f, 10.3f, 9.0f, 11.6f } } } },
    { CmdIconDelete, 0xE74D, false, 0, {} },
    { CmdIconSort,   0xE8CB, false, 1, { { 'p', 6, { 9.3f, 0.0f, 16.0f, 0.0f, 16.0f, 16.0f, 6.8f, 16.0f,
                                                     6.8f, 8.2f, 9.3f, 8.2f } } } },
    { CmdIconSettings, 0xE713, false, 0, {} },
};

const CmdGlyphSpec* FindCmdGlyph(int kind)
{
    for (const auto& spec : kCmdGlyphs)
        if (spec.kind == kind) return &spec;
    return nullptr;
}

// Fills (or, with grow > 0, fills and dilates) a glyph's accent shapes into an 8-bit mask.
void FillCmdShapes(const CmdGlyphSpec& spec, float ox, float oy, float unit, float grow,
                   int w, int h, std::vector<BYTE>& mask)
{
    using namespace Gdiplus;
    mask.assign(size_t(w) * size_t(h), 0);
    if (spec.count <= 0) return;
    Bitmap bmp(w, h, PixelFormat32bppARGB);
    if (bmp.GetLastStatus() != Ok) return;
    {
        Graphics g(&bmp);
        g.Clear(Color(0, 0, 0, 0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        SolidBrush white(Color(255, 255, 255, 255));
        Pen widen(Color(255, 255, 255, 255), 2.0f * grow * unit);
        widen.SetLineJoin(LineJoinRound);
        auto X = [&](float v) { return ox + v * unit; };
        auto Y = [&](float v) { return oy + v * unit; };
        for (int i = 0; i < spec.count; ++i) {
            const CmdShape& s = spec.shapes[i];
            GraphicsPath path;
            if (s.kind == 'c') {
                const float r = s.v[2];
                path.AddEllipse(X(s.v[0] - r), Y(s.v[1] - r), 2.0f * r * unit, 2.0f * r * unit);
            } else if (s.kind == 'r') {
                path.AddRectangle(RectF(X(s.v[0]), Y(s.v[1]), (s.v[2] - s.v[0]) * unit, (s.v[3] - s.v[1]) * unit));
            } else {
                PointF pts[8];
                const int n = (std::min)(s.n, 8);
                for (int k = 0; k < n; ++k) pts[k] = PointF(X(s.v[k * 2]), Y(s.v[k * 2 + 1]));
                path.AddPolygon(pts, n);
            }
            g.FillPath(&white, &path);
            if (grow > 0.0f) g.DrawPath(&widen, &path);
        }
    }
    BitmapData data{};
    Rect bounds(0, 0, w, h);
    if (bmp.LockBits(&bounds, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok) return;
    for (int y = 0; y < h; ++y) {
        const BYTE* row = static_cast<const BYTE*>(data.Scan0) + ptrdiff_t(y) * data.Stride;
        for (int x = 0; x < w; ++x) mask[size_t(y) * size_t(w) + size_t(x)] = row[x * 4 + 3];
    }
    bmp.UnlockBits(&data);
}

// Vector coverage (white = ink) for icons without a matching glyph: the 查看 icon (two
// rounded squares + two rules, Explorer's view glyph) and the GDI+ fallback art when no icon
// font is installed. Stroke = 1 design unit = em / 16 (~1 logical px at 16).
void AddVectorCoverage(int kind, float ox, float oy, float em, int w, int h, std::vector<BYTE>& cov)
{
    using namespace Gdiplus;
    if (cov.size() != size_t(w) * size_t(h)) cov.assign(size_t(w) * size_t(h), 0);
    Bitmap bmp(w, h, PixelFormat32bppARGB);
    if (bmp.GetLastStatus() != Ok) return;
    {
        Graphics g(&bmp);
        g.Clear(Color(0, 0, 0, 0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        const float u = em / 16.0f;
        if (kind == CmdIconMore) {
            // Explorer's 更多: three 3.3 logical px dots, 6.25 apart, centred in the 16px box.
            SolidBrush brush(Color(255, 255, 255, 255));
            const float d = 3.3f * u;
            for (float cx : { 8.0f - 6.25f, 8.0f, 8.0f + 6.25f })
                g.FillEllipse(&brush, ox + cx * u - d / 2.0f, oy + 8.0f * u - d / 2.0f, d, d);
        } else if (kind == CmdIconView) {
            Pen pen(Color(255, 255, 255, 255), u);
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            pen.SetLineJoin(LineJoinRound);
            auto square = [&](float l, float t, float side) {
                GraphicsPath path;
                const float d = 2.0f * u;
                const float x = ox + l * u, y = oy + t * u, s = side * u;
                path.AddArc(x, y, d, d, 180, 90);
                path.AddArc(x + s - d, y, d, d, 270, 90);
                path.AddArc(x + s - d, y + s - d, d, d, 0, 90);
                path.AddArc(x, y + s - d, d, d, 90, 90);
                path.CloseFigure();
                g.DrawPath(&pen, &path);
            };
            square(1.5f, 2.5f, 4.5f);
            square(1.5f, 9.0f, 4.5f);
            g.DrawLine(&pen, ox + 9.0f * u, oy + 4.75f * u, ox + 15.0f * u, oy + 4.75f * u);
            g.DrawLine(&pen, ox + 9.0f * u, oy + 11.25f * u, ox + 15.0f * u, oy + 11.25f * u);
        } else {
            GraphicsContainer state = g.BeginContainer();
            g.TranslateTransform(ox, oy);
            DrawCommandIcon(g, kind, em);
            g.EndContainer(state);
        }
    }
    BitmapData data{};
    Rect bounds(0, 0, w, h);
    if (bmp.LockBits(&bounds, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok) return;
    for (int y = 0; y < h; ++y) {
        const BYTE* row = static_cast<const BYTE*>(data.Scan0) + ptrdiff_t(y) * data.Stride;
        for (int x = 0; x < w; ++x) {
            BYTE& c = cov[size_t(y) * size_t(w) + size_t(x)];
            c = (std::max)(c, row[x * 4 + 3]);
        }
    }
    bmp.UnlockBits(&data);
}

// Renders a command-bar bitmap of w x h physical px: the two-tone icon at (iconX, iconY) and,
// when chevEm > 0, Explorer's small E70D chevron with its ink box starting at chevX and
// centred on chevCY. Everything is drawn 4x supersampled and box-filtered, then composed as
// grey / accent / chevron layers. Disabled icons are the whole icon at 36% alpha (#C2C2C2 /
// #A3CEEF on white); the chevron switches to its own disabled grey.
std::vector<Gdiplus::ARGB> RenderCommandCanvas(int kind, int w, int h, int iconX, int iconY, int iconPx,
                                               float chevX, float chevCY, float chevEm, bool dim)
{
    constexpr int s = 4;
    const int hw = w * s, hh = h * s;
    const wchar_t* face = CommandIconFace();
    const CmdGlyphSpec* spec = FindCmdGlyph(kind);
    std::vector<BYTE> ink(size_t(hw) * size_t(hh), 0), accent, knock, chev;
    if (spec && face)
        AddGlyphCoverage(face, spec->glyph, iconPx * s, iconX * s, iconY * s, hw, hh, ink);
    else
        AddVectorCoverage(kind, float(iconX * s), float(iconY * s), float(iconPx * s), hw, hh, ink);
    const float unit = float(iconPx * s) / 16.0f;
    if (spec && face && spec->count > 0) {
        FillCmdShapes(*spec, float(iconX * s), float(iconY * s), unit, 0.0f, hw, hh, accent);
        if (spec->knockout)
            FillCmdShapes(*spec, float(iconX * s), float(iconY * s), unit, 1.0f, hw, hh, knock);
    }
    std::vector<BYTE> grey(ink.size(), 0), blue(ink.size(), 0);
    for (size_t i = 0; i < ink.size(); ++i) {
        const unsigned a = accent.empty() ? 0u : accent[i];
        const unsigned k = (std::max)(a, knock.empty() ? 0u : unsigned(knock[i]));
        blue[i] = BYTE(unsigned(ink[i]) * a / 255u);
        grey[i] = BYTE(unsigned(ink[i]) * (255u - k) / 255u);
    }
    if (chevEm > 0.0f) {
        const int emHi = (std::max)(4, int(std::lround(chevEm * s)));
        // E972 (ChevronDownSmall) ink box: x 341..1707, y 682..1451 of a 2048 em (y down).
        const int ex = int(std::lround(chevX * s - (341.0f / 2048.0f) * emHi));
        const int ey = int(std::lround(chevCY * s - (1066.5f / 2048.0f) * emHi));
        if (face) {
            AddGlyphCoverage(face, UiTokens::GlyphChevronDown, emHi, ex, ey, hw, hh, chev);
        } else {
            using namespace Gdiplus;
            chev.assign(ink.size(), 0);
            Bitmap bmp(hw, hh, PixelFormat32bppARGB);
            {
                Graphics g(&bmp);
                g.Clear(Color(0, 0, 0, 0));
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                Pen pen(Color(255, 255, 255, 255), emHi / 16.0f);
                pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
                const PointF pts[3] = { PointF(ex + 0.16f * emHi, ey + 0.34f * emHi),
                    PointF(ex + 0.5f * emHi, ey + 0.69f * emHi), PointF(ex + 0.84f * emHi, ey + 0.34f * emHi) };
                g.DrawLines(&pen, pts, 3);
            }
            BitmapData data{}; Rect bounds(0, 0, hw, hh);
            if (bmp.LockBits(&bounds, ImageLockModeRead, PixelFormat32bppARGB, &data) == Ok) {
                for (int y = 0; y < hh; ++y) {
                    const BYTE* row = static_cast<const BYTE*>(data.Scan0) + ptrdiff_t(y) * data.Stride;
                    for (int x = 0; x < hw; ++x) chev[size_t(y) * size_t(hw) + size_t(x)] = row[x * 4 + 3];
                }
                bmp.UnlockBits(&data);
            }
        }
    }
    const auto g = DownsampleCoverage(grey, w, h, s);
    const auto b = DownsampleCoverage(blue, w, h, s);
    const auto c = chev.empty() ? std::vector<float>(size_t(w) * size_t(h), 0.0f) : DownsampleCoverage(chev, w, h, s);
    const Gdiplus::ARGB greyInk = kind == CmdIconMore ? UiTokens::ArgbCmdMore : UiTokens::ArgbCmdIcon;
    const Gdiplus::ARGB blueInk = UiTokens::ArgbCmdAccent;
    const Gdiplus::ARGB chevInk = dim ? UiTokens::ArgbCmdChevronDisabled : UiTokens::ArgbCmdChevron;
    const float iconAlpha = dim ? float(UiTokens::CmdDisabledAlpha) / 255.0f : 1.0f;
    auto ch = [](Gdiplus::ARGB c, int shift) { return float((c >> shift) & 0xFF); };
    std::vector<Gdiplus::ARGB> out(size_t(w) * size_t(h), 0);
    for (size_t i = 0; i < out.size(); ++i) {
        const float ag = g[i] * iconAlpha, ab = b[i] * iconAlpha, ac = c[i];
        const float sum = ag + ab + ac;
        if (sum <= 0.0f) continue;
        const float a = (std::min)(1.0f, sum);
        auto mix = [&](int shift) {
            return Gdiplus::ARGB(std::lround((ch(greyInk, shift) * ag + ch(blueInk, shift) * ab + ch(chevInk, shift) * ac) / sum)) & 0xFF;
        };
        out[i] = (Gdiplus::ARGB(std::lround(a * 255.0f)) << 24) | (mix(16) << 16) | (mix(8) << 8) | mix(0);
    }
    return out;
}

} // namespace

bool CMainWnd::SaveArgbPng(const std::vector<DWORD>& pixels, int w, int h, const std::wstring& path)
{
    using namespace Gdiplus;
    if (w <= 0 || h <= 0 || pixels.size() != size_t(w) * size_t(h) || path.empty()) return false;
    Bitmap bmp(w, h, PixelFormat32bppARGB);
    if (bmp.GetLastStatus() != Ok) return false;
    BitmapData data{};
    Rect bounds(0, 0, w, h);
    if (bmp.LockBits(&bounds, ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok) return false;
    for (int y = 0; y < h; ++y)
        memcpy(static_cast<BYTE*>(data.Scan0) + ptrdiff_t(y) * data.Stride, &pixels[size_t(y) * size_t(w)], size_t(w) * 4u);
    bmp.UnlockBits(&data);
    CLSID png = {};
    return GetPngEncoderClsid(&png) && bmp.Save(path.c_str(), &png, nullptr) == Ok;
}

std::wstring CMainWnd::GetCommandCanvasBmp(int kind, int w, int h, int iconX, int iconY, int iconPx,
    float chevX, float chevCY, float chevEm, bool dim)
{
    if (w < 8 || h < 8 || w > 512 || h > 256 || iconPx < 8 || iconPx > 128) return {};
    const wchar_t* face = CommandIconFace();
    wchar_t keybuf[160] = {};
    swprintf_s(keybuf, L"cmdc-v7:%d@%dx%d:%d,%d,%d:%.2f,%.2f,%.2f%s:%s", kind, w, h, iconX, iconY, iconPx,
        chevX, chevCY, chevEm, dim ? L"#dim" : L"", face ? face : L"vector");
    const std::wstring key = keybuf;
    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }
    if (m_iconCacheDir.empty() || !EnsureGdiplus())
        return {};
    wchar_t name[96] = {};
    swprintf_s(name, L"cmdc_%08X_%d_%dx%d_%d_v7.png",
        static_cast<unsigned>(std::hash<std::wstring>{}(key) & 0xFFFFFFFFu), kind, w, h, dim ? 1 : 0);
    const std::wstring pngPath = m_iconCacheDir + name;
    if (!::PathFileExistsW(pngPath.c_str())) {
        const auto pixels = RenderCommandCanvas(kind, w, h, iconX, iconY, iconPx, chevX, chevCY, chevEm, dim);
        if (!SaveArgbPng(pixels, w, h, pngPath)) return {};
    }
    std::lock_guard<std::mutex> lock(m_iconCacheMutex);
    m_iconCache[key] = pngPath;
    return pngPath;
}

std::wstring CMainWnd::GetCommandIconBmp(int kind, int px, bool dim)
{
    if (px < 8) px = 8;
    if (px > 128) px = 128;
    return GetCommandCanvasBmp(kind, px, px, 0, 0, px, 0.0f, 0.0f, 0.0f, dim);
}

// Places a command-bar bitmap on a button. Icon-only buttons centre the 16px icon (1 logical
// px below centre, like Explorer). Label buttons get one bitmap spanning the whole button:
// icon 12 from the left edge, label 8 after the icon (DuiLib text), and the small E70D
// chevron 12 from the right edge. While the button is disabled the dimmed variant is used.
void CMainWnd::ApplyCommandIcon(CControlUI* c, int kind, bool withLabel)
{
    if (!c) return;
    const int px = DpiScale(UiTokens::ToolbarGlyphPx);
    const bool dim = !c->IsEnabled();
    int bw = c->GetFixedWidth();
    int bh = c->GetFixedHeight();
    if (bw <= 0) bw = DpiScale(withLabel ? UiTokens::ToolbarTextBtnMinW : UiTokens::ToolbarBtnW);
    if (bh <= 0) bh = DpiScale(UiTokens::CmdBtnH);
    const int drop = DpiScale(UiTokens::ToolbarIconDropY);
    const int iconY = (std::max)(0, (bh - px) / 2 + drop);
    if (withLabel) {
        const int pad = DpiScale(UiTokens::ToolbarIconPad);
        const float chevEm = DpiScaleF(static_cast<float>(UiTokens::ToolbarChevronEm));
        const float chevX = static_cast<float>(bw - pad) - chevEm * (1366.0f / 2048.0f);
        const float chevCY = static_cast<float>(iconY) + px * 0.5f;
        const std::wstring bmp = GetCommandCanvasBmp(kind, bw, bh, pad, iconY, px, chevX, chevCY, chevEm, dim);
        if (bmp.empty()) return;
        CDuiString img;
        img.Format(_T("file='%s' dest='0,0,%d,%d'"), bmp.c_str(), bw, bh);
        c->SetAttribute(_T("foreimage"), img.GetData());
        c->SetAttribute(_T("hotforeimage"), img.GetData());
        CDuiString tp;
        tp.Format(_T("%d,%d,%d,0"), pad + px + DpiScale(UiTokens::ToolbarIconLabelGap), 2 * drop,
            DpiScale(UiTokens::ToolbarChevronPad));
        c->SetAttribute(_T("textpadding"), tp.GetData());
    } else {
        const std::wstring bmp = GetCommandIconBmp(kind, px, dim);
        if (bmp.empty()) return;
        ApplyControlForeIcon(c, bmp, px, (std::max)(0, (bw - px) / 2), iconY, false);
        c->SetAttribute(_T("textpadding"), _T("0,0,0,0"));
    }
    c->Invalidate();
}

// Address-row navigation glyphs (Segoe Fluent E72B / E72A / E74A / E72C) at 12 logical px,
// #1A1A1A, or #A2A2A0 while the button is disabled (no history).
void CMainWnd::ApplyNavButtonIcon(CControlUI* c, wchar_t glyph)
{
    if (!c) return;
    const int px = DpiScale(UiTokens::NavGlyphPx);
    const unsigned argb = c->IsEnabled() ? UiTokens::ArgbNavGlyph : UiTokens::ArgbNavGlyphDisabled;
    const std::wstring bmp = GetGlyphIconBmp(glyph, px,
        RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF));
    if (bmp.empty()) return;
    int bw = c->GetFixedWidth();
    int bh = c->GetFixedHeight();
    if (bw <= 0) bw = DpiScale(UiTokens::ToolbarNavBtnW);
    if (bh <= 0) bh = DpiScale(UiTokens::FieldH);
    ApplyControlForeIcon(c, bmp, px, (bw - px) / 2, (bh - px) / 2, false);
    c->SetAttribute(_T("textpadding"), _T("0,0,0,0"));
    c->Invalidate();
}

// Explorer-style command bar: 剪切 / 复制 / 粘贴 / 重命名 / 共享 / 删除 are enabled only when
// the action applies (a selection, exactly one item for 重命名, clipboard content for 粘贴)
// and are drawn dimmed while they are not. 新建 / 排序 / 查看 / 更多 stay available.
void CMainWnd::UpdateCommandBarState()
{
    std::vector<ClipboardItem> sel;
    CollectSelectedItems(sel);
    const bool hasSel = !sel.empty();
    const bool single = sel.size() == 1;

    auto state = [&](LPCTSTR name, int kind, bool enabled) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) return;
        c->SetEnabled(enabled);
        ApplyCommandIcon(c, kind, false);
    };
    state(_T("btn_cut"), CmdIconCut, hasSel);
    state(_T("btn_copy"), CmdIconCopy, hasSel);
    state(_T("btn_paste"), CmdIconPaste, IsClipboardFormatAvailable(CF_HDROP));
    state(_T("btn_rename"), CmdIconRename, single);
    state(_T("btn_share"), CmdIconShare, hasSel);
    state(_T("btn_delete"), CmdIconDelete, hasSel);
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

    // Caption buttons (min / max / restore / close) live in the tab row and report the
    // non-client hit codes, so they must look like Explorer's: Segoe MDL2 "chrome" glyphs at
    // the title-bar size instead of the UI font's fallback "− □ ×" characters.
    auto applyCaptionGlyph = [&](LPCTSTR name, wchar_t glyph) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) return;
        wchar_t text[2] = { glyph, L'\0' };
        c->SetAttribute(_T("font"), _T("8"));
        c->SetAttribute(_T("textpadding"), _T("0,0,0,0"));
        c->SetText(text);
        c->Invalidate();
    };
    applyCaptionGlyph(_T("minbtn"), 0xE921);      // ChromeMinimize
    applyCaptionGlyph(_T("maxbtn"), 0xE922);      // ChromeMaximize
    applyCaptionGlyph(_T("restorebtn"), 0xE923);  // ChromeRestore
    applyCaptionGlyph(_T("closebtn"), 0xE8BB);    // ChromeClose

    // Command bar (新建 / 剪切 / … / 更多): two-tone line icons - a light grey outline with a
    // light blue accent - drawn by GetCommandIconBmp so the bar matches the Explorer command
    // bar. ApplyCommandIcon also picks the dimmed variant while a button is disabled.
    auto applyCmdIcon = [&](LPCTSTR name, int kind, bool withLabel) {
        ApplyCommandIcon(m_PaintManager.FindControl(name), kind, withLabel);
    };
    // Address-row navigation: Explorer's Segoe Fluent glyphs as 12px bitmaps, dark while
    // enabled and #A2A2A0 while disabled (UpdateNavButtons re-applies them on history changes).
    ApplyNavButtonIcon(m_PaintManager.FindControl(_T("btn_back")), UiTokens::GlyphNavBack);
    ApplyNavButtonIcon(m_PaintManager.FindControl(_T("btn_forward")), UiTokens::GlyphNavForward);
    ApplyNavButtonIcon(m_PaintManager.FindControl(_T("btn_up")), UiTokens::GlyphNavUp);
    ApplyNavButtonIcon(m_PaintManager.FindControl(_T("btn_refresh")), UiTokens::GlyphNavRefresh);
    // Search box magnifier (E721) right-aligned inside the box.
    if (CControlUI* glyph = m_PaintManager.FindControl(_T("search_glyph"))) {
        const int px = DpiScale(UiTokens::SearchGlyphPx);
        const unsigned argb = UiTokens::ArgbSearchGlyph;
        const std::wstring bmp = GetGlyphIconBmp(UiTokens::GlyphSearch, px,
            RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF));
        if (!bmp.empty()) {
            CDuiString img;
            img.Format(_T("file='%s' dest='0,0,%d,%d'"), bmp.c_str(), px, px);
            glyph->SetAttribute(_T("bkimage"), img.GetData());
        }
    }
    applyFluent(_T("btn_toggle_preview"), 0xE7F4);
    applyFluent(_T("btn_newfolder"), 0xE710);

    applyCmdIcon(_T("btn_new"), CmdIconNew, true);
    applyCmdIcon(_T("btn_cut"), CmdIconCut, false);
    applyCmdIcon(_T("btn_copy"), CmdIconCopy, false);
    applyCmdIcon(_T("btn_paste"), CmdIconPaste, false);
    applyCmdIcon(_T("btn_rename"), CmdIconRename, false);
    applyCmdIcon(_T("btn_share"), CmdIconShare, false);
    applyCmdIcon(_T("btn_delete"), CmdIconDelete, false);
    applyCmdIcon(_T("btn_sort"), CmdIconSort, true);
    applyCmdIcon(_T("btn_view_menu"), CmdIconView, true);
    applyCmdIcon(_T("btn_more"), CmdIconMore, false);
    applyCmdIcon(_T("btn_settings"), CmdIconSettings, false);

    // Keep hidden legacy view buttons iconized for UpdateViewModeButtons
    applyBtn(_T("btn_view_xlarge"), GetModuleIconBmp(L"shell32.dll", 257, iconPx), true);
    applyBtn(_T("btn_view_large"), GetModuleIconBmp(L"shell32.dll", 257, iconPx), true);
    applyBtn(_T("btn_view_medium"), GetStockIconBmp(SIID_IMAGEFILES, iconPx), true);
    applyBtn(_T("btn_view_list"), GetModuleIconBmp(L"shell32.dll", 253, iconPx), true);
    applyBtn(_T("btn_view_details"), GetModuleIconBmp(L"shell32.dll", 253, iconPx), true);
    applyBtn(_T("btn_view_tiles"), GetStockIconBmp(SIID_STACK, iconPx), true);

    // Quick-access row icons are applied in RebuildLeftQuickRows (the rows are built at
    // runtime so the user can reorder them).
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
