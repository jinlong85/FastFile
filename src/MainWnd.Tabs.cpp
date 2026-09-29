// FastFile - tab strip, session persistence, per-folder view memory
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

// ---- Tabs ----------------------------------------------------------------

void CMainWnd::InitTabs()
{
    m_tabs.clear();
    m_activeTab = -1;
    if (m_pTabStrip) {
        m_pTabStrip->SetMetrics(static_cast<int>(m_dpi));
        m_pTabStrip->Clear();
    }
}

std::wstring CMainWnd::TabTitleForPath(const std::wstring& path) const
{
    if (IsThisPcPath(path))
        return L"此电脑";
    std::wstring leaf = GetLeafName(path);
    if (leaf.empty()) {
        if (path.size() >= 2 && path[1] == L':')
            return path.substr(0, 2);
        return L"标签";
    }
    return leaf;
}

void CMainWnd::RebuildTabStrip()
{
    if (!m_pTabStrip) return;
    m_updatingTabs = true;
    // The strip is a self-drawn control: hand it the model (path / title / shell icon) and it
    // sizes, hit-tests and paints the tabs itself (see TabStripUI.cpp).
    const int tabIconPx = DpiScale(UiTokens::TabIconPx);
    m_pTabStrip->SetMetrics(static_cast<int>(m_dpi));
    m_pTabStrip->SetMaxTabWidth(UiTokens::TabMaxW);
    m_pTabStrip->Clear();
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        const std::wstring title = TabTitleForPath(m_tabs[i].path);
        std::wstring icon = IsThisPcPath(m_tabs[i].path)
            ? GetStockIconBmp(SIID_DESKTOPPC, tabIconPx)
            : GetShellIconBmp(m_tabs[i].path, true, tabIconPx);
        if (icon.empty()) icon = GetStockIconBmp(SIID_FOLDER, tabIconPx);
        m_pTabStrip->Add(m_tabs[i].path, title, icon, tabIconPx, i == m_activeTab);
        m_tabs[i].button = nullptr;
    }
    m_pTabStrip->NeedParentUpdate();
    m_updatingTabs = false;
}

void CMainWnd::AddTab(const std::wstring& path, bool activate, bool allowDuplicate)
{
    std::wstring target = path.empty() ? GetDefaultStartPath() : path;
    if (target == L"此电脑")
        target = kThisPcPath;
    else if (!IsThisPcPath(target)) {
        const std::wstring normalized = NormalizePath(target);
        if (!normalized.empty())
            target = normalized;
    }

    // One directory has one tab.  This also applies to folders opened by another process,
    // so repeated clicks in Explorer simply bring the existing FastFile tab forward.
    // Ctrl+T / the "+" button pass allowDuplicate: a new tab is always what the user asked for.
    if (!allowDuplicate) {
        for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
            if (PathEquals(m_tabs[i].path, target)) {
                if (activate)
                    ActivateTab(i);
                return;
            }
        }
    }

    TabInfo tab;
    tab.path = target;
    m_tabs.push_back(tab);
    if (activate)
        ActivateTab(static_cast<int>(m_tabs.size()) - 1);
    else
        RebuildTabStrip();
}

