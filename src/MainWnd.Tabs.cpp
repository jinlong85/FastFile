// FastFile - tab strip, session persistence, per-folder view memory
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"
#include "FavoriteStarUI.h"
#include "SessionStore.h"

// ---- Tabs ----------------------------------------------------------------

void CMainWnd::InitTabs()
{
    m_tabs.clear();
    m_activeTab = -1;
    if (m_pTabStrip) {
        m_pTabStrip->SetBarHeight(m_settings.tabHeight);
        m_pTabStrip->SetWidthPercent(m_settings.tabWidthPercent);
        m_pTabStrip->SetMetrics(static_cast<int>(m_dpi));
        m_pTabStrip->Clear();
    }
}

std::wstring CMainWnd::TabTitleForPath(const std::wstring& path) const
{
    if (IsThisPcPath(path))
        return L"此电脑";
    if (FileTagManager::IsTagViewPath(path))
        return FileTagManager::TagViewDisplayName(path);
    std::wstring leaf = GetShellDisplayName(path);
    if (path.size() <= 3 && path.size() >= 2 && path[1] == L':') return path.substr(0, 2);
    if (leaf.empty()) leaf = GetLeafName(path);
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
    m_pTabStrip->SetBarHeight(m_settings.tabHeight);
        m_pTabStrip->SetWidthPercent(m_settings.tabWidthPercent);
        m_pTabStrip->SetMetrics(static_cast<int>(m_dpi));
    const int tabWidth=MulDiv(120,m_settings.tabWidthPercent,100);
    m_pTabStrip->SetTabWidthRange(tabWidth,tabWidth,tabWidth);
    m_pTabStrip->Clear();
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        const std::wstring title = TabTitleForPath(m_tabs[i].path);
        std::wstring icon = IsThisPcPath(m_tabs[i].path)
            ? GetStockIconBmp(SIID_DESKTOPPC, tabIconPx)
            : FileTagManager::IsTagViewPath(m_tabs[i].path)
                ? GetStockIconBmp(SIID_FIND, tabIconPx)
                : GetShellIconBmp(m_tabs[i].path, true, tabIconPx);
        if (icon.empty()) icon = GetStockIconBmp(SIID_FOLDER, tabIconPx);
        m_pTabStrip->Add(m_tabs[i].path, title, icon, tabIconPx, i == m_activeTab, m_tabs[i].isPinned);
        m_tabs[i].button = nullptr;
    }
    m_pTabStrip->NeedParentUpdate();
    m_updatingTabs = false;
}

int CMainWnd::FindTabForPath(const std::wstring& path) const
{
    const auto target = (IsThisPcPath(path) || path == L"此电脑") ? std::wstring(kThisPcPath)
        : FileTagManager::IsTagViewPath(path) ? path : NormalizePath(path);
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        const auto candidate = (IsThisPcPath(m_tabs[i].path) || FileTagManager::IsTagViewPath(m_tabs[i].path))
            ? m_tabs[i].path : NormalizePath(m_tabs[i].path);
        if (PathEquals(candidate, target)) return i;
    }
    return -1;
}

