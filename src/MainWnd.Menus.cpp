// FastFile - toolbar dropdown menus, Windows Shell context menus and native file verbs
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Declarations live in MainWnd.h.

#include "MainWndInternal.h"
#include "ShellPresentation.h"
#include "ShellMenuUtil.h"

void CMainWnd::ShowToolbarPopupMenu(CControlUI* anchor, HMENU hMenu)
{
    if (!anchor || !hMenu || !m_hWnd) {
        if (hMenu) ::DestroyMenu(hMenu);
        return;
    }
    RECT rc = anchor->GetPos();
    POINT pt = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &pt);
    ::TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        pt.x, pt.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
}

void CMainWnd::OnNewMenuClicked()
{
    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;
    ::AppendMenuW(hMenu, MF_STRING, 1, L"新建文件夹");
    CControlUI* anchor = m_PaintManager.FindControl(_T("btn_new"));
    if (!anchor) anchor = m_PaintManager.FindControl(_T("btn_newfolder"));

    if (!anchor || !m_hWnd) {
        ::DestroyMenu(hMenu);
        OnNewFolderClicked();
        return;
    }
    RECT rc = anchor->GetPos();
    POINT pt = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &pt);
    UINT cmd = ::TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
        pt.x, pt.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
    if (cmd == 1)
        OnNewFolderClicked();
}

void CMainWnd::OnSortMenuClicked()
{
    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;
    auto check = [&](SortColumn col) -> UINT {
        return (m_sortColumn == col) ? (MF_STRING | MF_CHECKED) : MF_STRING;
    };
    ::AppendMenuW(hMenu, check(SortColumn::Name), 1, L"名称");
    ::AppendMenuW(hMenu, check(SortColumn::Modified), 2, L"修改日期");
    ::AppendMenuW(hMenu, check(SortColumn::Type), 3, L"类型");
    ::AppendMenuW(hMenu, check(SortColumn::Size), 4, L"大小");
    ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(hMenu, m_sortAscending ? (MF_STRING | MF_CHECKED) : MF_STRING, 5, L"升序");
    ::AppendMenuW(hMenu, !m_sortAscending ? (MF_STRING | MF_CHECKED) : MF_STRING, 6, L"降序");

    CControlUI* anchor = m_PaintManager.FindControl(_T("btn_sort"));
    if (!anchor || !m_hWnd) {
        ::DestroyMenu(hMenu);
        return;
    }
    RECT rc = anchor->GetPos();
    POINT pt = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &pt);
    UINT cmd = ::TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
        pt.x, pt.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
    if (cmd == 0) return;
    if (cmd >= 1 && cmd <= 4) {
        SortColumn col = static_cast<SortColumn>(cmd - 1);
        if (m_sortColumn == col)
            m_sortAscending = !m_sortAscending;
        else {
            m_sortColumn = col;
            m_sortAscending = true;
        }
    } else if (cmd == 5) {
        m_sortAscending = true;
    } else if (cmd == 6) {
        m_sortAscending = false;
    }
    if (m_shellBrowser && m_shellBrowser->IsCreated() && m_shellBrowser->IsVisible()) {
        m_shellBrowser->SetSort(static_cast<int>(m_sortColumn), m_sortAscending);
        return;
    }
    SortListingCache();
    UpdateHeaderSortIndicators();
    RebuildCurrentViewFromCache();
}

void CMainWnd::OnViewMenuClicked()
{
    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;
    auto check = [&](ViewMode m) -> UINT {
        return (m_viewMode == m) ? (MF_STRING | MF_CHECKED) : MF_STRING;
    };
    ::AppendMenuW(hMenu, check(ViewMode::ExtraLargeIcons), 1, L"超大图标");
    ::AppendMenuW(hMenu, check(ViewMode::LargeIcons), 2, L"大图标");
    ::AppendMenuW(hMenu, check(ViewMode::MediumIcons), 3, L"中等图标");
    ::AppendMenuW(hMenu, check(ViewMode::SmallIcons), 9, L"小图标");
    ::AppendMenuW(hMenu, check(ViewMode::List), 4, L"列表");
    ::AppendMenuW(hMenu, check(ViewMode::Details), 5, L"详细信息");
    ::AppendMenuW(hMenu, check(ViewMode::Tiles), 6, L"平铺");
    ::AppendMenuW(hMenu, check(ViewMode::Content), 10, L"内容");
    ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(hMenu, m_previewVisible ? (MF_STRING | MF_CHECKED) : MF_STRING, 7, L"预览窗格");
    ::AppendMenuW(hMenu, m_favoritesBarVisible ? (MF_STRING | MF_CHECKED) : MF_STRING, 8, L"收藏栏");

    CControlUI* anchor = m_PaintManager.FindControl(_T("btn_view_menu"));
    if (!anchor || !m_hWnd) {
        ::DestroyMenu(hMenu);
        return;
    }
    RECT rc = anchor->GetPos();
    POINT pt = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &pt);
    UINT cmd = ::TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
        pt.x, pt.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
    switch (cmd) {
    case 1: SetViewMode(ViewMode::ExtraLargeIcons); break;
    case 2: SetViewMode(ViewMode::LargeIcons); break;
    case 3: SetViewMode(ViewMode::MediumIcons); break;
    case 4: SetViewMode(ViewMode::List); break;
    case 5: SetViewMode(ViewMode::Details); break;
    case 6: SetViewMode(ViewMode::Tiles); break;
    case 9: SetViewMode(ViewMode::SmallIcons); break;
    case 10: SetViewMode(ViewMode::Content); break;
    case 7:
        SetPreviewVisible(!m_previewVisible);
        if (m_previewVisible) UpdatePreviewForSelection();
        break;
    case 8:
        SetFavoritesBarVisible(!m_favoritesBarVisible);
        break;
    default: break;
    }
}

