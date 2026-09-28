// FastFile - navigation, listing refresh, search filter, breadcrumb/address bar
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

namespace {

ULONGLONG FileTimeToU64(const FILETIME& ft)
{
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

} // namespace

void CMainWnd::NavigateTo(const std::wstring& path, bool addToHistory)
{
    std::wstring raw = path;
    while (!raw.empty() && (raw.front() == L' ' || raw.front() == L'"'))
        raw.erase(raw.begin());
    while (!raw.empty() && (raw.back() == L' ' || raw.back() == L'"'))
        raw.pop_back();

    // Accept "此电脑" typed in the address bar.
    if (raw == kThisPcPath || raw == L"此电脑" || ::_wcsicmp(raw.c_str(), L"This PC") == 0) {
        if (addToHistory && !m_currentPath.empty() && !IsThisPcPath(m_currentPath))
            PushHistoryBeforeNav(m_currentPath);
        m_currentPath = kThisPcPath;
        m_searchFilter.clear();
        if (m_pSearchEdit) SetSearchPlaceholder(true);
        if (m_pAddressEdit)
            m_pAddressEdit->SetText(_T("此电脑"));
        {
            ViewMode remembered = LoadFolderViewForPath(m_currentPath);
            if (m_viewMode != remembered) {
                m_viewMode = remembered;
                m_iconAnchor = -1;
                m_lastIconClickTile = nullptr;
                m_lastIconClickTick = 0;
            }
            UpdateViewModeButtons();
        }
        UpdateActiveTabPath(m_currentPath);
        RefreshListing();
        SyncTreeToPath(m_currentPath);
        UpdateFavoritesHighlight();
        UpdateNavButtons();
        if (m_addressEditMode) {
            m_addressEditMode = false;
            if (m_pAddressEditHost) m_pAddressEditHost->SetVisible(false);
            if (m_pBreadcrumb) m_pBreadcrumb->SetVisible(true);
        }
        RebuildBreadcrumb();
        ClearPreview();
        return;
    }

    std::wstring target = NormalizePath(raw);
    if (target.empty()) {
        UpdateStatus(_T("路径为空"));
        return;
    }

    DWORD attrs = ::GetFileAttributesW(target.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        CDuiString tip;
        tip.Format(_T("无法打开目录: %s"), target.c_str());
        UpdateStatus(tip.GetData());
        return;
    }

    if (addToHistory && !m_currentPath.empty() && m_currentPath != target)
        PushHistoryBeforeNav(m_currentPath);

    m_currentPath = target;
    m_searchFilter.clear();
    if (m_pSearchEdit) SetSearchPlaceholder(true);
    if (m_pAddressEdit)
        m_pAddressEdit->SetText(m_currentPath.c_str());

    {
        ViewMode remembered = LoadFolderViewForPath(m_currentPath);
        if (m_viewMode != remembered) {
            m_viewMode = remembered;
            m_iconAnchor = -1;
            m_lastIconClickTile = nullptr;
            m_lastIconClickTick = 0;
        }
        UpdateViewModeButtons();
    }

    UpdateActiveTabPath(m_currentPath);
    RefreshListing();
    SyncTreeToPath(m_currentPath);
    UpdateFavoritesHighlight();
    UpdateNavButtons();
    if (m_addressEditMode) {
        m_addressEditMode = false;
        if (m_pAddressEditHost) m_pAddressEditHost->SetVisible(false);
        if (m_pBreadcrumb) m_pBreadcrumb->SetVisible(true);
    }
    RebuildBreadcrumb();
    ClearPreview();
}

void CMainWnd::GoUp()
{
    if (IsThisPcPath(m_currentPath)) {
        UpdateStatus(_T("已在此电脑"));
        return;
    }
    std::wstring parent = ParentPath(m_currentPath);
    if (parent.empty()) {
        NavigateTo(kThisPcPath, true);
        return;
    }
    NavigateTo(parent, true);
}

void CMainWnd::PushHistoryBeforeNav(const std::wstring& fromPath)
{
    if (m_navigatingHistory) return;
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return;
    TabInfo& tab = m_tabs[m_activeTab];
    if (!fromPath.empty())
        tab.backStack.push_back(fromPath);
    tab.forwardStack.clear();
    if (tab.backStack.size() > 64)
        tab.backStack.erase(tab.backStack.begin());
}

void CMainWnd::GoBack()
{
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return;
    TabInfo& tab = m_tabs[m_activeTab];
    if (tab.backStack.empty()) {
        UpdateStatus(_T("没有后退记录"));
        return;
    }
    std::wstring dest = tab.backStack.back();
    tab.backStack.pop_back();
    if (!m_currentPath.empty())
        tab.forwardStack.push_back(m_currentPath);
    m_navigatingHistory = true;
    NavigateTo(dest, false);
    m_navigatingHistory = false;
    UpdateNavButtons();
}

void CMainWnd::GoForward()
{
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return;
    TabInfo& tab = m_tabs[m_activeTab];
    if (tab.forwardStack.empty()) {
        UpdateStatus(_T("没有前进记录"));
        return;
    }
    std::wstring dest = tab.forwardStack.back();
    tab.forwardStack.pop_back();
    if (!m_currentPath.empty())
        tab.backStack.push_back(m_currentPath);
    m_navigatingHistory = true;
    NavigateTo(dest, false);
    m_navigatingHistory = false;
    UpdateNavButtons();
}

void CMainWnd::UpdateNavButtons()
{
    bool canBack = false, canFwd = false;
    if (m_activeTab >= 0 && m_activeTab < (int)m_tabs.size()) {
        canBack = !m_tabs[m_activeTab].backStack.empty();
        canFwd = !m_tabs[m_activeTab].forwardStack.empty();
    }
    if (m_pBtnBack) {
        m_pBtnBack->SetEnabled(canBack);
        m_pBtnBack->SetAttribute(_T("textcolor"), canBack ? _T("#FF1F2937") : _T("#FF9CA3AF"));
    }
    if (m_pBtnForward) {
        m_pBtnForward->SetEnabled(canFwd);
        m_pBtnForward->SetAttribute(_T("textcolor"), canFwd ? _T("#FF1F2937") : _T("#FF9CA3AF"));
    }
}

void CMainWnd::OnItemActivate(CControlUI* pSender)
{
    if (!pSender) return;
    CControlUI* pItem = FindListItemRoot(pSender);
    if (!pItem) return;
    CDuiString ud = pItem->GetUserData();
    if (ud.IsEmpty()) return;

    const bool isDir = (pItem->GetTag() != 0);
    std::wstring path = ud.GetData();
    if (isDir) {
        NavigateTo(path, true);
    } else {
        ::ShellExecuteW(m_hWnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        CDuiString tip;
        tip.Format(_T("已打开: %s"), path.c_str());
        UpdateStatus(tip.GetData());
    }
}

void CMainWnd::RefreshListing()
{
    if (m_currentPath.empty()) {
        UpdateStatus(_T("当前路径为空"));
        return;
    }

    CancelThumbJobs();

    if (!m_copyRunning.load())
        UpdateStatus(_T("正在枚举…"));

    if (m_pFileList)
        m_pFileList->RemoveAll();
    ClearIconView();

    std::vector<DirEntry> dirs;
    std::vector<DirEntry> files;
    bool truncated = false;

    if (IsThisPcPath(m_currentPath)) {
        wchar_t drives[512] = {};
        const DWORD n = ::GetLogicalDriveStringsW(_countof(drives) - 1, drives);
        if (n > 0 && n < _countof(drives)) {
            for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
                DirEntry e;
                e.name = FormatDriveDisplayName(p);
                e.fullPath = p;
                e.isDir = true;
                ULARGE_INTEGER available = {}, total = {}, totalFree = {};
                if (::GetDiskFreeSpaceExW(p, &available, &total, &totalFree)) {
                    e.size = available.QuadPart;
                    e.capacity = total.QuadPart;
                }
                e.mtime = 0;
                e.attrs = FILE_ATTRIBUTE_DIRECTORY;
                if (EntryMatchesFilter(e))
                    dirs.push_back(std::move(e));
            }
        }
        std::stable_sort(dirs.begin(), dirs.end(), [](const DirEntry& a, const DirEntry& b) {
            const wchar_t da = a.fullPath.empty() ? L'Z' : static_cast<wchar_t>(::towupper(a.fullPath[0]));
            const wchar_t db = b.fullPath.empty() ? L'Z' : static_cast<wchar_t>(::towupper(b.fullPath[0]));
            if (da == L'C') return db != L'C';
            if (db == L'C') return false;
            return da < db;
        });
    } else if (IsRecursiveSearch() && !m_searchFilter.empty()) {
        CollectRecursiveMatches(m_currentPath, m_searchFilter, dirs, files, truncated);
    } else {
        std::wstring pattern = m_currentPath;
        if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
            pattern.push_back(L'\\');
        pattern += L"*";

        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = ::FindFirstFileExW(
            pattern.c_str(), FindExInfoBasic, &fd,
            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);

        if (hFind == INVALID_HANDLE_VALUE) {
            CDuiString tip;
            tip.Format(_T("枚举失败 (%lu): %s"), ::GetLastError(), m_currentPath.c_str());
            UpdateStatus(tip.GetData());
            m_hasListingCache = false;
            return;
        }

        dirs.reserve(256);
        files.reserve(1024);
        int scanned = 0;
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;

            DirEntry e;
            e.name = fd.cFileName;
            e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            e.size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            e.mtime = FileTimeToU64(fd.ftLastWriteTime);
            e.attrs = fd.dwFileAttributes;
            e.fullPath = m_currentPath;
            if (!e.fullPath.empty() && e.fullPath.back() != L'\\' && e.fullPath.back() != L'/')
                e.fullPath.push_back(L'\\');
            e.fullPath += e.name;

            if (ShouldHideByAttributes(e.attrs))
                continue;
            if (!EntryMatchesFilter(e))
                continue;

            if (e.isDir) dirs.push_back(std::move(e));
            else files.push_back(std::move(e));

            ++scanned;
            if ((scanned % kPumpEvery) == 0)
                PumpUiMessages();

            // The details view is virtualised and can afford far more entries; the icon views
            // still create one control per item and keep the smaller bound.
            const int scanCap = IsTileViewMode() ? kMaxListItems : kMaxDetailsItems;
            if (static_cast<int>(dirs.size() + files.size()) >= scanCap) {
                truncated = true;
                break;
            }
        } while (::FindNextFileW(hFind, &fd));
        ::FindClose(hFind);
    }