void CMainWnd::AddTab(const std::wstring& path, bool activate, bool forceNew)
{
    std::wstring target = path.empty() ? GetDefaultStartPath() : path;
    if (target == L"此电脑")
        target = kThisPcPath;
    else if (!IsThisPcPath(target) && !FileTagManager::IsTagViewPath(target)) {
        const std::wstring normalized = NormalizePath(target);
        if (!normalized.empty())
            target = normalized;
    }

    // All open-folder entry points share one directory / one tab, including + and Ctrl+T.
    const int existing = FindTabForPath(target);
    if (!forceNew && m_settings.reuseTabs && existing >= 0) {
        if (activate && existing != m_activeTab) ActivateTab(existing);
        return;
    }

    // Opening descendants continues the current browsing chain. Keep the exact
    // existing-target lookup above this so another open tab still wins.
    if (!forceNew && !m_openingExternalPaths && activate && m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()) &&
        !IsThisPcPath(target) && !IsThisPcPath(m_currentPath)) {
        std::wstring parent = NormalizePath(m_currentPath);
        if (!parent.empty()) {
            if (parent.back() != L'\\') parent.push_back(L'\\');
            if (target.size() > parent.size() &&
                _wcsnicmp(target.c_str(), parent.c_str(), parent.size()) == 0) {
                NavigateToNow(target, true);
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
    AddTab(NewTabTargetForSelection(), true);
}

// ---- CTabStripUI notifications -------------------------------------------

CControlUI* CMainWnd::CreateControl(LPCTSTR pstrClass)
{
    if (_tcsicmp(pstrClass, _T("TabStrip")) == 0)
        return new CTabStripUI;
    if (_tcsicmp(pstrClass, _T("FavoriteStar")) == 0)
        return new CFavoriteStarUI;
    if (_tcsicmp(pstrClass, _T("PreviewPathLabel")) == 0)
        return new CPreviewPathLabelUI;
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
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        m_tabs[m_activeTab].path = m_currentPath;
        m_tabs[m_activeTab].searchFilter = m_searchFilter;
    }
    for (int i = static_cast<int>(m_tabs.size()) - 1; i >= 0; --i) {
        if (i == keepIndex) continue;
        if (m_tabs[i].isPinned) continue; // 保护：绝不关闭已固定的标签！
        if (rightSideOnly && i < keepIndex) continue;
        ClosedTabInfo closed;
        closed.path = m_tabs[i].path;
        closed.searchFilter = m_tabs[i].searchFilter;
        closed.backStack = m_tabs[i].backStack;
        closed.forwardStack = m_tabs[i].forwardStack;
        closed.isPinned = m_tabs[i].isPinned;
        m_closedTabsHistory.push_back(std::move(closed));
        m_tabs.erase(m_tabs.begin() + i);
        if (i < keepIndex) --keepIndex;
    }
    if (m_closedTabsHistory.size() > 20) {
        m_closedTabsHistory.erase(m_closedTabsHistory.begin(), m_closedTabsHistory.begin() + (m_closedTabsHistory.size() - 20));
    }
    m_activeTab = -1;
    ActivateTab(keepIndex);
    RebuildTabStrip();
    SaveSession();
}

void CMainWnd::CloseLeftTabs(int keepIndex)
{
    if (keepIndex < 0 || keepIndex >= static_cast<int>(m_tabs.size())) return;
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        m_tabs[m_activeTab].path = m_currentPath;
        m_tabs[m_activeTab].searchFilter = m_searchFilter;
    }
    for (int i = keepIndex - 1; i >= 0; --i) {
        if (m_tabs[i].isPinned) continue; // 保护：绝不关闭已固定的标签！
        ClosedTabInfo closed;
        closed.path = m_tabs[i].path;
        closed.searchFilter = m_tabs[i].searchFilter;
        closed.backStack = m_tabs[i].backStack;
        closed.forwardStack = m_tabs[i].forwardStack;
        closed.isPinned = m_tabs[i].isPinned;
        m_closedTabsHistory.push_back(std::move(closed));
        m_tabs.erase(m_tabs.begin() + i);
        --keepIndex;
    }
    if (m_closedTabsHistory.size() > 20) {
        m_closedTabsHistory.erase(m_closedTabsHistory.begin(), m_closedTabsHistory.begin() + (m_closedTabsHistory.size() - 20));
    }
    m_activeTab = -1;
    ActivateTab(keepIndex);
    RebuildTabStrip();
    SaveSession();
}

void CMainWnd::DuplicateTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        m_tabs[m_activeTab].path = m_currentPath;
        m_tabs[m_activeTab].searchFilter = m_searchFilter;
    }
    TabInfo copy = m_tabs[index];
    copy.button = nullptr;
    const int insertIdx = index + 1;
    m_tabs.insert(m_tabs.begin() + insertIdx, std::move(copy));
    m_activeTab = -1;
    RebuildTabStrip();
    ActivateTab(insertIdx);
    SaveSession();
}

void CMainWnd::TogglePinTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        m_tabs[m_activeTab].path = m_currentPath;
        m_tabs[m_activeTab].searchFilter = m_searchFilter;
    }
    bool wasPinned = m_tabs[index].isPinned;
    TabInfo tab = m_tabs[index];
    tab.isPinned = !wasPinned;
    m_tabs.erase(m_tabs.begin() + index);

    int targetIdx = 0;
    int pinnedCount = 0;
    for (const auto& t : m_tabs) {
        if (t.isPinned) pinnedCount++;
    }
    targetIdx = pinnedCount; // 如果固定，放在已有固定标签末尾；若取消固定，放在固定标签后第一个
    if (targetIdx > static_cast<int>(m_tabs.size())) targetIdx = static_cast<int>(m_tabs.size());
    m_tabs.insert(m_tabs.begin() + targetIdx, std::move(tab));
    m_activeTab = -1;
    RebuildTabStrip();
    ActivateTab(targetIdx);
    SaveSession();
}