void CMainWnd::OnMoreMenuClicked()
{
    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;
    // Explorer-style "…": the app's own settings/config folder lives here instead of taking a
    // chip in the favourites strip.
    std::wstring cfgDir = GetFavoritesFilePath();
    const size_t slash = cfgDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        cfgDir.erase(slash);
    ::AppendMenuW(hMenu, MF_STRING, 6, L"\u6253\u5f00\u914d\u7f6e\u6587\u4ef6\u76ee\u5f55");
    ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(hMenu, MF_STRING, 1, L"刷新");
    ::AppendMenuW(hMenu, m_previewVisible ? (MF_STRING | MF_CHECKED) : MF_STRING,
        2, L"预览窗格");
    ::AppendMenuW(hMenu, m_showHidden ? (MF_STRING | MF_CHECKED) : MF_STRING,
        3, L"显示隐藏的项目");
    ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    // 撤销 / 重做 mirror the Windows undo history or internal fallback stack.
    ::AppendMenuW(hMenu, CanUndo() ? MF_STRING : (MF_STRING | MF_GRAYED),
        4, L"撤销\tCtrl+Z");
    ::AppendMenuW(hMenu, CanRedo() ? MF_STRING : (MF_STRING | MF_GRAYED),
        7, L"重做\tCtrl+Y");

    CControlUI* anchor = m_PaintManager.FindControl(_T("btn_more"));
    if (!anchor || !m_hWnd) {
        ::DestroyMenu(hMenu);
        return;
    }
    RECT rc = anchor->GetPos();
    POINT pt = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &pt);
    const UINT cmd = ::TrackPopupMenuEx(hMenu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
        pt.x, pt.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
    if (cmd == 1) {
        RefreshListing();
    } else if (cmd == 2) {
        SetPreviewVisible(!m_previewVisible);
        if (m_previewVisible) UpdatePreviewForSelection();
    } else if (cmd == 3) {
        ToggleShowHidden();
    } else if (cmd == 4) {
        OnUndo();
    } else if (cmd == 7) {
        OnRedo();

    } else if (cmd == 6) {
        if (!cfgDir.empty())
            AddTab(cfgDir, true);
    }
}

void CMainWnd::ForwardShellMenuMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* pResult, bool* handled)
{
    if (handled) *handled = false;
    if (uMsg != WM_INITMENUPOPUP && uMsg != WM_DRAWITEM && uMsg != WM_MEASUREITEM && uMsg != WM_MENUCHAR) return;
    // These messages also belong to owner-drawn controls. A Shell extension must
    // only receive menu items, even while a popup's nested message loop is active.
    DRAWITEMSTRUCT* draw = nullptr;
    if (uMsg == WM_DRAWITEM) {
        draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!draw || draw->CtlType != ODT_MENU) return;
    } else if (uMsg == WM_MEASUREITEM) {
        const auto* measure = reinterpret_cast<const MEASUREITEMSTRUCT*>(lParam);
        if (!measure || measure->CtlType != ODT_MENU) return;
    }
    // Isolate extension fonts, colors and clipping from subsequent native rows.
    const int saved = draw && draw->hDC ? SaveDC(draw->hDC) : 0;
    const LRESULT legacyResult = uMsg == WM_DRAWITEM || uMsg == WM_MEASUREITEM ? TRUE : 0;
    LRESULT result = legacyResult;
    HRESULT hr = E_NOINTERFACE;
    if (m_pCtxMenu3) hr = m_pCtxMenu3->HandleMenuMsg2(uMsg, wParam, lParam, &result);
    if ((!m_pCtxMenu3 || hr == E_NOTIMPL || hr == E_NOINTERFACE) && m_pCtxMenu2 && uMsg != WM_MENUCHAR) {
        hr = m_pCtxMenu2->HandleMenuMsg(uMsg, wParam, lParam);
        result = legacyResult; // IContextMenu2 has no LRESULT output.
    }
    if (saved) RestoreDC(draw->hDC, saved);
    if (hr == S_OK) {
        if (pResult) *pResult = result;
        if (handled) *handled = true;
    }
}

// ---- Windows Shell context menus ------------------------------------------------------
// Every Shell right-click menu FastFile shows is the unmodified Windows classic menu:
// QueryContextMenu with Explorer's flags, TrackPopupMenuEx, and the chosen command goes
// straight back to the same IContextMenu through CMINVOKECOMMANDINFOEX. Nothing is pruned,
// inserted, relabelled or intercepted, and no FastFile fallback menu exists. The hosted
// Windows view shows its own menus (FastFile never sees its WM_CONTEXTMENU).