    StoreListingCache(std::move(dirs), std::move(files), truncated);
    SortListingCache();

    if (IsTileViewMode())
        RebuildIconsView(m_listingDirs, m_listingFiles, m_listingTruncated);
    else
        RebuildDetailsView(m_listingDirs, m_listingFiles, m_listingTruncated);

    if (m_copyRunning.load()) {
        OnCopyProgressMessage();
        return;
    }

    UpdateListingStatusTip();
    UpdateEmptyStateHint();
}

void CMainWnd::StoreListingCache(std::vector<DirEntry> dirs, std::vector<DirEntry> files, bool truncated)
{
    m_listingDirs = std::move(dirs);
    m_listingFiles = std::move(files);
    m_listingTruncated = truncated;
    m_listingPath = m_currentPath;
    m_listingFilter = m_searchFilter;
    m_listingRecursive = IsRecursiveSearch();
    m_hasListingCache = true;
}

void CMainWnd::UpdateListingStatusTip()
{
    if (m_copyRunning.load())
        return;
    const int shown = static_cast<int>(m_listingDirs.size() + m_listingFiles.size());
    CDuiString tip;
    if (!m_searchFilter.empty()) {
        if (IsRecursiveSearch())
            tip.Format(_T("递归搜索 \"%s\"  ·  %d 项%s"), m_searchFilter.c_str(), shown,
                m_listingTruncated ? _T("（已截断）") : _T(""));
        else
            tip.Format(_T("筛选 \"%s\"  ·  %d 项"), m_searchFilter.c_str(), shown);
    } else if (IsThisPcPath(m_currentPath)) {
        tip.Format(_T("此电脑  ·  %d 个驱动器"), shown);
    } else if (m_listingTruncated) {
        tip.Format(_T("%s  ·  显示 %d 项（已截断）"), m_currentPath.c_str(), shown);
    } else {
        tip.Format(_T("%s  ·  %d 文件夹 / %d 文件"),
            m_currentPath.c_str(),
            static_cast<int>(m_listingDirs.size()),
            static_cast<int>(m_listingFiles.size()));
    }

    // Selected / clipboard counts - separator spacing from StatusCountSepPad
    {
        CDuiString sep;
        {
            const int n = UiTokens::StatusCountSepPad / 4; // design spaces (~SpaceSm -> 2)
            const int spaces = n > 0 ? n : 1;
            sep = _T(" ");
            for (int i = 1; i < spaces; ++i) sep += _T(" ");
            sep += _T("|");
            for (int i = 0; i < spaces; ++i) sep += _T(" ");
        }
        std::vector<ClipboardItem> sel;
        CollectSelectedItems(sel);
        if (!sel.empty()) {
            CDuiString selTip;
            selTip.Format(_T("%s已选 %d 项"), sep.GetData(), static_cast<int>(sel.size()));
            tip += selTip;
        }
        if (!m_clipboard.empty()) {
            CDuiString clip;
            clip.Format(_T("%s剪贴板 %d 项"), sep.GetData(), static_cast<int>(m_clipboard.size()));
            tip += clip;
        }
    }
    UpdateStatus(tip.GetData());
}