void CMainWnd::RestoreClosedTab()
{
    if (m_closedTabsHistory.empty()) {
        UpdateStatus(_T("没有最近关闭的标签页"));
        return;
    }
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        m_tabs[m_activeTab].path = m_currentPath;
        m_tabs[m_activeTab].searchFilter = m_searchFilter;
    }
    ClosedTabInfo info = m_closedTabsHistory.back();
    m_closedTabsHistory.pop_back();

    TabInfo tab;
    tab.path = info.path;
    tab.searchFilter = info.searchFilter;
    tab.backStack = info.backStack;
    tab.forwardStack = info.forwardStack;
    tab.isPinned = info.isPinned;

    int insertIdx = static_cast<int>(m_tabs.size());
    if (info.isPinned) {
        int pinnedCount = 0;
        for (const auto& t : m_tabs) {
            if (t.isPinned) pinnedCount++;
        }
        insertIdx = pinnedCount;
    }
    m_tabs.insert(m_tabs.begin() + insertIdx, std::move(tab));
    m_activeTab = -1;
    RebuildTabStrip();
    ActivateTab(insertIdx);
    std::wstring msg = L"已恢复标签页：" + TabTitleForPath(info.path);
    UpdateStatus(msg.c_str());
    SaveSession();
}

void CMainWnd::OpenPathInNewWindow(const std::wstring& path, POINT screenPt)
{
    // "--new-window" bypasses the single-instance forwarding in main.cpp so the clone really
    // becomes its own HWND.
    wchar_t exe[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --new-window";
    if (!path.empty())
        cmd += L" \"" + path + (IsThisPcPath(path) ? L"" : L"\\.") + L"\"";
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
    enum {
        kClose = 1,
        kTogglePin,
        kDuplicate,
        kCloseLeft,
        kCloseRight,
        kCloseOthers,
        kRestoreClosed,
        kCopyPath,
        kNewWindow
    };
    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;

    const bool isPinned = m_tabs[index].isPinned;
    bool hasUnpinnedLeft = false;
    for (int i = 0; i < index; ++i) {
        if (!m_tabs[i].isPinned) { hasUnpinnedLeft = true; break; }
    }
    const bool hasRight = index + 1 < static_cast<int>(m_tabs.size());
    bool hasCloseableOthers = false;
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (i != index && !m_tabs[i].isPinned) { hasCloseableOthers = true; break; }
    }
    const bool canRestore = !m_closedTabsHistory.empty();

    ::AppendMenuW(menu, MF_STRING | (isPinned ? MF_GRAYED : 0), kClose, L"关闭标签页\tCtrl+W");
    ::AppendMenuW(menu, MF_STRING, kTogglePin, isPinned ? L"取消固定标签页" : L"固定标签页");
    ::AppendMenuW(menu, MF_STRING, kDuplicate, L"复制标签页");
    ::AppendMenuW(menu, MF_STRING | (hasUnpinnedLeft ? 0 : MF_GRAYED), kCloseLeft, L"关闭左侧标签页");
    ::AppendMenuW(menu, MF_STRING | (hasRight ? 0 : MF_GRAYED), kCloseRight, L"关闭右侧标签页");
    ::AppendMenuW(menu, MF_STRING | (hasCloseableOthers ? 0 : MF_GRAYED), kCloseOthers, L"关闭其他标签页");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING | (canRestore ? 0 : MF_GRAYED), kRestoreClosed, L"重新打开关闭的标签页\tCtrl+Shift+T");
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
    case kTogglePin:
        TogglePinTab(index);
        break;
    case kDuplicate:
        DuplicateTab(index);
        break;
    case kCloseLeft:
        CloseLeftTabs(index);
        break;
    case kCloseRight:
        CloseOtherTabs(index, true);
        break;
    case kCloseOthers:
        CloseOtherTabs(index, false);
        break;
    case kRestoreClosed:
        RestoreClosedTab();
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