UINT CMainWnd::ShellItemMenuFlags(bool shift, bool explore)
{
    UINT flags = CMF_NORMAL | CMF_ITEMMENU;
    if (explore) flags |= CMF_EXPLORE;          // navigation tree, like Explorer's folder pane
    if (shift) flags |= CMF_EXTENDEDVERBS;      // Shift+right-click / Shift+F10
    return flags;
}

UINT CMainWnd::ShellBackgroundMenuFlags(bool shift)
{
    return CMF_NORMAL | (shift ? CMF_EXTENDEDVERBS : 0u);
}

HRESULT CMainWnd::InvokeShellCommand(IContextMenu* menu, UINT offset, const wchar_t* verb,
    POINT ptInvoke, DWORD extraMask)
{
    if (!menu) return E_POINTER;
    CMINVOKECOMMANDINFOEX info = {};
    info.cbSize = sizeof(info);
    info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE | extraMask;
    if (::GetKeyState(VK_SHIFT) < 0) info.fMask |= CMIC_MASK_SHIFT_DOWN;
    if (::GetKeyState(VK_CONTROL) < 0) info.fMask |= CMIC_MASK_CONTROL_DOWN;
    info.hwnd = m_hWnd;
    std::string verbA;
    if (verb) {
        // String verb (e.g. CMDSTR_NEWFOLDER inside the lazily filled 新建 submenu).
        for (const wchar_t* c = verb; *c; ++c) verbA.push_back(static_cast<char>(*c)); // ASCII verbs
        info.lpVerb = verbA.c_str();
        info.lpVerbW = verb;
    } else {
        info.lpVerb = MAKEINTRESOURCEA(offset);
        info.lpVerbW = MAKEINTRESOURCEW(offset);
    }
    info.nShow = SW_SHOWNORMAL;
    info.ptInvoke = ptInvoke;
    m_lastNativeVerb.mask = info.fMask;
    m_lastNativeVerb.byOffset = verb == nullptr;
    if (!verb) {
        wchar_t name[128] = {};
        if (SUCCEEDED(menu->GetCommandString(offset, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(name), _countof(name))))
            m_lastNativeVerb.verb = name;
        else
            m_lastNativeVerb.verb.clear();
    }
    ++m_lastNativeVerb.count;
    const HRESULT hr = m_nativeInvokeHook ? m_nativeInvokeHook(menu)
        : menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
    m_lastNativeVerb.hr = hr;
    return hr;
}

bool CMainWnd::TrackPopupShellMenu(IContextMenu* pMenu, HMENU hMenu, POINT ptScreen,
    UINT idCmdFirst, UINT idShellMax)
{
    if (!pMenu || !hMenu)
        return false;
    // Hold IContextMenu2/3 so owner-draw + cascading submenus work during TrackPopupMenu.
    // Do NOT use TPM_NONOTIFY - Shell needs WM_INITMENUPOPUP / DRAWITEM / MEASUREITEM.
    m_pCtxMenu = pMenu;
    m_pCtxMenu2 = nullptr;
    m_pCtxMenu3 = nullptr;
    pMenu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void**>(&m_pCtxMenu2));
    pMenu->QueryInterface(IID_IContextMenu3, reinterpret_cast<void**>(&m_pCtxMenu3));

    const UINT cmd = m_trackMenuHook ? m_trackMenuHook(pMenu, hMenu)
        : ::TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, ptScreen.x, ptScreen.y, m_hWnd, nullptr);

    IContextMenu2* pcm2 = m_pCtxMenu2;
    IContextMenu3* pcm3 = m_pCtxMenu3;
    m_pCtxMenu = nullptr;
    m_pCtxMenu2 = nullptr;
    m_pCtxMenu3 = nullptr;
    if (pcm3) pcm3->Release();
    if (pcm2) pcm2->Release();

    if (cmd < idCmdFirst || cmd >= idShellMax)
        return true; // cancelled
    // Windows runs the command itself (open, rename, delete, paste, properties, ...).
    if (SUCCEEDED(InvokeShellCommand(pMenu, cmd - idCmdFirst, nullptr, ptScreen))) {
        // Keep FastFile's own projections in step: pins/recent items, and the search list,
        // which is not a Shell view and does not follow change notifications.
        LoadQuickAccess();
        if (!m_searchFilter.empty()) RefreshListing();
    }
    return true;
}

bool CMainWnd::QueryShellMenu(IContextMenu* menu, UINT flags, HMENU* outPopup, UINT* outShellMax)
{
    if (!menu || !outPopup || !outShellMax) return false;
    *outPopup = nullptr; *outShellMax = 0;
    HMENU popup = ::CreatePopupMenu();
    if (!popup) return false;
    const UINT idCmdFirst = 1;
    const HRESULT hr = menu->QueryContextMenu(popup, 0, idCmdFirst, 0x7FFF, flags);
    if (FAILED(hr)) { ::DestroyMenu(popup); return false; }
    *outPopup = popup;
    *outShellMax = idCmdFirst + static_cast<UINT>(HRESULT_CODE(hr));
    return true;
}