void CMainWnd::RebuildCurrentViewFromCache()
{
    if (!m_hasListingCache)
        return;
    CancelThumbJobs();
    if (m_pFileList)
        m_pFileList->RemoveAll();
    // ClearIconView 会在非复用路径里调用；图标尺寸切换尽量 TryReuse
    if (IsTileViewMode())
        RebuildIconsView(m_listingDirs, m_listingFiles, m_listingTruncated);
    else {
        ClearIconView();
        RebuildDetailsView(m_listingDirs, m_listingFiles, m_listingTruncated);
    }
    UpdateListingStatusTip();
    UpdateEmptyStateHint();
}

void CMainWnd::UpdateEmptyStateHint()
{
    CControlUI* hint = m_PaintManager.FindControl(_T("list_empty_hint"));
    if (!hint) return;

    // "Empty" means the *view* has no items: a search filter can hide every entry of a folder
    // that is not empty at all.
    const bool empty = IsTileViewMode()
        ? (!m_pIconTiles || m_pIconTiles->GetCount() == 0)
        : (!m_pFileList || m_pFileList->GetCount() == 0);

    if (!empty) {
        if (hint->IsVisible())
            hint->SetVisible(false);
        return;
    }

    CDuiString text;
    if (!m_searchFilter.empty())
        text.Format(_T("没有找到名称包含「%s」的项目"), m_searchFilter.c_str());
    else if (IsThisPcPath(m_currentPath))
        text = _T("未检测到可用的驱动器");
    else
        text = _T("此文件夹为空");
    hint->SetText(text);
    hint->SetVisible(true);
}