void CMainWnd::OnTabStripEmptyContextMenu(POINT screenPt)
{
    enum { kNewTab = 1, kRestoreClosed };
    HMENU menu = ::CreatePopupMenu();
    if (!menu) return;
    const bool canRestore = !m_closedTabsHistory.empty();
    ::AppendMenuW(menu, MF_STRING, kNewTab, L"新建标签页\tCtrl+T");
    ::AppendMenuW(menu, MF_STRING | (canRestore ? 0 : MF_GRAYED), kRestoreClosed, L"重新打开关闭的标签页\tCtrl+Shift+T");
    const UINT cmd = ::TrackPopupMenuEx(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, m_hWnd, nullptr);
    ::DestroyMenu(menu);
    if (cmd == kNewTab) {
        OnNewTabRequested();
    } else if (cmd == kRestoreClosed) {
        RestoreClosedTab();
    }
}

// Modal confirmation for "close the window while several tabs are open". Uses the native
// task dialog so the buttons can read 关闭 / 取消 (a plain MessageBox can only offer
// 确定 / 取消) and so it inherits the app's window icon and DPI.
bool CMainWnd::ConfirmCloseWithMultipleTabs()
{
    if(!m_settings.confirmClose)return true;
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
    ClosedTabInfo closed;
    closed.path = m_tabs[index].path;
    closed.searchFilter = m_tabs[index].searchFilter;
    closed.backStack = m_tabs[index].backStack;
    closed.forwardStack = m_tabs[index].forwardStack;
    closed.isPinned = m_tabs[index].isPinned;
    m_closedTabsHistory.push_back(std::move(closed));
    if (m_closedTabsHistory.size() > 20) {
        m_closedTabsHistory.erase(m_closedTabsHistory.begin());
    }

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
    // Switching existing tabs must not recreate them: rebuilding progressively
    // clamps the strip's scroll position.
    // Structural changes (new/closed tabs) still require a model rebuild.
    if (m_pTabStrip && m_pTabStrip->GetCount() == static_cast<int>(m_tabs.size()))
        m_pTabStrip->SetActiveTab(index);
    else
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
    } else if (FileTagManager::IsTagViewPath(target)) {
        m_currentPath = target;
        if (m_pAddressEdit) m_pAddressEdit->SetText(FileTagManager::TagViewDisplayName(target).c_str());
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
    // Every folder remembers its own view mode; switching tabs (or opening a shortcut in a new
    // tab) has to apply it too, otherwise E:\软件 opened as whatever the previous tab used.
    {
        const ViewMode remembered = LoadFolderViewForPath(m_currentPath);
        if (m_viewMode != remembered) {
            m_viewMode = remembered;
            m_iconAnchor = -1;
            m_lastIconClickTile = nullptr;
            m_lastIconClickTick = 0;
        }
        UpdateViewModeButtons();
    }
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
    // A folder name changes its title and tooltip, so rebuild immediately
    // instead of waiting for a later tab activation to refresh the layout.
    RebuildTabStrip();
}

// ---- Session persist -----------------------------------------------------

std::wstring CMainWnd::GetSessionFilePath()
{
    return FastFileSession::SessionFilePath();
}

std::wstring CMainWnd::GetFolderViewsFilePath()
{
    return FastFileSession::FolderViewsFilePath();
}

std::wstring CMainWnd::NormalizeViewKey(const std::wstring& path)
{
    return FastFileSession::NormalizeViewKey(path, kThisPcPath);
}

CMainWnd::ViewMode CMainWnd::LoadFolderViewForPath(const std::wstring& path) const
{
    const ViewMode kDefault = static_cast<ViewMode>(m_settings.defaultView);
    if (!m_settings.rememberViews) return kDefault;
    const int mode = FastFileSession::LoadFolderView(
        GetFolderViewsFilePath(),
        NormalizeViewKey(path),
        static_cast<int>(kDefault),
        static_cast<int>(ViewMode::Content));
    return static_cast<ViewMode>(mode);
}

void CMainWnd::SaveFolderViewForPath(const std::wstring& path, ViewMode mode) const
{
    if (!m_settings.rememberViews) return;
    FastFileSession::SaveFolderView(
        GetFolderViewsFilePath(),
        NormalizeViewKey(path),
        static_cast<int>(mode),
        static_cast<int>(ViewMode::Content));
}