bool CMainWnd::ShowQueriedShellMenu(IContextMenu* menu, UINT flags, POINT ptScreen)
{
    HMENU popup = nullptr;
    UINT shellMax = 0;
    if (!QueryShellMenu(menu, flags, &popup, &shellMax)) return false;
    m_lastShellMenuFlags = flags;
    TrackPopupShellMenu(menu, popup, ptScreen, 1, shellMax);
    ::DestroyMenu(popup);
    return true;
}

void CMainWnd::ShowBlankAreaContextMenu(POINT ptScreen)
{
    // Background right-clicks are Shell-owned in every view; on failure only report it.
    if (!ShowShellBackgroundContextMenu(m_currentPath, ptScreen))
        UpdateStatus(_T("无法显示 Windows 文件夹菜单"));
}

void CMainWnd::ShowTreeContextMenu(CTreeNodeUI* node, POINT ptScreen)
{
    if (!node) return;
    CDuiString ud = node->GetUserData();
    if (ud.IsEmpty()) return;
    const std::wstring path = ud.GetData();
    if (path == kPendingMarker) return; // "loading" placeholder: not a Shell item
    const bool shown = IsThisPcPath(path) ? ShowThisPcContextMenu(ptScreen, true)
        : ShowShellContextMenu({ NormalizePath(path) }, ptScreen, true);
    if (!shown) UpdateStatus(_T("无法显示 Windows 右键菜单"));
}

bool CMainWnd::ShellBrowserShowsFolder(const std::wstring& folderPath) const
{
    if (!m_shellBrowser || !m_shellBrowser->IsCreated() || !m_shellBrowser->IsVisible()) return false;
    const std::wstring current = m_shellBrowser->CurrentPath();
    if (folderPath.empty() || IsThisPcPath(folderPath)) return IsThisPcPath(current);
    return !IsThisPcPath(current) && PathEquals(current, folderPath);
}

// Folder-background menu object. The visible ExplorerBrowser view supplies exactly the menu
// Explorer shows (IShellView::GetItemObject(SVGIO_BACKGROUND)); folders that are not on
// screen (search results) use the folder's own IShellFolder::CreateViewObject menu.
bool CMainWnd::CreateShellBackgroundContextMenu(const std::wstring& folderPath, IContextMenu** outMenu,
    bool* outFromView)
{
    if (!outMenu) return false;
    *outMenu = nullptr;
    if (outFromView) *outFromView = false;
    if (ShellBrowserShowsFolder(folderPath)
        && SUCCEEDED(m_shellBrowser->CreateBackgroundContextMenu(outMenu)) && *outMenu) {
        if (outFromView) *outFromView = true;
        return true;
    }
    PIDLIST_ABSOLUTE pidlFolder = nullptr;
    SFGAOF sfgao = 0;
    HRESULT hr = (folderPath.empty() || IsThisPcPath(folderPath))
        ? ::SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &pidlFolder)
        : ::SHParseDisplayName(folderPath.c_str(), nullptr, &pidlFolder, 0, &sfgao);
    if (FAILED(hr) || !pidlFolder) return false;
    IShellFolder* pFolder = nullptr;
    hr = ::SHBindToObject(nullptr, pidlFolder, nullptr, IID_PPV_ARGS(&pFolder));
    ::CoTaskMemFree(pidlFolder);
    if (FAILED(hr) || !pFolder) return false;
    hr = pFolder->CreateViewObject(m_hWnd, IID_PPV_ARGS(outMenu));
    pFolder->Release();
    if (FAILED(hr)) *outMenu = nullptr;
    return *outMenu != nullptr;
}

bool CMainWnd::BuildShellBackgroundMenu(const std::wstring& folderPath, IContextMenu** outMenu,
    HMENU* outPopup, UINT* outShellMax, bool* outFromView)
{
    if (!outMenu || !outPopup || !outShellMax) return false;
    *outMenu = nullptr; *outPopup = nullptr; *outShellMax = 0;
    IContextMenu* pMenu = nullptr;
    if (!CreateShellBackgroundContextMenu(folderPath, &pMenu, outFromView)) return false;
    if (!QueryShellMenu(pMenu, ShellBackgroundMenuFlags(::GetKeyState(VK_SHIFT) < 0), outPopup, outShellMax)) {
        pMenu->Release();
        return false;
    }
    *outMenu = pMenu;
    return true;
}

bool CMainWnd::ShowShellBackgroundContextMenu(const std::wstring& folderPath, POINT ptScreen)
{
    IContextMenu* pMenu = nullptr;
    if (!CreateShellBackgroundContextMenu(folderPath, &pMenu, nullptr)) return false;
    const bool shown = ShowQueriedShellMenu(pMenu, ShellBackgroundMenuFlags(::GetKeyState(VK_SHIFT) < 0), ptScreen);
    pMenu->Release();
    return shown;
}