// ---- Search filter -------------------------------------------------------

bool CMainWnd::EntryMatchesFilter(const DirEntry& e) const
{
    if (m_searchFilter.empty())
        return true;
    return ::StrStrIW(e.name.c_str(), m_searchFilter.c_str()) != nullptr;
}

bool CMainWnd::IsRecursiveSearch() const
{
    return m_pChkRecursive && m_pChkRecursive->IsSelected();
}

void CMainWnd::CollectRecursiveMatches(const std::wstring& root, const std::wstring& filter,
    std::vector<DirEntry>& dirs, std::vector<DirEntry>& files, bool& truncated)
{
    truncated = false;
    dirs.clear();
    files.clear();
    if (root.empty() || filter.empty()) return;

    std::vector<std::wstring> queue;
    queue.push_back(root);
    int visited = 0;

    while (!queue.empty()) {
        std::wstring dir = queue.back();
        queue.pop_back();

        std::wstring pattern = dir;
        if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
            pattern.push_back(L'\\');
        pattern += L"*";

        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = ::FindFirstFileExW(
            pattern.c_str(), FindExInfoBasic, &fd,
            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (hFind == INVALID_HANDLE_VALUE)
            continue;

        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            std::wstring full = dir;
            if (!full.empty() && full.back() != L'\\' && full.back() != L'/')
                full.push_back(L'\\');
            full += fd.cFileName;

            if (isDir && !ShouldHideByAttributes(fd.dwFileAttributes)) {
                queue.push_back(full);
            }

            if (::StrStrIW(fd.cFileName, filter.c_str()) != nullptr) {
                DirEntry e;
                // Show relative path under root for clarity
                std::wstring rel = full;
                if (rel.size() > root.size()
                    && _wcsnicmp(rel.c_str(), root.c_str(), (int)root.size()) == 0) {
                    rel = rel.substr(root.size());
                    while (!rel.empty() && (rel.front() == L'\\' || rel.front() == L'/'))
                        rel.erase(rel.begin());
                }
                e.name = rel.empty() ? fd.cFileName : rel;
                e.fullPath = full;
                e.isDir = isDir;
                e.size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                e.mtime = FileTimeToU64(fd.ftLastWriteTime);
                e.attrs = fd.dwFileAttributes;
                if (ShouldHideByAttributes(e.attrs))
                    continue;
                if (isDir) dirs.push_back(std::move(e));
                else files.push_back(std::move(e));
            }

            ++visited;
            if ((visited % kPumpEvery) == 0)
                PumpUiMessages();

            if ((int)(dirs.size() + files.size()) >= kMaxRecursiveItems) {
                truncated = true;
                ::FindClose(hFind);
                queue.clear();
                break;
            }
        } while (::FindNextFileW(hFind, &fd));
        if (truncated) break;
        ::FindClose(hFind);
    }
}

