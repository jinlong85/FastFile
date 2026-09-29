// FastFile - preview pane (image, text, video metadata)
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

namespace {

bool ReadPropertyUInt64(IPropertyStore* store, REFPROPERTYKEY key, ULONGLONG& value)
{
    value = 0;
    if (!store) return false;
    PROPVARIANT pv;
    ::PropVariantInit(&pv);
    const HRESULT hr = store->GetValue(key, &pv);
    bool ok = false;
    if (SUCCEEDED(hr)) {
        switch (pv.vt) {
        case VT_UI8: value = pv.uhVal.QuadPart; ok = true; break;
        case VT_UI4: value = pv.ulVal; ok = true; break;
        case VT_I8:  value = static_cast<ULONGLONG>(pv.hVal.QuadPart); ok = pv.hVal.QuadPart >= 0; break;
        case VT_I4:  value = static_cast<ULONGLONG>(pv.lVal); ok = pv.lVal >= 0; break;
        default: break;
        }
    }
    ::PropVariantClear(&pv);
    return ok;
}

struct VideoPropertyInfo {
    ULONGLONG duration100ns = 0;
    ULONGLONG width = 0;
    ULONGLONG height = 0;
    ULONGLONG frameRateMilli = 0;
    ULONGLONG audioBitRate = 0;
};

VideoPropertyInfo ReadVideoProperties(const std::wstring& path)
{
    VideoPropertyInfo info;
    IPropertyStore* store = nullptr;
    if (FAILED(::SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_DEFAULT,
            IID_PPV_ARGS(&store))) || !store)
        return info;
    ReadPropertyUInt64(store, PKEY_Media_Duration, info.duration100ns);
    ReadPropertyUInt64(store, PKEY_Video_FrameWidth, info.width);
    ReadPropertyUInt64(store, PKEY_Video_FrameHeight, info.height);
    ReadPropertyUInt64(store, PKEY_Video_FrameRate, info.frameRateMilli);
    ReadPropertyUInt64(store, PKEY_Audio_EncodingBitrate, info.audioBitRate);
    store->Release();
    return info;
}

std::wstring FormatMediaDuration(ULONGLONG duration100ns)
{
    if (duration100ns == 0) return L"";
    const ULONGLONG seconds = duration100ns / 10000000ULL;
    wchar_t buf[48] = {};
    swprintf_s(buf, L"%02llu:%02llu:%02llu", seconds / 3600ULL,
        (seconds / 60ULL) % 60ULL, seconds % 60ULL);
    return buf;
}

std::wstring FormatBitRate(ULONGLONG bitsPerSecond)
{
    if (bitsPerSecond == 0) return L"";
    wchar_t buf[48] = {};
    if (bitsPerSecond >= 1000000ULL)
        swprintf_s(buf, L"%.2f Mbps", static_cast<double>(bitsPerSecond) / 1000000.0);
    else
        swprintf_s(buf, L"%llu kbps", bitsPerSecond / 1000ULL);
    return buf;
}

} // namespace

// ===== ABDE extensions (A drop->bg copy, B preview, D polish, E virtualize) =====

bool CMainWnd::IsTextExtension(const std::wstring& name)
{
    const wchar_t* ext = PathFindExtensionW(name.c_str());
    if (!ext || !*ext) return false;
    static const wchar_t* kExts[] = {
        L".txt", L".log", L".md", L".csv", L".json", L".xml", L".ini", L".cfg",
        L".c", L".cpp", L".h", L".hpp", L".cs", L".py", L".js", L".ts", L".html",
        L".htm", L".css", L".bat", L".cmd", L".ps1", L".yml", L".yaml", L".toml",
        L".cmake", L".gitignore", L".dockerignore", L".sql", L".rs", L".go", L".java"
    };
    for (auto e : kExts) {
        if (_wcsicmp(ext, e) == 0) return true;
    }
    return false;
}

void CMainWnd::SetPreviewVisible(bool visible)
{
    m_previewVisible = visible;
    // The pane is persistent while enabled. Empty selection shows the current
    // folder summary rather than collapsing the whole layout.
    if (m_pPreviewPane)
        m_pPreviewPane->SetVisible(visible);
    if (m_pBtnTogglePreview) {
        if (visible) {
            m_pBtnTogglePreview->SetAttribute(_T("bkcolor"), UiTokens::ColorAccentSoft);
            m_pBtnTogglePreview->SetAttribute(_T("textcolor"), UiTokens::ColorAccentText);
        } else {
            m_pBtnTogglePreview->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
            m_pBtnTogglePreview->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        }
    }
    if (visible)
        UpdatePreviewForSelection();
}