void CMainWnd::ShowItemContextMenu(CControlUI* /*pItem*/, POINT ptScreen)
{
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.empty()) {
        ShowBlankAreaContextMenu(ptScreen);
        return;
    }
    std::vector<std::wstring> paths;
    paths.reserve(items.size());
    for (const auto& it : items)
        paths.push_back(it.path);
    if (!ShowShellContextMenu(paths, ptScreen))
        UpdateStatus(_T("无法显示 Windows 右键菜单"));
}

// Menu key / Shift+F10 outside the hosted view (the view handles its own).
void CMainWnd::ShowKeyboardContextMenu()
{
    RECT bounds = m_pFileList ? m_pFileList->GetPos() : RECT{0, 0, 100, 100};
    POINT point{bounds.left + 20, bounds.top + 40};
    if (IsTreeKeyboardFocus() && m_pDirTree) {
        CControlUI* selected = m_pDirTree->GetItemAt(m_pDirTree->GetCurSel());
        if (auto* node = selected ? static_cast<CTreeNodeUI*>(selected->GetInterface(DUI_CTR_TREENODE)) : nullptr) {
            const RECT rc = node->GetPos();
            point = {rc.left + 24, rc.bottom};
            ::ClientToScreen(m_hWnd, &point);
            ShowTreeContextMenu(node, point);
            return;
        }
    }
    ::ClientToScreen(m_hWnd, &point);
    ShowItemContextMenu(nullptr, point);
}

bool CMainWnd::ShowShellContextMenu(const std::vector<std::wstring>& paths, POINT ptScreen, bool explore)
{
    IContextMenu* pMenu = nullptr;
    if (!CreateShellItemContextMenu(paths, &pMenu)) return false;
    m_shellMenuPaths = paths;
    const bool shown = ShowQueriedShellMenu(pMenu, ShellItemMenuFlags(::GetKeyState(VK_SHIFT) < 0, explore), ptScreen);
    m_shellMenuPaths.clear();
    pMenu->Release();
    return shown;
}

bool CMainWnd::ShowPidlContextMenu(PCIDLIST_ABSOLUTE item, POINT ptScreen, bool explore)
{
    IContextMenu* pMenu = nullptr;
    if (!CreatePidlContextMenu(item, &pMenu)) return false;
    const bool shown = ShowQueriedShellMenu(pMenu, ShellItemMenuFlags(::GetKeyState(VK_SHIFT) < 0, explore), ptScreen);
    pMenu->Release();
    return shown;
}

bool CMainWnd::ShowThisPcContextMenu(POINT ptScreen, bool explore)
{
    PIDLIST_ABSOLUTE computer = nullptr;
    if (FAILED(::SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &computer)) || !computer)
        return false;
    const bool shown = ShowPidlContextMenu(computer, ptScreen, explore);
    ::CoTaskMemFree(computer);
    return shown;
}

bool CMainWnd::CreatePidlContextMenu(PCIDLIST_ABSOLUTE item, IContextMenu** outMenu)
{
    if (!outMenu) return false;
    *outMenu = nullptr;
    IShellFolder* parent = nullptr;
    PCUITEMID_CHILD child = nullptr;
    if (!item || FAILED(::SHBindToParent(item, IID_PPV_ARGS(&parent), &child)) || !parent) return false;
    const HRESULT hr = parent->GetUIObjectOf(m_hWnd, 1, &child, IID_IContextMenu, nullptr,
        reinterpret_cast<void**>(outMenu));
    parent->Release();
    if (FAILED(hr)) { *outMenu = nullptr; return false; }
    // Explorer sites navigation-pane menus on its frame; the hosted browser plays that role,
    // so the native 打开 browses in place (FastFile follows its navigation events).
    if (m_shellBrowser) m_shellBrowser->SiteContextMenu(*outMenu);
    return true;
}