void CMainWnd::ApplySearchFilter()
{
    if (!m_pSearchEdit) return;
    if (m_searchPlaceholder) {
        m_searchFilter.clear();
    } else {
        m_searchFilter = m_pSearchEdit->GetText().GetData();
        while (!m_searchFilter.empty() && (m_searchFilter.front() == L' ' || m_searchFilter.front() == L'\t'))
            m_searchFilter.erase(m_searchFilter.begin());
        while (!m_searchFilter.empty() && (m_searchFilter.back() == L' ' || m_searchFilter.back() == L'\t'))
            m_searchFilter.pop_back();
    }
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
        m_tabs[m_activeTab].searchFilter = m_searchFilter;
    if (IsRecursiveSearch() && !m_searchFilter.empty())
        UpdateStatus(_T("正在递归搜索…"));
    RefreshListing();
}

void CMainWnd::ClearSearchFilter()
{
    m_searchFilter.clear();
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
        m_tabs[m_activeTab].searchFilter.clear();
    SetSearchPlaceholder(true);
    RefreshListing();
}

void CMainWnd::SetSearchPlaceholder(bool show)
{
    if (!m_pSearchEdit) return;
    m_searchPlaceholder = show;
    if (show) {
        m_pSearchEdit->SetText(L"搜索");
        m_pSearchEdit->SetAttribute(_T("textcolor"), _T("#FFB0B0B0"));
    } else {
        // Clear placeholder glyph; keep any real typed text.
        if (std::wstring(m_pSearchEdit->GetText().GetData()) == L"搜索")
            m_pSearchEdit->SetText(_T(""));
        m_pSearchEdit->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
    }
}