void CMainWnd::ShowPreviewDetails(bool showMeta, bool showImage, bool showActions)
{
    for (LPCTSTR rowName : {
        _T("preview_row_type"), _T("preview_row_size"),
        _T("preview_row_mtime"), _T("preview_row_ctime")
    }) {
        if (CControlUI* row = m_PaintManager.FindControl(rowName))
            row->SetVisible(showMeta);
    }
    if (CControlUI* row = m_PaintManager.FindControl(_T("preview_row_location")))
        row->SetVisible(showMeta && m_pPreviewLocation && !m_pPreviewLocation->GetText().IsEmpty());
    const struct { LPCTSTR row; CLabelUI* value; } optionalRows[] = {
        { _T("preview_row_dimensions"), m_pPreviewDimensions },
        { _T("preview_row_duration"), m_pPreviewDuration },
        { _T("preview_row_framerate"), m_pPreviewFrameRate },
        { _T("preview_row_bitrate"), m_pPreviewBitRate },
        { _T("preview_row_totalbitrate"), m_pPreviewTotalBitRate },
    };
    for (const auto& item : optionalRows) {
        if (CControlUI* row = m_PaintManager.FindControl(item.row))
            row->SetVisible(showMeta && item.value && !item.value->GetText().IsEmpty());
    }
    if (CControlUI* gap = m_PaintManager.FindControl(_T("preview_gap_title")))
        gap->SetVisible(showImage);
    if (m_pPreviewImage)
        m_pPreviewImage->SetVisible(showImage);
    // The preview pane no longer offers a "共享" action (it duplicated the command bar and
    // added nothing): the button and its spacer stay hidden no matter what callers pass.
    (void)showActions;
    if (CControlUI* gap = m_PaintManager.FindControl(_T("preview_gap_action")))
        gap->SetVisible(false);
    if (CControlUI* share = m_PaintManager.FindControl(_T("btn_preview_share")))
        share->SetVisible(false);
    if (CControlUI* gap = m_PaintManager.FindControl(_T("preview_gap_image")))
        gap->SetVisible(showMeta);
    if (CControlUI* title = m_PaintManager.FindControl(_T("preview_details_title")))
        title->SetVisible(showMeta);
    // Hairline above "详细信息": gives the metadata block a clear start instead of running
    // straight on from the preview title.
    if (CControlUI* sep = m_PaintManager.FindControl(_T("preview_sep_details")))
        sep->SetVisible(showMeta);
    if (CControlUI* gap = m_PaintManager.FindControl(_T("preview_gap_text")))
        gap->SetVisible(false);
    if (m_pPreviewPane)
        m_pPreviewPane->NeedUpdate();
}

void CMainWnd::ClearPreviewMeta()
{
    SetPreviewMeta(L"", L"", L"", L"");
    ClearPreviewExtraMeta();
}

void CMainWnd::SetPreviewMeta(const std::wstring& typeName,
    const std::wstring& sizeText,
    const std::wstring& mtimeText,
    const std::wstring& ctimeText)
{
    if (m_pPreviewType) m_pPreviewType->SetText(typeName.c_str());
    if (m_pPreviewSize) m_pPreviewSize->SetText(sizeText.c_str());
    if (m_pPreviewMTime) m_pPreviewMTime->SetText(mtimeText.c_str());
    if (m_pPreviewCTime) m_pPreviewCTime->SetText(ctimeText.c_str());
}

void CMainWnd::ClearPreviewExtraMeta()
{
    SetPreviewExtraMeta(L"", L"", L"", L"", L"", L"");
}

void CMainWnd::SetPreviewExtraMeta(const std::wstring& location,
    const std::wstring& dimensions,
    const std::wstring& duration,
    const std::wstring& frameRate,
    const std::wstring& bitRate,
    const std::wstring& totalBitRate)
{
    if (m_pPreviewLocation) m_pPreviewLocation->SetText(location.c_str());
    if (m_pPreviewDimensions) m_pPreviewDimensions->SetText(dimensions.c_str());
    if (m_pPreviewDuration) m_pPreviewDuration->SetText(duration.c_str());
    if (m_pPreviewFrameRate) m_pPreviewFrameRate->SetText(frameRate.c_str());
    if (m_pPreviewBitRate) m_pPreviewBitRate->SetText(bitRate.c_str());
    if (m_pPreviewTotalBitRate) m_pPreviewTotalBitRate->SetText(totalBitRate.c_str());
}