bool CMainWnd::CreateShellItemContextMenu(const std::vector<std::wstring>& paths, IContextMenu** outMenu)
{
    if (!outMenu) return false;
    *outMenu = nullptr;
    if (paths.empty()) return false;
    if (paths.size() == 1 && IsThisPcPath(paths.front())) {
        PIDLIST_ABSOLUTE computer = nullptr;
        if (FAILED(::SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &computer)) || !computer)
            return false;
        const bool ok = CreatePidlContextMenu(computer, outMenu);
        ::CoTaskMemFree(computer);
        return ok;
    }

    // Use parent folder of first item; all items should share parent for multi
    std::wstring parent = ParentPath(paths[0]);
    if (parent.empty() && paths[0].size() >= 3 && paths[0][1] == L':')
        parent = paths[0].substr(0, 3);

    // Two ways to reach the Shell's item menu:
    //   1) bind the *parent* folder and parse the leaf names (ordinary files/folders);
    //   2) bind the desktop and parse the *full* path (drive roots in 此电脑, "shell:" items,
    //      anything whose parent folder cannot host it).
    IShellFolder* pFolder = nullptr;
    std::vector<PIDLIST_RELATIVE> pidlChildren;

    auto bindParentFolder = [&](const std::wstring& parentPath) -> bool {
        PIDLIST_ABSOLUTE pidlFolder = nullptr;
        SFGAOF sfgao = 0;
        if (FAILED(::SHParseDisplayName(parentPath.c_str(), nullptr, &pidlFolder, 0, &sfgao))
            || !pidlFolder)
            return false;
        IShellFolder* folder = nullptr;
        const HRESULT hrBind = ::SHBindToObject(nullptr, pidlFolder, nullptr, IID_IShellFolder,
            reinterpret_cast<void**>(&folder));
        ::CoTaskMemFree(pidlFolder);
        if (FAILED(hrBind) || !folder)
            return false;
        std::vector<PIDLIST_RELATIVE> kids;
        for (const auto& path : paths) {
            const std::wstring leaf = GetLeafName(path);
            PIDLIST_RELATIVE kid = nullptr;
            DWORD attrs = 0;
            if (leaf.empty()
                || FAILED(folder->ParseDisplayName(m_hWnd, nullptr,
                       const_cast<LPWSTR>(leaf.c_str()), nullptr, &kid, &attrs))
                || !kid) {
                for (auto* k : kids) ::CoTaskMemFree(k);
                folder->Release();
                return false;
            }
            kids.push_back(kid);
        }
        pFolder = folder;
        pidlChildren.swap(kids);
        return true;
    };

    auto bindDesktopFolder = [&]() -> bool {
        IShellFolder* desktop = nullptr;
        if (FAILED(::SHGetDesktopFolder(&desktop)) || !desktop)
            return false;
        std::vector<PIDLIST_RELATIVE> kids;
        for (const auto& path : paths) {
            PIDLIST_RELATIVE kid = nullptr;
            DWORD attrs = 0;
            if (FAILED(desktop->ParseDisplayName(m_hWnd, nullptr,
                    const_cast<LPWSTR>(path.c_str()), nullptr, &kid, &attrs))
                || !kid) {
                for (auto* k : kids) ::CoTaskMemFree(k);
                desktop->Release();
                return false;
            }
            kids.push_back(kid);
        }
        pFolder = desktop;
        pidlChildren.swap(kids);
        return true;
    };

    // A drive root has no usable parent ("I:" resolves as a drive-relative path), so it is
    // bound through the desktop, which resolves the real drive object.
    const bool driveRoot = (paths[0].size() >= 2 && paths[0][1] == L':'
        && (paths[0].size() == 2
            || (paths[0].size() == 3 && (paths[0][2] == L'\\' || paths[0][2] == L'/'))));
    if (paths.size() == 1 && driveRoot) {
        PIDLIST_ABSOLUTE drive = nullptr;
        if (SUCCEEDED(::SHParseDisplayName(paths[0].c_str(), nullptr, &drive, 0, nullptr)) && drive) {
            const bool ok = CreatePidlContextMenu(drive, outMenu);
            ::CoTaskMemFree(drive);
            if (ok) return true;
        }
    }

    bool ok = false;
    if (!driveRoot && !parent.empty())
        ok = bindParentFolder(parent);
    if (!ok)
        ok = bindDesktopFolder();
    if (!ok || pidlChildren.empty())
        return false;

    std::vector<LPCITEMIDLIST> pidlArgs(pidlChildren.begin(), pidlChildren.end());
    const HRESULT hrMenu = pFolder->GetUIObjectOf(m_hWnd, static_cast<UINT>(pidlArgs.size()),
        pidlArgs.data(), IID_IContextMenu, nullptr, reinterpret_cast<void**>(outMenu));
    for (auto* p : pidlChildren)
        ::CoTaskMemFree(p);
    pFolder->Release();
    if (FAILED(hrMenu)) { *outMenu = nullptr; return false; }
    if (m_shellBrowser) m_shellBrowser->SiteContextMenu(*outMenu);
    return true;
}

bool CMainWnd::BuildShellItemMenu(const std::vector<std::wstring>& paths, IContextMenu** outMenu,
    HMENU* outPopup, UINT* outShellMax, bool explore)
{
    if (!outMenu || !outPopup || !outShellMax) return false;
    *outMenu = nullptr; *outPopup = nullptr; *outShellMax = 0;
    IContextMenu* pMenu = nullptr;
    if (!CreateShellItemContextMenu(paths, &pMenu)) return false;
    // No CMF_CANRENAME: outside the hosted view there is no Shell view to host the native
    // in-place edit, so Windows' own 重命名 could not work here (Explorer passes it only
    // where it can). The hosted view adds it to its own menus itself.
    if (!QueryShellMenu(pMenu, ShellItemMenuFlags(::GetKeyState(VK_SHIFT) < 0, explore), outPopup, outShellMax)) {
        pMenu->Release();
        return false;
    }
    *outMenu = pMenu;
    return true;
}

// ---- Native verbs for keyboard shortcuts and command-bar buttons ------------------------