std::wstring CMainWnd::NewTabTargetForSelection() const
{
    ClipboardItem selected;
    bool haveSelected = false;

    // The virtual details view owns its own selection/cursor.  Prefer its cursor only while
    // it remains selected: Ctrl-click can deliberately remove the cursor item from a range.
    if (m_viewMode == ViewMode::Details
        && m_detailsCur >= 0 && m_detailsCur < static_cast<int>(m_detailsEntries.size())
        && m_detailsCur < static_cast<int>(m_detailsSel.size()) && m_detailsSel[m_detailsCur]) {
        selected.path = m_detailsEntries[m_detailsCur].fullPath;
        selected.isDir = m_detailsEntries[m_detailsCur].isDir;
        haveSelected = true;
    }

    // Icon/list focus is the most recently clicked item, which is the intended target when
    // a multi-selection exists.  Fall back to the selected collection only if focus is gone.
    if (!haveSelected && IsTileViewMode() && m_pIconTiles) {
        CControlUI* focus = m_PaintManager.GetFocus();
        while (focus && focus->GetParent() != m_pIconTiles)
            focus = focus->GetParent();
        if (focus && (focus->GetTag() & 0x100) != 0 && !focus->GetUserData().IsEmpty()) {
            selected.path = focus->GetUserData().GetData();
            selected.isDir = (focus->GetTag() & 1) != 0;
            haveSelected = true;
        }
    }
    if (!haveSelected && m_viewMode != ViewMode::Details && m_pFileList) {
        const int current = m_pFileList->GetCurSel();
        CControlUI* row = current >= 0 ? m_pFileList->GetItemAt(current) : nullptr;
        IListItemUI* item = row ? static_cast<IListItemUI*>(row->GetInterface(DUI_CTR_ILISTITEM)) : nullptr;
        if (row && item && item->IsSelected() && !row->GetUserData().IsEmpty()) {
            selected.path = row->GetUserData().GetData();
            selected.isDir = row->GetTag() != 0;
            haveSelected = true;
        }
    }
    if (!haveSelected) {
        std::vector<ClipboardItem> items;
        CollectSelectedItems(items);
        if (!items.empty()) {
            selected = items.front();
            haveSelected = true;
        }
    }

    if (haveSelected) {
        if (selected.isDir) {
            const std::wstring folder = NormalizePath(selected.path);
            if (!folder.empty())
                return folder;
        } else {
            const std::wstring parent = ParentPath(selected.path);
            if (!parent.empty())
                return parent;
        }
    }
    return m_currentPath.empty() ? GetDefaultStartPath() : m_currentPath;
}

void CMainWnd::OnNewTabRequested()
{
    AddTab(NewTabTargetForSelection(), true, true);
}

// ---- CTabStripUI notifications -------------------------------------------

CControlUI* CMainWnd::CreateControl(LPCTSTR pstrClass)
{
    if (_tcsicmp(pstrClass, _T("TabStrip")) == 0)
        return new CTabStripUI;
    return nullptr;   // everything else goes through DuiLib's own factory
}

void CMainWnd::OnTabStripSelect(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()) || index == m_activeTab)
        return;
    ActivateTab(index);
}

void CMainWnd::OnTabStripClose(int index)
{
    CloseTab(index);
}

void CMainWnd::OnTabStripReorder(int from, int to)
{
    if (from < 0 || to < 0 || from == to) return;
    if (from >= static_cast<int>(m_tabs.size()) || to >= static_cast<int>(m_tabs.size())) return;
    // The strip already moved its own copy; mirror that on the real tab model.
    TabInfo moved = m_tabs[from];
    m_tabs.erase(m_tabs.begin() + from);
    m_tabs.insert(m_tabs.begin() + to, std::move(moved));
    if (m_activeTab == from) m_activeTab = to;
    else if (from < m_activeTab && to >= m_activeTab) --m_activeTab;
    else if (from > m_activeTab && to <= m_activeTab) ++m_activeTab;
    SaveSession();
}

void CMainWnd::OnTabStripDragOut(int index, POINT screenPt)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;
    OpenPathInNewWindow(m_tabs[index].path, screenPt);
}

void CMainWnd::OnTabStripAdd()
{
    OnNewTabRequested();
}

void CMainWnd::CloseOtherTabs(int keepIndex, bool rightSideOnly)
{
    if (keepIndex < 0 || keepIndex >= static_cast<int>(m_tabs.size())) return;
    for (int i = static_cast<int>(m_tabs.size()) - 1; i >= 0; --i) {
        if (i == keepIndex) continue;
        if (rightSideOnly && i < keepIndex) continue;
        m_tabs.erase(m_tabs.begin() + i);
        if (i < keepIndex) --keepIndex;
    }
    m_activeTab = -1;
    ActivateTab(keepIndex);
}

void CMainWnd::OpenPathInNewWindow(const std::wstring& path, POINT screenPt)
{
    // "--new-window" bypasses the single-instance forwarding in main.cpp so the clone really
    // becomes its own HWND.
    wchar_t exe[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --new-window";
    if (!path.empty())
        cmd += L" \"" + path + L"\"";
    // Hand the clone its geometry so a dragged-out tab opens exactly as big as this window,
    // at the drop point, without the parent having to race the new process' own layout pass.
    if ((screenPt.x != 0 || screenPt.y != 0) && m_hWnd) {
        RECT rc = {};
        ::GetWindowRect(m_hWnd, &rc);
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w > 0 && h > 0) {
            wchar_t geom[64] = {};
            swprintf_s(geom, L" --geometry=%d,%d,%d,%d", screenPt.x - 60, screenPt.y - 16, w, h);
            cmd += geom;
        }
    }
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (::CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
    }
}