std::wstring CMainWnd::FormatFileTimeLocal(const FILETIME& ft)
{
    if (ft.dwHighDateTime == 0 && ft.dwLowDateTime == 0)
        return L"—";
    FILETIME local = {};
    if (!::FileTimeToLocalFileTime(&ft, &local))
        return L"—";
    SYSTEMTIME st = {};
    if (!::FileTimeToSystemTime(&local, &st))
        return L"—";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04u/%02u/%02u %02u:%02u",
        (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
        (unsigned)st.wHour, (unsigned)st.wMinute);
    return buf;
}

// Type name for a folder/file as Explorer would show it ("文件夹" / "光盘映像文件" /
// "JPG 文件" ...). Cached per extension: the tile view asks once per item and
// SHGetFileInfo would otherwise hit the shell's type registry every time.
// SHGFI_USEFILEATTRIBUTES means we only need the extension — no file access at all.
std::wstring CMainWnd::QueryShellTypeNameCached(const std::wstring& path, bool isDir)
{
    if (isDir)
        return IsThisPcPath(m_currentPath) ? L"驱动器" : L"文件夹";

    const wchar_t* extPtr = ::PathFindExtensionW(path.c_str());
    std::wstring key;
    if (extPtr && *extPtr) {
        key = extPtr;
        for (auto& ch : key) ch = static_cast<wchar_t>(::towlower(ch));
    }

    static std::mutex cacheMutex;
    static std::map<std::wstring, std::wstring> cache;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;
    }

    std::wstring name;
    if (!key.empty()) {
        const std::wstring probe = L"file" + key;
        SHFILEINFOW sfi = {};
        if (::SHGetFileInfoW(probe.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) && sfi.szTypeName[0])
            name = sfi.szTypeName;
    }
    if (name.empty())
        name = L"文件";

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache[key] = name;
    }
    return name;
}

std::wstring CMainWnd::QueryShellTypeName(const std::wstring& path, bool isDir)
{
    SHFILEINFOW sfi = {};
    UINT flags = SHGFI_TYPENAME;
    DWORD attrs = 0;
    if (isDir) {
        flags |= SHGFI_USEFILEATTRIBUTES;
        attrs = FILE_ATTRIBUTE_DIRECTORY;
    }
    if (::SHGetFileInfoW(path.c_str(), attrs, &sfi, sizeof(sfi), flags) && sfi.szTypeName[0])
        return sfi.szTypeName;
    return isDir ? L"文件夹" : L"文件";
}

void CMainWnd::FillPreviewMetaFromPath(const std::wstring& path, bool isDir)
{
    std::wstring typeName = QueryShellTypeName(path, isDir);
    std::wstring sizeText = L"—";
    std::wstring mtimeText = L"—";
    std::wstring ctimeText = L"—";

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        const bool dirAttr = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (dirAttr || isDir) {
            sizeText = L"—";
        } else {
            const ULONGLONG bytes =
                (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
            sizeText = FormatFileSize(bytes);
        }
        mtimeText = FormatFileTimeLocal(fad.ftLastWriteTime);
        ctimeText = FormatFileTimeLocal(fad.ftCreationTime);
    }
    SetPreviewMeta(typeName, sizeText, mtimeText, ctimeText);
    SetPreviewExtraMeta(path, L"", L"", L"", L"", L"");
}

std::wstring CMainWnd::QueryImageDimensions(const std::wstring& path)
{
    if (!EnsureGdiplus()) return L"";
    Gdiplus::Bitmap bitmap(path.c_str(), FALSE);
    if (bitmap.GetLastStatus() != Gdiplus::Ok || bitmap.GetWidth() == 0 || bitmap.GetHeight() == 0)
        return L"";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%u x %u", bitmap.GetWidth(), bitmap.GetHeight());
    return buf;
}

void CMainWnd::FillVideoPreviewMeta(const std::wstring& path)
{
    VideoPropertyInfo info = ReadVideoProperties(path);
    std::wstring dimensions;
    if (info.width && info.height) {
        wchar_t buf[64] = {};
        swprintf_s(buf, L"%llu x %llu", info.width, info.height);
        dimensions = buf;
    }
    std::wstring frameRate;
    if (info.frameRateMilli) {
        wchar_t buf[48] = {};
        const double fps = info.frameRateMilli >= 1000
            ? static_cast<double>(info.frameRateMilli) / 1000.0
            : static_cast<double>(info.frameRateMilli);
        swprintf_s(buf, L"%.2f 帧/秒", fps);
        frameRate = buf;
    }

    ULONGLONG bytes = 0;
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        bytes = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    ULONGLONG totalRate = 0;
    if (bytes && info.duration100ns)
        totalRate = (bytes * 8ULL * 10000000ULL) / info.duration100ns;
    const ULONGLONG videoRate = totalRate > info.audioBitRate ? totalRate - info.audioBitRate : totalRate;
    SetPreviewExtraMeta(path, dimensions, FormatMediaDuration(info.duration100ns), frameRate,
        FormatBitRate(videoRate), FormatBitRate(totalRate));
}