void CMainWnd::SaveSession() const
{
    SaveLeftNavSplitter();
    FastFileSession::SessionData data;
    data.activeTab = m_activeTab;
    data.viewMode = static_cast<int>(m_viewMode);
    data.recursive = IsRecursiveSearch();
    data.colWidthName = m_colWidthName;
    data.colWidthMTime = m_colWidthMTime;
    data.colWidthType = m_colWidthType;
    data.colWidthSize = m_colWidthSize;
    data.sortColumn = static_cast<int>(m_sortColumn);
    data.sortAscending = m_sortAscending;
    data.previewVisible = m_previewVisible;
    data.showHidden = m_showHidden;
    data.favoritesBarVisible = m_favoritesBarVisible;
    data.tabs.reserve(m_tabs.size());
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        FastFileSession::TabRecord rec;
        rec.path = (i == m_activeTab && !m_currentPath.empty()) ? m_currentPath : m_tabs[i].path;
        rec.searchFilter = (i == m_activeTab) ? m_searchFilter : m_tabs[i].searchFilter;
        rec.isPinned = m_tabs[i].isPinned;
        data.tabs.push_back(std::move(rec));
    }
    FastFileSession::SaveSession(GetSessionFilePath(), data);
}

bool CMainWnd::LoadSession()
{
    FastFileSession::SessionData data;
    data.viewMode = static_cast<int>(ViewMode::Tiles);
    data.colWidthName = m_colWidthName;
    data.colWidthMTime = m_colWidthMTime;
    data.colWidthType = m_colWidthType;
    data.colWidthSize = m_colWidthSize;
    data.sortColumn = static_cast<int>(m_sortColumn);
    data.sortAscending = m_sortAscending;
    data.previewVisible = m_previewVisible;
    data.showHidden = m_showHidden;
    data.favoritesBarVisible = m_favoritesBarVisible;

    if (!FastFileSession::LoadSession(GetSessionFilePath(), data))
        return false;

    m_colWidthName = data.colWidthName;
    m_colWidthMTime = data.colWidthMTime;
    m_colWidthType = data.colWidthType;
    m_colWidthSize = data.colWidthSize;
    m_sortColumn = static_cast<SortColumn>(data.sortColumn);
    m_sortAscending = data.sortAscending;
    m_previewVisible = data.previewVisible;
    m_showHidden = data.showHidden;
    m_favoritesBarVisible = data.favoritesBarVisible;

    int active = data.activeTab;
    int viewMode = data.viewMode;

    if (m_settings.startup != 0) {
        data.tabs.clear();
        const auto custom = m_settings.startup == 2 && m_startupOpenPaths.empty()
            ? ResolveFolderOpenTarget(m_settings.startupPath) : std::wstring();
        FastFileSession::TabRecord rec;
        rec.path = m_settings.startup == 2 && !custom.empty() ? custom : std::wstring(kThisPcPath);
        data.tabs.push_back(std::move(rec));
        active = 0;
    }
    if (GetFileAttributesW(FastFileSettings::FilePath().c_str()) != INVALID_FILE_ATTRIBUTES) {
        m_sortColumn = static_cast<SortColumn>(m_settings.sortColumn);
        m_sortAscending = m_settings.sortAscending;
        viewMode = m_settings.defaultView;
    }

    m_tabs.clear();
    m_activeTab = -1;
    for (const auto& rec : data.tabs) {
        if (rec.path.empty()) continue;
        TabInfo tab;
        tab.path = rec.path;
        tab.searchFilter = rec.searchFilter;
        tab.isPinned = rec.isPinned;
        m_tabs.push_back(std::move(tab));
    }
    if (m_tabs.empty())
        return false;

    if (viewMode >= 0 && viewMode <= static_cast<int>(ViewMode::Content))
        m_viewMode = static_cast<ViewMode>(viewMode);
    if (m_pChkRecursive)
        m_pChkRecursive->Selected(data.recursive);
    ApplyColumnWidths();
    SetPreviewVisible(m_previewVisible);
    if (m_pFavoritesBar)
        m_pFavoritesBar->SetVisible(m_favoritesBarVisible);
    UpdateHeaderSortIndicators();

    if (active < 0 || active >= static_cast<int>(m_tabs.size()))
        active = 0;
    if (m_startupOpenPaths.empty()) ActivateTab(active);
    else {
        // Restore the tab model, but let the external target own the first browse.
        m_activeTab = active;
        RebuildTabStrip();
    }
    RebuildBreadcrumb();
    // Startup preferences decide whether paths are restored or replaced above.
    UpdateStatus(_T("已就绪"));
    return true;
}