void CMainWnd::OnTabStripContextMenu(int index, POINT screenPt)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;
    enum { kClose = 1, kCloseRight, kCloseOthers, kCopyPath, kNewWindow };
    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;
    const bool hasOthers = m_tabs.size() > 1;
    const bool hasRight = index + 1 < static_cast<int>(m_tabs.size());
    ::AppendMenuW(menu, MF_STRING, kClose, L"关闭标签页");
    ::AppendMenuW(menu, MF_STRING | (hasRight ? 0 : MF_GRAYED), kCloseRight, L"关闭右侧标签页");
    ::AppendMenuW(menu, MF_STRING | (hasOthers ? 0 : MF_GRAYED), kCloseOthers, L"关闭其他标签页");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kCopyPath, L"复制路径");
    ::AppendMenuW(menu, MF_STRING, kNewWindow, L"在新窗口打开");
    const UINT cmd = ::TrackPopupMenuEx(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, m_hWnd, nullptr);
    ::DestroyMenu(menu);
    switch (cmd) {
    case kClose:
        CloseTab(index);
        break;
    case kCloseRight:
        CloseOtherTabs(index, true);
        break;
    case kCloseOthers:
        CloseOtherTabs(index, false);
        break;
    case kCopyPath: {
        const std::wstring text = (m_tabs[index].path == kThisPcPath) ? L"此电脑" : m_tabs[index].path;
        ::OpenClipboard(m_hWnd);
        ::EmptyClipboard();
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (void* dst = ::GlobalLock(mem)) {
                memcpy(dst, text.c_str(), bytes);
                ::GlobalUnlock(mem);
                ::SetClipboardData(CF_UNICODETEXT, mem);
            }
        }
        ::CloseClipboard();
        UpdateStatus(_T("已复制路径"));
        break;
    }
    case kNewWindow:
        OpenPathInNewWindow(m_tabs[index].path, screenPt);
        break;
    default:
        break;
    }
}

// Modal confirmation for "close the window while several tabs are open". Uses the native
// task dialog so the buttons can read 关闭 / 取消 (a plain MessageBox can only offer
// 确定 / 取消) and so it inherits the app's window icon and DPI.
bool CMainWnd::ConfirmCloseWithMultipleTabs()
{
    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = m_hWnd;
    cfg.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION
        | TDF_SIZE_TO_CONTENT;
    cfg.dwCommonButtons = 0;
    cfg.pszWindowTitle = L"FastFile";
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.pszMainInstruction = L"当前打开了多个标签页，确认是否关闭";
    wchar_t content[128] = {};
    swprintf_s(content, L"关闭窗口会同时关闭全部 %u 个标签页。",
        static_cast<unsigned>(m_tabs.size()));
    cfg.pszContent = content;

    // First entry is the default (Enter) action; Cancel is listed second but focused, so a
    // stray Enter never closes the window by accident.
    const TASKDIALOG_BUTTON buttons[] = {
        { 1001, L"关闭" },
        { 1002, L"取消" },
    };
    cfg.pButtons = buttons;
    cfg.cButtons = ARRAYSIZE(buttons);
    cfg.nDefaultButton = 1002;

    int pressed = 0;
    const HRESULT hr = ::TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr);
    if (FAILED(hr))
        return true;   // no task dialog available: keep the previous close behaviour
    return pressed == 1001;
}

void CMainWnd::CloseTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;
    if (m_tabs.size() <= 1) {
        // Last tab: closing it closes the program (like Explorer's tabbed windows).
        // WM_CLOSE runs the normal shutdown path, so the session is saved on the way out.
        if (m_hWnd)
            ::PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
        return;
    }
    m_tabs.erase(m_tabs.begin() + index);
    int next = m_activeTab;
    if (index < m_activeTab)
        next = m_activeTab - 1;
    else if (index == m_activeTab)
        next = (std::min)(index, static_cast<int>(m_tabs.size()) - 1);
    m_activeTab = -1;
    ActivateTab(next);
}