void CMainWnd::ClearPreview()
{
    m_previewPath.clear();
    m_previewPathIsDir = true;
    m_previewFromSelection = false;
    if (!m_previewBmp.empty()) {
        m_PaintManager.RemoveImage(m_previewBmp.c_str());
        ::DeleteFileW(m_previewBmp.c_str());
        m_previewBmp.clear();
    }
    if (!m_previewVisible) {
        if (m_pPreviewPane)
            m_pPreviewPane->SetVisible(false);
        if (m_pPreviewTitle) m_pPreviewTitle->SetText(_T("预览"));
        ClearPreviewMeta();
        if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
        ShowPreviewDetails(false, false, false);
        return;
    }
    if (m_pPreviewPane)
        m_pPreviewPane->SetVisible(true);
    if (m_pPreviewTitle) m_pPreviewTitle->SetText(_T("预览"));
    if (m_pPreviewImage) {
        m_pPreviewImage->SetAttribute(_T("bkimage"), _T(""));
        m_pPreviewImage->SetBkImage(_T(""));
        m_pPreviewImage->Invalidate();
    }
    ClearPreviewMeta();
    if (m_pPreviewText)
        m_pPreviewText->SetText(_T(""));
    UpdatePreviewForCurrentFolder();
}

void CMainWnd::UpdatePreviewForCurrentFolder()
{
    if (!m_previewVisible) return;
    if (m_pPreviewPane)
        m_pPreviewPane->SetVisible(true);

    const int dirCount = static_cast<int>(m_listingDirs.size());
    const int fileCount = static_cast<int>(m_listingFiles.size());
    wchar_t summary[128] = {};
    swprintf_s(summary, L"%d 个文件夹 · %d 个文件", dirCount, fileCount);
    std::wstring sizeSummary = summary;
    if (m_listingTruncated)
        sizeSummary += L"（仅显示部分）";

    if (m_currentPath.empty()) {
        if (m_pPreviewTitle) m_pPreviewTitle->SetText(_T("当前目录"));
        SetPreviewMeta(L"文件夹", L"尚未打开目录", L"—", L"—");
        if (m_pPreviewText)
            m_pPreviewText->SetText(_T("选择文件或文件夹可查看详细信息"));
        ShowPreviewDetails(true, false, false);
        return;
    }

    m_previewPath = m_currentPath;
    m_previewPathIsDir = true;
    m_previewFromSelection = false;
    std::wstring title = IsThisPcPath(m_currentPath) ? L"此电脑" : GetLeafName(m_currentPath);
    if (title.empty()) title = m_currentPath;
    if (m_pPreviewTitle) m_pPreviewTitle->SetText(title.c_str());

    if (IsThisPcPath(m_currentPath)) {
        SetPreviewMeta(L"此电脑", sizeSummary, L"—", L"—");
    } else {
        FillPreviewMetaFromPath(m_currentPath, true);
        if (m_pPreviewSize) m_pPreviewSize->SetText(sizeSummary.c_str());
    }
    if (m_pPreviewText)
        m_pPreviewText->SetText(_T("当前目录概览\n未选择项目"));

    bool showImage = false;
    if (m_pPreviewImage) {
        m_pPreviewImage->SetBkImage(_T(""));
        showImage = LoadPreviewShellIcon(m_currentPath, true,
            DpiScale(UiTokens::PreviewIconPx));
    }
    ShowPreviewDetails(true, showImage, false);
}