CMainWnd::FileCommand CMainWnd::FileCommandForKey(WPARAM key, bool ctrl, bool shift, bool alt)
{
    if (alt) return (!ctrl && key == VK_RETURN) ? FileCommand::Properties : FileCommand::None;
    if (ctrl) {
        switch (key) {
        case 'C': return shift ? FileCommand::None : FileCommand::Copy; // Ctrl+Shift+C = 复制路径
        case VK_INSERT: return FileCommand::Copy;
        case 'X': return FileCommand::Cut;
        case 'V': return FileCommand::Paste;
        case 'Z': return shift ? FileCommand::Redo : FileCommand::Undo;
        case 'Y': return FileCommand::Redo;
        case 'A': return FileCommand::SelectAll;
        case 'N': return shift ? FileCommand::NewFolder : FileCommand::None;
        case 'D': return shift ? FileCommand::DeletePermanent : FileCommand::Delete;
        case VK_DELETE: return shift ? FileCommand::DeletePermanent : FileCommand::Delete;
        }
        return FileCommand::None;
    }
    if (key == VK_DELETE) return shift ? FileCommand::DeletePermanent : FileCommand::Delete;
    if (key == VK_INSERT && shift) return FileCommand::Paste;
    if (key == VK_F2 && !shift) return FileCommand::Rename;
    return FileCommand::None;
}

void CMainWnd::RunFileCommand(FileCommand command)
{
    // Only a status line on failure; Windows shows its own dialogs for everything else.
    auto selectionVerb = [&](const wchar_t* verb, DWORD mask, LPCTSTR nothingSelected) {
        if (InvokeNativeVerb(verb, NativeScope::Selection, mask)) return;
        std::vector<ClipboardItem> items;
        CollectSelectedItems(items);
        const bool viewSelection = m_shellBrowser && m_shellBrowser->IsCreated() && m_shellBrowser->IsVisible()
            && !IsTreeKeyboardFocus() && m_shellBrowser->SelectedCount() > 0;
        UpdateStatus(items.empty() && !viewSelection ? nothingSelected : _T("Windows 不能对所选项目执行此操作"));
    };
    switch (command) {
    case FileCommand::Copy: selectionVerb(L"copy", 0, _T("请先选中要复制的项目")); break;
    case FileCommand::Cut: selectionVerb(L"cut", 0, _T("请先选中要剪切的项目")); break;
    case FileCommand::Paste:
        if (!InvokeNativeVerb(L"paste", NativeScope::Background)) UpdateStatus(_T("剪贴板中没有可粘贴到此处的内容"));
        break;
    case FileCommand::Delete: selectionVerb(L"delete", 0, _T("请先选中要删除的项目")); break;
    case FileCommand::DeletePermanent:
        selectionVerb(L"delete", CMIC_MASK_SHIFT_DOWN, _T("请先选中要删除的项目"));
        break;
    case FileCommand::Rename: OnRenameClicked(); break;
    case FileCommand::NewFolder:
        if (!InvokeNativeVerb(CMDSTR_NEWFOLDERW, NativeScope::Background)) UpdateStatus(_T("此位置不能新建文件夹"));
        break;
    case FileCommand::SelectAll: SelectAllItems(); break;
    case FileCommand::Undo: OnUndo(); break;
    case FileCommand::Redo: OnRedo(); break;
    case FileCommand::Properties: ShowPropertiesForSelection(); break;
    case FileCommand::None: break;
    }
}

bool CMainWnd::CanUndo()
{
    return NativeVerbAvailable(L"undo", NativeScope::Background) || !m_undoStack.empty();
}

bool CMainWnd::CanRedo()
{
    return NativeVerbAvailable(L"redo", NativeScope::Background) || !m_redoStack.empty();
}

void CMainWnd::OnUndo()
{
    if (InvokeNativeVerb(L"undo", NativeScope::Background)) return;
    if (!m_undoStack.empty()) {
        UndoEntry entry = std::move(m_undoStack.back());
        m_undoStack.pop_back();
        if (ApplyUndoEntry(entry, false)) {
            m_redoStack.push_back(std::move(entry));
        }
        return;
    }
    UpdateStatus(_T("没有可撤销的操作"));
}

void CMainWnd::OnRedo()
{
    if (InvokeNativeVerb(L"redo", NativeScope::Background)) return;
    if (!m_redoStack.empty()) {
        UndoEntry entry = std::move(m_redoStack.back());
        m_redoStack.pop_back();
        if (ApplyUndoEntry(entry, true)) {
            m_undoStack.push_back(std::move(entry));
        }
        return;
    }
    UpdateStatus(_T("没有可重做的操作"));
}

// Menu object for a native verb. Selection: the hosted view's own selection menu, the tree's
// folder, or the items selected in FastFile's search list. Background: the current folder's
// background menu. Undo / redo use the view's background menu wherever it is, because the
// Windows undo history is per process rather than per folder.
bool CMainWnd::CreateNativeVerbMenu(NativeScope scope, bool history, IContextMenu** outMenu, bool* outFromView)
{
    if (!outMenu) return false;
    *outMenu = nullptr;
    if (outFromView) *outFromView = false;
    const bool viewReady = m_shellBrowser && m_shellBrowser->IsCreated();
    if (scope == NativeScope::Background) {
        if (history && viewReady && SUCCEEDED(m_shellBrowser->CreateBackgroundContextMenu(outMenu)) && *outMenu) {
            if (outFromView) *outFromView = true;
            return true;
        }
        if (m_currentPath.empty()) return false;
        return CreateShellBackgroundContextMenu(m_currentPath, outMenu, outFromView);
    }
    if (!IsTreeKeyboardFocus() && viewReady && m_shellBrowser->IsVisible()) {
        if (m_shellBrowser->SelectedCount() <= 0) return false;
        IContextMenu* menu = nullptr;
        if (SUCCEEDED(m_shellBrowser->CreateSelectionContextMenu(&menu)) && menu) {
            *outMenu = menu;
            if (outFromView) *outFromView = true;
            return true;
        }
        return false; // nothing selected in the Windows view
    }
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.empty()) return false;
    std::vector<std::wstring> paths;
    for (const auto& item : items) paths.push_back(item.path);
    return CreateShellItemContextMenu(paths, outMenu);
}