void CMainWnd::ActivateTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;
    // persist current tab filter/path
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        m_tabs[m_activeTab].path = m_currentPath;
        m_tabs[m_activeTab].searchFilter = m_searchFilter;
    }
    m_activeTab = index;
    RebuildTabStrip();

    const TabInfo& tab = m_tabs[m_activeTab];
    m_searchFilter = tab.searchFilter;
    if (m_pSearchEdit)
        m_searchPlaceholder = false;
        m_pSearchEdit->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        m_pSearchEdit->SetText(m_searchFilter.c_str());
        if (m_searchFilter.empty())
            SetSearchPlaceholder(true);

    // Navigate without wiping the restored filter
    std::wstring target = tab.path;
    std::wstring savedFilter = m_searchFilter;
    if (IsThisPcPath(target) || target == L"此电脑") {
        m_currentPath = kThisPcPath;
        if (m_pAddressEdit) m_pAddressEdit->SetText(_T("此电脑"));
    } else {
        std::wstring norm = NormalizePath(target);
        if (norm.empty())
            norm = GetDefaultStartPath();
        m_currentPath = norm;
        if (m_pAddressEdit) m_pAddressEdit->SetText(m_currentPath.c_str());
    }
    m_searchFilter = savedFilter;
    if (m_pSearchEdit)
        m_searchPlaceholder = false;
        m_pSearchEdit->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        m_pSearchEdit->SetText(m_searchFilter.c_str());
        if (m_searchFilter.empty())
            SetSearchPlaceholder(true);
    RefreshListing();
    if (!m_suspendTreeSync)
        SyncTreeToPath(m_currentPath);
    // A tab switch changes the source path without going through NavigateToNow().
    // Refresh the path-derived UI here as well; otherwise the old breadcrumb and preview
    // can remain on screen while the new tab's file view has already been rendered.
    RebuildBreadcrumb();
    ClearPreview();
    UpdateFavoritesHighlight();
    UpdateNavButtons();
}

void CMainWnd::UpdateActiveTabPath(const std::wstring& path)
{
    if (m_activeTab < 0 || m_activeTab >= static_cast<int>(m_tabs.size()))
        return;
    m_tabs[m_activeTab].path = path;
    m_tabs[m_activeTab].searchFilter = m_searchFilter;
    // A folder name changes both text and measured width, so rebuild immediately
    // instead of waiting for a later tab activation to refresh the layout.
    RebuildTabStrip();
}

// ---- Session persist -----------------------------------------------------

std::wstring CMainWnd::GetSessionFilePath()
{
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir;
    if (n > 0 && n < MAX_PATH)
        dir = appdata;
    else
        dir = L".";
    dir += L"\\FastFile";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\session.ini";
}

std::wstring CMainWnd::GetFolderViewsFilePath()
{
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir;
    if (n > 0 && n < MAX_PATH)
        dir = appdata;
    else
        dir = L".";
    dir += L"\\FastFile";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\folder_views.ini";
}

std::wstring CMainWnd::NormalizeViewKey(const std::wstring& path)
{
    if (path.empty())
        return {};
    if (IsThisPcPath(path) || path == L"此电脑" || ::_wcsicmp(path.c_str(), L"This PC") == 0)
        return kThisPcPath;
    std::wstring key = NormalizePath(path);
    if (key.empty())
        return {};
    for (auto& ch : key)
        ch = static_cast<wchar_t>(towlower(ch));
    return key;
}

CMainWnd::ViewMode CMainWnd::LoadFolderViewForPath(const std::wstring& path) const
{
    const ViewMode kDefault = ViewMode::Tiles;
    std::wstring key = NormalizeViewKey(path);
    if (key.empty())
        return kDefault;

    std::wstring file = GetFolderViewsFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, file.c_str(), L"rb") != 0 || !fp)
        return kDefault;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 4) { fclose(fp); return kDefault; }
    std::wstring content;
    content.resize(sz / sizeof(wchar_t));
    fread(&content[0], 1, sz, fp);
    fclose(fp);
    if (!content.empty() && content[0] == 0xFEFF)
        content.erase(content.begin());

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = content.size();
        std::wstring line = content.substr(pos, eol - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        pos = eol + 1;
        if (line.empty() || line[0] == L'[' || line[0] == L';') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = line.substr(0, eq);
        std::wstring v = line.substr(eq + 1);
        if (::_wcsicmp(k.c_str(), key.c_str()) != 0) continue;
        int mode = _wtoi(v.c_str());
        if (mode >= 0 && mode <= static_cast<int>(ViewMode::Tiles))
            return static_cast<ViewMode>(mode);
        break;
    }
    return kDefault;
}