void CMainWnd::UpdatePreviewForSelection()
{
    if (!m_previewVisible) return;
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.size() == 1) {
        UpdatePreviewPath(items[0].path, items[0].isDir);
        return;
    }
    if (items.empty()) {
        ClearPreview();
        return;
    }

    // Multi-selection has a useful summary, so reveal the pane for it as well.
    if (m_pPreviewPane)
        m_pPreviewPane->SetVisible(true);

    // Multi-select: drop cached path so a later single-select reloads.
    m_previewPath.clear();
    m_previewPathIsDir = true;
    m_previewFromSelection = false;
    if (!m_previewBmp.empty()) {
        m_PaintManager.RemoveImage(m_previewBmp.c_str());
        ::DeleteFileW(m_previewBmp.c_str());
        m_previewBmp.clear();
    }
    if (m_pPreviewImage) m_pPreviewImage->SetBkImage(_T(""));

    CDuiString title;
    title.Format(_T("已选择 %d 个项目"), (int)items.size());
    if (m_pPreviewTitle) m_pPreviewTitle->SetText(title.GetData());

    // Fast total: sum selected files only (skip folder recursion for UI snappiness).
    ULONGLONG total = 0;
    bool anyFile = false;
    for (const auto& it : items) {
        if (it.isDir) continue;
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (::GetFileAttributesExW(it.path.c_str(), GetFileExInfoStandard, &fad)) {
            total += (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
            anyFile = true;
        }
    }
    SetPreviewMeta(L"—",
        anyFile ? FormatFileSize(total) : L"—",
        L"—", L"—");
    if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
    ClearPreviewExtraMeta();
    ShowPreviewDetails(true, false, false);  // meta only — no empty thumb frame
}

void CMainWnd::UpdatePreviewPath(const std::wstring& path, bool isDir)
{
    if (!m_previewVisible) return;
    if (path.empty()) { ClearPreview(); return; }
    if (m_pPreviewPane)
        m_pPreviewPane->SetVisible(true);
    if (m_previewPath == path) return;
    m_previewPath = path;
    m_previewPathIsDir = isDir;
    m_previewFromSelection = true;

    std::wstring leaf = GetLeafName(path);
    if (m_pPreviewTitle) m_pPreviewTitle->SetText(leaf.c_str());
    FillPreviewMetaFromPath(path, isDir);
    if (isDir)
        ClearPreviewExtraMeta();
    else if (IsImageExtension(leaf))
        SetPreviewExtraMeta(path, QueryImageDimensions(path), L"", L"", L"", L"");
    else if (IsVideoExtension(leaf))
        FillVideoPreviewMeta(path);
    ShowPreviewDetails(true, true, true);

    if (isDir) {
        // Folders: HICON->PNG true alpha (never SIIGBF black pocket).
        const int ip = DpiScale(UiTokens::PreviewIconPx);
        if (LoadPreviewShellIcon(path, true, ip)) {
            if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
            return;
        }
        if (m_pPreviewImage) m_pPreviewImage->SetBkImage(_T(""));
        if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
        return;
    }
    if (IsImageExtension(leaf)) {
        if (LoadPreviewImage(path)) {
            if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
            return;
        }
    }
    if (IsVideoExtension(leaf)) {
        if (LoadPreviewShellThumbnail(path,
                DpiScale(UiTokens::PreviewThumbW), DpiScale(UiTokens::PreviewThumbH))) {
            if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
            return;
        }
    }
    if (IsTextExtension(leaf)) {
        if (m_pPreviewImage) {
            m_pPreviewImage->SetBkImage(_T(""));
            m_pPreviewImage->SetVisible(false);
        }
        if (CControlUI* gap = m_PaintManager.FindControl(_T("preview_gap_image")))
            gap->SetVisible(false);
        if (LoadPreviewText(path)) return;
    }
    // Generic files: compact HICON (true alpha PNG)
    {
        const int ip = DpiScale(UiTokens::PreviewIconPx);
        if (LoadPreviewShellIcon(path, false, ip)) {
            if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
            return;
        }
    }
    if (m_pPreviewImage) m_pPreviewImage->SetBkImage(_T(""));
    if (m_pPreviewText) m_pPreviewText->SetText(_T("暂不支持该类型预览"));
}

// DuiLib resizes the panes itself, so no WM_SIZE reaches us when the preview splitter
// (or a column-width change) moves. A light poll keeps the breadcrumb fit and the
// preview picture in sync with the live layout.
void CMainWnd::SyncLayoutDependents()
{
    if (m_pBreadcrumb) {
        const int w = static_cast<int>(m_pBreadcrumb->GetWidth());
        if (w > 8 && w != m_breadcrumbFitW) {
            m_breadcrumbFitW = w;
            RebuildBreadcrumb();
        }
    }
    if (m_pPreviewPane && m_previewVisible && m_pPreviewPane->IsVisible()) {
        // When content does not need to scroll, retain a quiet hairline that separates
        // preview from the file area. A visible scrollbar already provides that edge.
        const bool hasScrollBar = m_pPreviewBody && m_pPreviewBody->GetVerticalScrollBar()
            && m_pPreviewBody->GetVerticalScrollBar()->IsVisible();
        m_pPreviewPane->SetAttribute(_T("bordercolor"),
            hasScrollBar ? _T("#00000000") : UiTokens::ColorBorder);
        m_pPreviewPane->SetAttribute(_T("bordersize"), hasScrollBar ? _T("0") : _T("1,0,0,0"));

        const int w = static_cast<int>(m_pPreviewPane->GetWidth());
        if (w > 8 && w != m_previewPaneW) {
            m_previewPaneW = w;
            ReloadPreviewForWidth();
        }
    }
}