void CMainWnd::SyncRecursiveCheckLabel()
{
    if (!m_pChkRecursive) return;
    const bool on = m_pChkRecursive->IsSelected();
    m_pChkRecursive->SetText(on ? L"☑ 含子目录" : L"☐ 含子目录");
    m_pChkRecursive->SetAttribute(_T("font"), _T("0"));
    m_pChkRecursive->SetAttribute(_T("valign"), _T("vcenter"));
    m_pChkRecursive->SetAttribute(_T("align"), _T("left"));
}

void CMainWnd::SyncAddressEditFromPath()
{
    if (!m_pAddressEdit) return;
    if (IsThisPcPath(m_currentPath) || m_currentPath.empty())
        m_pAddressEdit->SetText(_T("此电脑"));
    else
        m_pAddressEdit->SetText(m_currentPath.c_str());
}

void CMainWnd::EnterAddressEditMode()
{
    if (m_addressEditMode) {
        if (m_pAddressEdit)
            m_PaintManager.SetFocus(m_pAddressEdit);
        return;
    }
    m_addressEditMode = true;
    SyncAddressEditFromPath();
    if (m_pBreadcrumb)
        m_pBreadcrumb->SetVisible(false);
    if (m_pAddressEditHost)
        m_pAddressEditHost->SetVisible(true);
    // The native edit window also has to be shown again: ExitAddressEditMode hides the
    // control itself so that it stops owning the keyboard (see the note there).
    if (m_pAddressEdit)
        m_pAddressEdit->SetVisible(true);
    if (m_pPathHost)
        m_pPathHost->NeedUpdate();
    if (m_pAddressEdit) {
        m_PaintManager.SetFocus(m_pAddressEdit);
        // Select-all when native edit window appears
        m_pAddressEdit->SetSelAll();
    }
}

void CMainWnd::ExitAddressEditMode(bool commitNavigate)
{
    if (!m_addressEditMode && !commitNavigate)
        return;
    std::wstring typed;
    if (m_pAddressEdit)
        typed = m_pAddressEdit->GetText().GetData();

    m_addressEditMode = false;
    if (m_pAddressEditHost)
        m_pAddressEditHost->SetVisible(false);
    // Hiding the host alone is not enough: DuiLib leaves the native edit window alive and
    // still focused, and Windows routes WM_KEYDOWN (Ctrl+A/C/V/X/Z, Del, F2, ...) to the
    // focus window - so every file-view shortcut would land in a hidden text box. Hiding
    // the edit control itself makes DuiLib drop its focus and hand the native focus back
    // to the paint window (see CEditUI::SetVisible / CPaintManagerUI::SetFocus).
    if (m_pAddressEdit)
        m_pAddressEdit->SetVisible(false);
    if (m_pBreadcrumb)
        m_pBreadcrumb->SetVisible(true);

    if (commitNavigate) {
        if (!typed.empty())
            NavigateTo(typed, true);
        else
            RebuildBreadcrumb();
    } else {
        SyncAddressEditFromPath();
        RebuildBreadcrumb();
    }
    if (m_pPathHost)
        m_pPathHost->NeedUpdate();

    ReturnFocusToFileView();
}