void CMainWnd::SaveFolderViewForPath(const std::wstring& path, ViewMode mode) const
{
    std::wstring key = NormalizeViewKey(path);
    if (key.empty())
        return;

    std::wstring file = GetFolderViewsFilePath();
    std::map<std::wstring, int> entries;

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, file.c_str(), L"rb") == 0 && fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz >= 4) {
            std::wstring content;
            content.resize(sz / sizeof(wchar_t));
            fread(&content[0], 1, sz, fp);
            if (!content.empty() && content[0] == 0xFEFF)
                content.erase(content.begin());
            size_t pos = 0;
            while (pos < content.size()) {
                size_t eol = content.find(L'\n', pos);
                if (eol == std::wstring::npos) eol = content.size();
                std::wstring line = content.substr(pos, eol - pos);
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                pos = eol + 1;
                if (line.empty() || line[0] == L'[' || line[0] == L';') continue;
                size_t eq = line.find(L'=');
                if (eq == std::wstring::npos) continue;
                std::wstring k = line.substr(0, eq);
                int v = _wtoi(line.substr(eq + 1).c_str());
                if (!k.empty() && v >= 0 && v <= static_cast<int>(ViewMode::Tiles))
                    entries[k] = v;
            }
        }
        fclose(fp);
    }

    entries[key] = static_cast<int>(mode);

    if (_wfopen_s(&fp, file.c_str(), L"wb") != 0 || !fp)
        return;
    unsigned char bom[2] = { 0xFF, 0xFE };
    fwrite(bom, 1, 2, fp);
    auto writeLine = [&](const std::wstring& s) {
        fwrite(s.c_str(), sizeof(wchar_t), s.size(), fp);
        wchar_t nl = L'\n';
        fwrite(&nl, sizeof(wchar_t), 1, fp);
    };
    writeLine(L"[FolderViews]");
    for (const auto& kv : entries) {
        wchar_t buf[32];
        swprintf_s(buf, L"=%d", kv.second);
        writeLine(kv.first + buf);
    }
    fclose(fp);
}

void CMainWnd::SaveSession() const
{
    SaveLeftNavSplitter();
    // Persist current tab path/filter first
    // (const_cast not needed — write from copies)
    std::wstring path = GetSessionFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || !fp)
        return;
    // UTF-16 LE BOM
    unsigned char bom[2] = { 0xFF, 0xFE };
    fwrite(bom, 1, 2, fp);

    auto writeLine = [&](const std::wstring& s) {
        fwrite(s.c_str(), sizeof(wchar_t), s.size(), fp);
        wchar_t nl = L'\n';
        fwrite(&nl, sizeof(wchar_t), 1, fp);
    };

    writeLine(L"[Session]");
    {
        wchar_t buf[128];
        swprintf_s(buf, L"ActiveTab=%d", m_activeTab);
        writeLine(buf);
        swprintf_s(buf, L"ViewMode=%d", static_cast<int>(m_viewMode));
        writeLine(buf);
        swprintf_s(buf, L"Recursive=%d", IsRecursiveSearch() ? 1 : 0);
        writeLine(buf);
        swprintf_s(buf, L"ColName=%d", m_colWidthName);
        writeLine(buf);
        swprintf_s(buf, L"ColMTime=%d", m_colWidthMTime);
        writeLine(buf);
        swprintf_s(buf, L"ColType=%d", m_colWidthType);
        writeLine(buf);
        swprintf_s(buf, L"ColSize=%d", m_colWidthSize);
        writeLine(buf);
        swprintf_s(buf, L"SortCol=%d", static_cast<int>(m_sortColumn));
        writeLine(buf);
        swprintf_s(buf, L"SortAsc=%d", m_sortAscending ? 1 : 0);
        writeLine(buf);
        swprintf_s(buf, L"Preview=%d", m_previewVisible ? 1 : 0);
        writeLine(buf);
        swprintf_s(buf, L"ShowHidden=%d", m_showHidden ? 1 : 0);
        writeLine(buf);
        swprintf_s(buf, L"FavoritesBar=%d", m_favoritesBarVisible ? 1 : 0);
        writeLine(buf);
    }
    writeLine(L"[Tabs]");
    {
        wchar_t buf[64];
        swprintf_s(buf, L"Count=%d", static_cast<int>(m_tabs.size()));
        writeLine(buf);
    }
    for (int i = 0; i < (int)m_tabs.size(); ++i) {
        std::wstring p = m_tabs[i].path;
        if (i == m_activeTab && !m_currentPath.empty())
            p = m_currentPath;
        wchar_t key[64];
        swprintf_s(key, L"Path%d=", i);
        writeLine(std::wstring(key) + p);
        swprintf_s(key, L"Filter%d=", i);
        writeLine(std::wstring(key) + ((i == m_activeTab) ? m_searchFilter : m_tabs[i].searchFilter));
    }
    fclose(fp);
}