namespace {
// Command offset of the canonical verb among the menu's items (top level and one level of
// populated submenus), or -1. disabled reports a grayed entry (e.g. 撤销 with no history).
int FindVerbOffset(IContextMenu* menu, HMENU popup, UINT first, UINT last, const wchar_t* verb, bool& disabled)
{
    disabled = false;
    for (int pass = 0; pass < 2; ++pass) {
        const int count = ::GetMenuItemCount(popup);
        for (int pos = 0; pos < count; ++pos) {
            MENUITEMINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU;
            if (!::GetMenuItemInfoW(popup, static_cast<UINT>(pos), TRUE, &info)) continue;
            if (pass == 1) {
                if (info.hSubMenu) {
                    const int found = FindVerbOffset(menu, info.hSubMenu, first, last, verb, disabled);
                    if (found >= 0) return found;
                }
                continue;
            }
            if ((info.fType & MFT_SEPARATOR) || info.hSubMenu || info.wID < first || info.wID >= last) continue;
            wchar_t name[128]{};
            if (SUCCEEDED(menu->GetCommandString(info.wID - first, GCS_VERBW, nullptr,
                    reinterpret_cast<LPSTR>(name), _countof(name))) && ::_wcsicmp(name, verb) == 0) {
                disabled = (info.fState & (MFS_DISABLED | MFS_GRAYED)) != 0;
                return static_cast<int>(info.wID - first);
            }
        }
    }
    return -1;
}
}

bool CMainWnd::InvokeNativeVerb(const wchar_t* verb, NativeScope scope, DWORD extraMask)
{
    if (!verb || !*verb) return false;
    const bool history = ::_wcsicmp(verb, L"undo") == 0 || ::_wcsicmp(verb, L"redo") == 0;
    m_lastNativeVerb.verb = verb;
    m_lastNativeVerb.scope = scope;
    m_lastNativeVerb.mask = 0;
    m_lastNativeVerb.byOffset = false;
    m_lastNativeVerb.fromView = false;
    m_lastNativeVerb.hr = S_FALSE;
    IContextMenu* menu = nullptr;
    bool fromView = false;
    if (!CreateNativeVerbMenu(scope, history, &menu, &fromView)) return false;
    m_lastNativeVerb.fromView = fromView;
    const bool shift = ::GetKeyState(VK_SHIFT) < 0 || (extraMask & CMIC_MASK_SHIFT_DOWN) != 0;
    const UINT flags = scope == NativeScope::Selection ? ShellItemMenuFlags(shift, false) : ShellBackgroundMenuFlags(shift);
    HMENU popup = nullptr;
    UINT shellMax = 0;
    bool invoked = false;
    if (QueryShellMenu(menu, flags, &popup, &shellMax)) {
        bool disabled = false;
        const int offset = FindVerbOffset(menu, popup, 1, shellMax, verb, disabled);
        POINT cursor{};
        ::GetCursorPos(&cursor);
        if (offset >= 0 && !disabled) {
            invoked = SUCCEEDED(InvokeShellCommand(menu, static_cast<UINT>(offset), nullptr, cursor, extraMask));
        } else if (offset < 0 && scope == NativeScope::Background && !history) {
            // Not listed at the top level (CMDSTR_NEWFOLDER lives in the lazily built 新建
            // submenu): the menu still accepts its canonical string verb.
            invoked = SUCCEEDED(InvokeShellCommand(menu, 0, verb, cursor, extraMask));
            m_lastNativeVerb.verb = verb;
        }
        ::DestroyMenu(popup);
    }
    menu->Release();
    return invoked;
}

bool CMainWnd::NativeVerbAvailable(const wchar_t* verb, NativeScope scope)
{
    IContextMenu* menu = nullptr;
    const bool history = ::_wcsicmp(verb, L"undo") == 0 || ::_wcsicmp(verb, L"redo") == 0;
    if (!CreateNativeVerbMenu(scope, history, &menu, nullptr)) return false;
    HMENU popup = nullptr;
    UINT shellMax = 0;
    bool available = false;
    if (QueryShellMenu(menu, scope == NativeScope::Selection ? ShellItemMenuFlags(false, false)
            : ShellBackgroundMenuFlags(false), &popup, &shellMax)) {
        bool disabled = false;
        available = FindVerbOffset(menu, popup, 1, shellMax, verb, disabled) >= 0 && !disabled;
        ::DestroyMenu(popup);
    }
    menu->Release();
    return available;
}