void CMainWnd::ReturnFocusToFileView()
{
    if (!m_hWnd) return;

    // DuiLib hides the native address edit but leaves it holding the keyboard focus, so
    // every later shortcut (Ctrl+A/C/V/X/Z, Del, F2, F5, ...) would be delivered to a
    // hidden text box and silently dropped until the user clicked the listing.
    // Only reclaim focus while it is still that very edit, so clicking into the search box
    // (or another app) keeps its focus.
    const HWND hEdit = m_pAddressEdit ? m_pAddressEdit->GetNativeEditHWND() : nullptr;
    if (!hEdit || ::GetFocus() != hEdit)
        return;

    CControlUI* focusTarget = nullptr;
    if (IsTileViewMode() && m_pIconTiles)
        focusTarget = m_pIconTiles;
    else if (m_pFileList)
        focusTarget = m_pFileList;
    if (focusTarget)
        m_PaintManager.SetFocus(focusTarget);   // also pulls the native focus to the paint window
    ::SetFocus(m_hWnd);
}

void CMainWnd::RebuildBreadcrumb()
{
    if (!m_pBreadcrumb) return;
    m_pBreadcrumb->RemoveAll();

    // ---- 1. Build the full chain (此电脑 / drive / folder...) --------------
    std::vector<std::pair<std::wstring, std::wstring>> chain;
    chain.push_back({ L"此电脑", kThisPcPath });
    // 此电脑 is a pseudo path ("::ThisPC"): never run it through NormalizePath, which
    // would turn it into a bogus drive-relative path and fake a "本地磁盘 (:)" segment.
    if (!IsThisPcPath(m_currentPath) && !m_currentPath.empty()) {
        std::wstring path = NormalizePath(m_currentPath);
        if (!path.empty()) {
            if (path.size() >= 2 && path[1] == L':') {
                std::wstring drive = path.substr(0, 2) + L"\\";
                chain.push_back({ FormatDriveDisplayName(drive), NormalizePath(drive) });
                std::wstring rest = path.size() > 3 ? path.substr(3) : L"";
                std::wstring acc = NormalizePath(drive);
                size_t start = 0;
                while (start < rest.size()) {
                    size_t slash = rest.find(L'\\', start);
                    std::wstring part = (slash == std::wstring::npos)
                        ? rest.substr(start) : rest.substr(start, slash - start);
                    if (!part.empty()) {
                        if (!acc.empty() && acc.back() != L'\\') acc.push_back(L'\\');
                        acc += part;
                        chain.push_back({ part, NormalizePath(acc) });
                    }
                    if (slash == std::wstring::npos) break;
                    start = slash + 1;
                }
            } else {
                chain.push_back({ GetLeafName(path), path });
            }
        }
    }

    // ---- 2. Measure every label so the row can be fitted to the address bar ----
    const int padX = DpiScale(UiTokens::BreadcrumbSegPadX);
    const int sepW = DpiScale(UiTokens::BreadcrumbSepW);
    const int segH = DpiScale(UiTokens::HitBreadcrumbH);
    struct Seg { std::wstring label; std::wstring path; int w; };
    std::vector<Seg> segs;
    segs.reserve(chain.size());
    {
        HFONT font = m_PaintManager.GetFont(0);
        HDC dc = m_hWnd ? ::GetDC(m_hWnd) : nullptr;
        for (const auto& c : chain) {
            SIZE labelSize = { 0, 0 };
            if (dc) {
                HGDIOBJ oldFont = font ? ::SelectObject(dc, font) : nullptr;
                ::GetTextExtentPoint32W(dc, c.first.c_str(),
                    static_cast<int>(c.first.size()), &labelSize);
                if (oldFont) ::SelectObject(dc, oldFont);
            }
            if (labelSize.cx <= 0)
                labelSize.cx = DpiScale(static_cast<int>(c.first.size()) * 8);
            int w = labelSize.cx + padX * 2 + DpiScale(6);
            if (w < DpiScale(28)) w = DpiScale(28);
            segs.push_back({ c.first, c.second, w });
        }
        if (dc) ::ReleaseDC(m_hWnd, dc);
    }

    // ---- 3. Fit: drop leading segments, never the folder you are in --------
    // Explorer keeps the deepest segments and hides the top ones instead of letting
    // the deepest label run out of the frame (that used to clip glyphs mid-stroke).
    int avail = static_cast<int>(m_pBreadcrumb->GetWidth());
    if (avail <= 8) avail = DpiScale(700);      // layout not ready yet: assume roomy
    const int ellipsisW = DpiScale(20);
    auto rowWidth = [&](size_t first) {
        int t = (first > 0) ? (ellipsisW + sepW) : 0;
        for (size_t i = first; i < segs.size(); ++i) {
            if (t > 0) t += sepW;
            t += segs[i].w;
        }
        return t;
    };
    size_t first = 0;
    while (first + 1 < segs.size() && rowWidth(first) > avail)
        ++first;

    std::vector<Seg> shown;
    if (first > 0)
        shown.push_back({ L"…", segs[first - 1].path, ellipsisW });
    for (size_t i = first; i < segs.size(); ++i)
        shown.push_back(segs[i]);

    // Shrink the tail when the deepest name alone is wider than what is left.
    {
        int used = 0;
        for (size_t i = 0; i < shown.size(); ++i)
            used += shown[i].w + (i ? sepW : 0);
        if (!shown.empty() && used > avail) {
            shown.back().w -= (used - avail);
            if (shown.back().w < DpiScale(48)) shown.back().w = DpiScale(48);
        }
    }

    // ---- 4. Emit -----------------------------------------------------------
    for (size_t i = 0; i < shown.size(); ++i) {
        const bool isLast = (i + 1 == shown.size());
        auto* btn = new CButtonUI;
        btn->SetText(shown[i].label.c_str());
        btn->SetUserData(shown[i].path.c_str());
        btn->SetName(shown[i].label == L"…" ? _T("bc_more") : _T("bc_seg"));
        btn->SetFixedHeight(segH);
        btn->SetFixedWidth(shown[i].w);
        btn->SetAttribute(_T("padding"), _T("0,0,0,0"));
        {
            CDuiString tp;
            tp.Format(_T("%d,0,%d,0"), padX, padX);
            btn->SetAttribute(_T("textpadding"), tp);
        }
        btn->SetAttribute(_T("align"), _T("center"));
        btn->SetAttribute(_T("valign"), _T("vcenter"));
        btn->SetAttribute(_T("endellipsis"), _T("true"));
        btn->SetAttribute(_T("font"), _T("0"));
        btn->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
        btn->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
        btn->SetAttribute(_T("bordersize"), _T("0"));
        btn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorHover);
        btn->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorPressed);
        btn->SetAttribute(_T("textcolor"),
            isLast ? UiTokens::ColorTextPrimary : UiTokens::ColorTextTabIdle);
        m_pBreadcrumb->Add(btn);
        if (!isLast) {
            auto* sep = new CLabelUI;
            sep->SetText(_T(" › "));
            sep->SetFixedWidth(sepW);
            sep->SetFixedHeight(segH);
            sep->SetAttribute(_T("textcolor"), UiTokens::ColorTextMuted);
            sep->SetAttribute(_T("font"), _T("0"));
            sep->SetAttribute(_T("align"), _T("center"));
            sep->SetAttribute(_T("valign"), _T("vcenter"));
            m_pBreadcrumb->Add(sep);
        }
    }

    // Click empty trailing area -> editable address (Explorer-style)
    if (!m_addressEditMode) {
        auto* filler = new CButtonUI;
        filler->SetName(_T("bc_edit"));
        filler->SetText(_T(""));
        filler->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
        filler->SetAttribute(_T("hotbkcolor"), UiTokens::ColorTransparent);
        filler->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorTransparent);
        filler->SetAttribute(_T("bordersize"), _T("0"));
        filler->SetFixedHeight(segH);
        m_pBreadcrumb->Add(filler);
    }

    m_pBreadcrumb->NeedUpdate();
}

void CMainWnd::OnBreadcrumbSegmentClick(CControlUI* btn)
{
    if (!btn) return;
    CDuiString ud = btn->GetUserData();
    if (ud.IsEmpty()) return;
    NavigateTo(ud.GetData(), true);
}