bool CMainWnd::LoadSession()
{
    std::wstring path = GetSessionFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp)
        return false;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 4) { fclose(fp); return false; }
    std::wstring content;
    content.resize(sz / sizeof(wchar_t));
    fread(&content[0], 1, sz, fp);
    fclose(fp);
    if (!content.empty() && content[0] == 0xFEFF)
        content.erase(content.begin());

    int active = 0;
    int viewMode = static_cast<int>(ViewMode::Tiles);
    int recursive = 0;
    int count = 0;
    std::map<int, std::wstring> paths;
    std::map<int, std::wstring> filters;

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = content.size();
        std::wstring line = content.substr(pos, eol - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        pos = eol + 1;
        if (line.empty() || line[0] == L'[' || line[0] == L';') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq);
        std::wstring val = line.substr(eq + 1);
        if (key == L"ActiveTab") active = _wtoi(val.c_str());
        else if (key == L"ViewMode") viewMode = _wtoi(val.c_str());
        else if (key == L"Recursive") recursive = _wtoi(val.c_str());
        else if (key == L"ColName") m_colWidthName = (std::max)(60, _wtoi(val.c_str()));
        else if (key == L"ColMTime") m_colWidthMTime = (std::max)(50, _wtoi(val.c_str()));
        else if (key == L"ColType") m_colWidthType = (std::max)(50, _wtoi(val.c_str()));
        else if (key == L"ColSize") m_colWidthSize = (std::max)(50, _wtoi(val.c_str()));
        else if (key == L"SortCol") {
            int sc = _wtoi(val.c_str());
            if (sc >= 0 && sc <= 3) m_sortColumn = static_cast<SortColumn>(sc);
        }
        else if (key == L"SortAsc") m_sortAscending = (_wtoi(val.c_str()) != 0);
        else if (key == L"Preview") m_previewVisible = (_wtoi(val.c_str()) != 0);
        else if (key == L"ShowHidden") m_showHidden = (_wtoi(val.c_str()) != 0);
        else if (key == L"FavoritesBar") m_favoritesBarVisible = (_wtoi(val.c_str()) != 0);
        else if (key == L"Count") count = _wtoi(val.c_str());
        else if (key.size() > 4 && key.compare(0, 4, L"Path") == 0)
            paths[_wtoi(key.c_str() + 4)] = val;
        else if (key.size() > 6 && key.compare(0, 6, L"Filter") == 0)
            filters[_wtoi(key.c_str() + 6)] = val;
    }

    if (count <= 0 || paths.empty())
        return false;

    // User preference: always start in 此电脑, never restore the last folder. The rest of
    // the session (view mode, preview/recursive/favourites-bar flags, column widths) is
    // still restored below.
    paths.clear();
    filters.clear();
    paths[0] = kThisPcPath;
    count = 1;
    active = 0;

    m_tabs.clear();
    m_activeTab = -1;
    for (int i = 0; i < count; ++i) {
        auto it = paths.find(i);
        if (it == paths.end() || it->second.empty()) continue;
        TabInfo tab;
        tab.path = it->second;
        auto fit = filters.find(i);
        if (fit != filters.end())
            tab.searchFilter = fit->second;
        m_tabs.push_back(tab);
    }
    if (m_tabs.empty())
        return false;

    if (viewMode >= 0 && viewMode <= static_cast<int>(ViewMode::Tiles))
        m_viewMode = static_cast<ViewMode>(viewMode);
    if (m_pChkRecursive)
        m_pChkRecursive->Selected(recursive != 0);
    ApplyColumnWidths();
    SetPreviewVisible(m_previewVisible);
    if (m_pFavoritesBar)
        m_pFavoritesBar->SetVisible(m_favoritesBarVisible);
    UpdateHeaderSortIndicators();

    if (active < 0 || active >= (int)m_tabs.size())
        active = 0;
    ActivateTab(active);
    RebuildBreadcrumb();
    // Only the settings are restored — the folder is always 此电脑 (see above).
    UpdateStatus(_T("已就绪"));
    return true;
}