void CMainWnd::ReloadPreviewForWidth()
{
    if (!m_previewFromSelection) {
        UpdatePreviewForCurrentFolder();
        return;
    }
    const std::wstring path = m_previewPath;
    const bool isDir = m_previewPathIsDir;
    m_previewPath.clear();   // defeat the "same path" early-out in UpdatePreviewPath
    if (path.empty())
        UpdatePreviewForCurrentFolder();
    else
        UpdatePreviewPath(path, isDir);
}

// Resample a PNG on disk to exactly cx x cy (GDI+ HighQualityBicubic, alpha kept).
// Used so preview bitmaps reach DuiLib at their final pixel size and never go
// through the unfiltered AlphaBlend stretch.
bool CMainWnd::ResamplePngToSize(const std::wstring& pngPath, int cx, int cy)
{
    if (pngPath.empty() || cx <= 0 || cy <= 0 || cx > 4096 || cy > 4096) return false;
    using namespace Gdiplus;
    if (!EnsureGdiplus()) return false;

    const std::wstring tmp = pngPath + L".rs.tmp";
    bool written = false;
    {
        Bitmap src(pngPath.c_str());
        if (src.GetLastStatus() != Ok) return false;
        const int sw = src.GetWidth();
        const int sh = src.GetHeight();
        if (sw <= 0 || sh <= 0) return false;
        if (sw == cx && sh == cy) return true;

        Bitmap dst(cx, cy, PixelFormat32bppARGB);
        if (dst.GetLastStatus() != Ok) return false;
        {
            Graphics g(&dst);
            if (g.GetLastStatus() != Ok) return false;
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            g.SetCompositingQuality(CompositingQualityHighQuality);
            g.Clear(Color(0, 0, 0, 0));
            g.DrawImage(&src, Rect(0, 0, cx, cy), 0, 0, sw, sh, UnitPixel);
        }
        CLSID clsidPng = {};
        if (!GetPngEncoderClsid(&clsidPng)) return false;
        ::DeleteFileW(tmp.c_str());
        written = (dst.Save(tmp.c_str(), &clsidPng, nullptr) == Ok);
    }
    if (!written) return false;

    // Replace atomically-ish: the GDI+ reader above is closed by now, so the
    // original can be swapped out without leaving a stray temp file behind.
    ::DeleteFileW(pngPath.c_str());
    if (!::MoveFileW(tmp.c_str(), pngPath.c_str())) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// Pixel box the preview picture is fitted into. It follows the live pane width so
// dragging the splitter grows/shrinks the picture instead of leaving the thumb at a
// fixed design size (the box keeps the 408x255 design ratio, capped at PreviewImageH).
void CMainWnd::PreviewImageBox(int& boxW, int& boxH) const
{
    int ctrlW = m_pPreviewImage ? static_cast<int>(m_pPreviewImage->GetWidth()) : 0;
    if (ctrlW <= 8) ctrlW = DpiScale(UiTokens::PreviewThumbW);
    const int minW = DpiScale(96);
    if (ctrlW < minW) ctrlW = minW;
    if (ctrlW > DpiScale(1400)) ctrlW = DpiScale(1400);

    int h = static_cast<int>(ctrlW * 0.62);
    const int maxH = DpiScale(UiTokens::PreviewImageH);
    if (h > maxH) h = maxH;
    if (h < DpiScale(96)) h = DpiScale(96);
    boxW = ctrlW;
    boxH = h;
}

void CMainWnd::ApplyPreviewImageBk(const std::wstring& pngPath, int imgPxW, int imgPxH, int frameDesignH)
{
    if (!m_pPreviewImage || pngPath.empty() || imgPxW <= 0 || imgPxH <= 0)
        return;

    const int frameH = DpiScale((std::max)(frameDesignH, 1));
    m_pPreviewImage->SetFixedHeight(frameH);

    // Prefer live layout width; fall back to design thumb width.
    int ctrlW = m_pPreviewImage->GetWidth();
    if (ctrlW <= 8)
        ctrlW = DpiScale(UiTokens::PreviewThumbW);
    const int ctrlH = frameH;

    // Center Fit: scale image to fit control, then center (not top-left).
    double sx = static_cast<double>(ctrlW) / static_cast<double>(imgPxW);
    double sy = static_cast<double>(ctrlH) / static_cast<double>(imgPxH);
    double scale = (std::min)(sx, sy);
    if (scale <= 0.0) scale = 1.0;
    int dw = (std::max)(1, static_cast<int>(imgPxW * scale));
    int dh = (std::max)(1, static_cast<int>(imgPxH * scale));
    int ox = (ctrlW - dw) / 2;
    int oy = (ctrlH - dh) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    // DuiLib blits bkimage with AlphaBlend, whose scaling is unfiltered, so any
    // preview PNG that did not match its destination pixel-exactly came out soft or
    // aliased. Resample once (GDI+ HighQualityBicubic) to the exact draw size and
    // then blit 1:1.
    int drawW = imgPxW;
    int drawH = imgPxH;
    if ((dw != imgPxW || dh != imgPxH) && ResamplePngToSize(pngPath, dw, dh)) {
        drawW = dw;
        drawH = dh;
        m_PaintManager.RemoveImage(pngPath.c_str());
    }

    CDuiString img;
    img.Format(_T("file='%s' dest='%d,%d,%d,%d' source='0,0,%d,%d'"),
        pngPath.c_str(), ox, oy, ox + dw, oy + dh, drawW, drawH);
    m_pPreviewImage->SetBkImage(img.GetData());
    m_pPreviewImage->Invalidate();
    if (m_pPreviewPane)
        m_pPreviewPane->NeedUpdate();
}

bool CMainWnd::LoadPreviewImage(const std::wstring& path)
{
    if (!m_pPreviewImage) return false;

    // DuiLib caches bitmaps by file path; SetBkImage no-ops when draw-string
    // is unchanged. Reusing one preview bmp left a stale thumb after select.
    if (!m_previewBmp.empty()) {
        m_PaintManager.RemoveImage(m_previewBmp.c_str());
        ::DeleteFileW(m_previewBmp.c_str());
        m_previewBmp.clear();
    }
    m_pPreviewImage->SetBkImage(_T(""));

    // Render the thumb at the exact size the pane will show: no DuiLib stretch, and the
    // picture follows the splitter when the pane is dragged wider or narrower.
    int boxW = 0, boxH = 0;
    PreviewImageBox(boxW, boxH);

    ++m_previewSerial;
    wchar_t leaf[64] = {};
    swprintf_s(leaf, L"preview_%u.png", m_previewSerial);
    m_previewBmp = m_iconCacheDir + leaf;
    if (!SaveImageThumbnailPng(path, m_previewBmp, boxW, boxH)) {
        m_previewBmp.clear();
        return false;
    }
    m_PaintManager.RemoveImage(m_previewBmp.c_str());

    const int frameDesignH = (std::max)(48, ::MulDiv(boxH, 96, (int)m_dpi));
    ApplyPreviewImageBk(m_previewBmp, boxW, boxH, frameDesignH);
    return true;
}

bool CMainWnd::LoadPreviewShellIcon(const std::wstring& path, bool isDir, int iconPx)
{
    if (!m_pPreviewImage || path.empty()) return false;
    if (!m_previewBmp.empty()) {
        m_PaintManager.RemoveImage(m_previewBmp.c_str());
        ::DeleteFileW(m_previewBmp.c_str());
        m_previewBmp.clear();
    }
    m_pPreviewImage->SetBkImage(_T(""));

    // Request the icon at the exact size the compact frame will show it. Asking for a
    // smaller PNG left DuiLib upscaling it with AlphaBlend (unfiltered) — that is why
    // the folder preview looked mushy. Worst case the Shell hands back the 384px JUMBO
    // icon and we downscale it once with GDI+ HighQualityBicubic.
    const int boxH = DpiScale(UiTokens::PreviewIconCompactH);
    int ctrlW = m_pPreviewImage->GetWidth();
    if (ctrlW <= 8) ctrlW = DpiScale(UiTokens::PreviewThumbW);
    int ip = (std::min)(ctrlW, boxH);
    if (ip < iconPx) ip = iconPx;
    if (ip < 16) ip = 16;
    if (ip > 384) ip = 384;

    ++m_previewSerial;
    wchar_t leaf[64] = {};
    swprintf_s(leaf, L"preview_icon_%u.png", m_previewSerial);
    m_previewBmp = m_iconCacheDir + leaf;

    bool ok = ExtractShellIconSized(path, isDir, ip, m_previewBmp);
    if (!ok && isDir)
        ok = ExtractShellIconSized(path, true, ip, m_previewBmp);
    if (!ok) {
        // Stock folder / document fallback via sized extract of known path fails:
        // use GetStockIconBmp then copy into preview slot.
        std::wstring stock = isDir
            ? GetStockIconBmp(SIID_FOLDER, ip)
            : GetStockIconBmp(SIID_DOCNOASSOC, ip);
        if (!stock.empty() && ::CopyFileW(stock.c_str(), m_previewBmp.c_str(), FALSE))
            ok = true;
    }
    if (!ok) {
        m_previewBmp.clear();
        return false;
    }
    m_PaintManager.RemoveImage(m_previewBmp.c_str());
    ApplyPreviewImageBk(m_previewBmp, ip, ip, UiTokens::PreviewIconCompactH);
    return true;
}

bool CMainWnd::LoadPreviewShellThumbnail(const std::wstring& path, int cx, int cy)
{
    if (!m_pPreviewImage || path.empty()) return false;
    if (!m_previewBmp.empty()) {
        m_PaintManager.RemoveImage(m_previewBmp.c_str());
        ::DeleteFileW(m_previewBmp.c_str());
        m_previewBmp.clear();
    }
    m_pPreviewImage->SetBkImage(_T(""));

    // Physical pixel request (callers pass design or already-scaled; normalize).
    int reqW = cx;
    int reqH = cy;
    if (reqW < 16) reqW = 16;
    if (reqH < 16) reqH = 16;
    // Anything bigger than the compact icon box is the picture area: re-fit it to the
    // live pane so the thumb tracks the splitter.
    const bool iconSized = (reqW <= DpiScale(UiTokens::PreviewIconPx) + 8
                            && reqH <= DpiScale(UiTokens::PreviewIconPx) + 8);
    if (!iconSized) {
        int bw = 0, bh = 0;
        PreviewImageBox(bw, bh);
        reqW = bw;
        reqH = bh;
    }

    ++m_previewSerial;
    wchar_t leaf[64] = {};
    swprintf_s(leaf, L"preview_shell_%u.png", m_previewSerial);
    m_previewBmp = m_iconCacheDir + leaf;
    if (!ExtractShellItemImage(path, reqW, reqH, m_previewBmp)) {
        m_previewBmp.clear();
        return false;
    }
    m_PaintManager.RemoveImage(m_previewBmp.c_str());

    int imgW = reqW, imgH = reqH;
    // Compact frame when request is icon-sized (folders / generic).
    int frameDesignH = iconSized
        ? UiTokens::PreviewIconCompactH
        : (std::max)(48, ::MulDiv(reqH, 96, (int)m_dpi));
    {
        using namespace Gdiplus;
        if (EnsureGdiplus()) {
            Bitmap bmp(m_previewBmp.c_str());
            if (bmp.GetLastStatus() == Ok && bmp.GetWidth() > 0 && bmp.GetHeight() > 0) {
                imgW = bmp.GetWidth();
                imgH = bmp.GetHeight();
            }
        }
    }
    ApplyPreviewImageBk(m_previewBmp, imgW, imgH, frameDesignH);
    return true;
}

bool CMainWnd::LoadPreviewText(const std::wstring& path)
{
    if (!m_pPreviewText) return false;
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li = {};
    ::GetFileSizeEx(h, &li);
    DWORD toRead = (DWORD)(std::min<LONGLONG>(li.QuadPart, kPreviewMaxTextBytes));
    std::string raw(toRead, '\0');
    DWORD got = 0;
    BOOL ok = ::ReadFile(h, raw.data(), toRead, &got, nullptr);
    ::CloseHandle(h);
    if (!ok) return false;
    raw.resize(got);

    std::wstring text;
    if (got >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        text.assign(reinterpret_cast<const wchar_t*>(raw.data() + 2), (got - 2) / 2);
    } else if (got >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        int n = ::MultiByteToWideChar(CP_UTF8, 0, raw.data() + 3, (int)got - 3, nullptr, 0);
        text.resize(n);
        ::MultiByteToWideChar(CP_UTF8, 0, raw.data() + 3, (int)got - 3, &text[0], n);
    } else {
        int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), (int)got, nullptr, 0);
        if (n > 0) {
            text.resize(n);
            ::MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)got, &text[0], n);
        } else {
            n = ::MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)got, nullptr, 0);
            text.resize(n);
            ::MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)got, &text[0], n);
        }
    }
    // Soft-limit UI string length
    if (text.size() > 8000) {
        text.resize(8000);
        text += L"\n…";
    }
    if (li.QuadPart > kPreviewMaxTextBytes)
        text += L"\n\n文件已截断预览。";
    m_pPreviewText->SetText(text.c_str());
    return true;
}
