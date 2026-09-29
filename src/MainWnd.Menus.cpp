// FastFile - toolbar dropdown menus and Shell context menus
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

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
    ::AppendMenuW(hMenu, check(ViewMode::List), 4, L"列表");
    ::AppendMenuW(hMenu, check(ViewMode::Details), 5, L"详细信息");
    ::AppendMenuW(hMenu, check(ViewMode::Tiles), 6, L"平铺");
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
    ::AppendMenuW(hMenu, m_undoStack.empty() ? (MF_STRING | MF_GRAYED) : MF_STRING,
        4, L"撤销\tCtrl+Z");
    ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(hMenu, IsFolderOpenHandlerEnabled() ? (MF_STRING | MF_CHECKED) : MF_STRING,
        5, L"使用 FastFile 打开系统文件夹");

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
    } else if (cmd == 5) {
        OnFolderOpenHandlerMenuClicked();
    } else if (cmd == 6) {
        if (!cfgDir.empty())
            AddTab(cfgDir, true, /*allowDuplicate*/ false);
    }
}

void CMainWnd::ForwardShellMenuMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* pResult, bool* handled)
{
    if (handled) *handled = false;
    if (m_pCtxMenu3) {
        if (uMsg == WM_MENUCHAR) {
            LRESULT lr = 0;
            if (SUCCEEDED(m_pCtxMenu3->HandleMenuMsg2(uMsg, wParam, lParam, &lr))) {
                if (pResult) *pResult = lr;
                if (handled) *handled = true;
            }
            return;
        }
        if (uMsg == WM_INITMENUPOPUP || uMsg == WM_DRAWITEM || uMsg == WM_MEASUREITEM) {
            if (SUCCEEDED(m_pCtxMenu3->HandleMenuMsg(uMsg, wParam, lParam))) {
                if (pResult) *pResult = 0;
                if (handled) *handled = true;
            }
            return;
        }
    } else if (m_pCtxMenu2) {
        if (uMsg == WM_INITMENUPOPUP || uMsg == WM_DRAWITEM || uMsg == WM_MEASUREITEM) {
            if (SUCCEEDED(m_pCtxMenu2->HandleMenuMsg(uMsg, wParam, lParam))) {
                if (pResult) *pResult = 0;
                if (handled) *handled = true;
            }
        }
    }
}

// Shell menus hand us a few entries we deliberately hide:
//   * the legacy "在此处打开 PowerShell 窗口" verb (Explorer suppresses it when the
//     Windows Terminal entry exists);
//   * third-party "用 <app> 打开" verbs injected into the folder background;
//   * cascading submenus the Shell leaves empty (Windows 11 no longer fills
//     授予访问权限 for local folders, so the entry would just be a dead arrow).
// Cascading submenus are pre-populated here so emptiness can be detected before the menu
// is shown; the Shell simply re-populates them again in the real menu loop.
void CMainWnd::PruneShellMenu(IContextMenu* pMenu, HMENU hMenu, UINT idCmdFirst,
    UINT idShellMax, bool backgroundMenu)
{
    if (!pMenu || !hMenu)
        return;

    IContextMenu2* pcm2 = nullptr;
    pMenu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void**>(&pcm2));

    for (int pos = ::GetMenuItemCount(hMenu) - 1; pos >= 0; --pos) {
        HMENU sub = ::GetSubMenu(hMenu, pos);
        if (sub) {
            if (pcm2)
                pcm2->HandleMenuMsg(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(sub),
                    MAKELONG(static_cast<WORD>(pos), FALSE));
            if (::GetMenuItemCount(sub) <= 0) {
                ::DeleteMenu(hMenu, pos, MF_BYPOSITION);
                ::DestroyMenu(sub);
            }
            continue;
        }

        const UINT id = ::GetMenuItemID(hMenu, pos);
        if (id == 0 || id == 0xFFFFFFFFu)      // separator
            continue;

        if (id >= idCmdFirst && id < idShellMax) {
            wchar_t verb[128] = {};
            if (SUCCEEDED(pMenu->GetCommandString(id - idCmdFirst, GCS_VERBW, nullptr,
                    reinterpret_cast<LPSTR>(verb), _countof(verb)))
                && ::_wcsicmp(verb, L"Powershell") == 0) {
                ::DeleteMenu(hMenu, pos, MF_BYPOSITION);
                continue;
            }
        }

        if (backgroundMenu) {
            wchar_t text[256] = {};
            ::GetMenuStringW(hMenu, pos, text, _countof(text), MF_BYPOSITION);
            if (text[0] == L'用' && ::wcsstr(text, L"打开") != nullptr)
                ::DeleteMenu(hMenu, pos, MF_BYPOSITION);
        }
    }
    if (pcm2)
        pcm2->Release();

    TidyMenuSeparators(hMenu);
}

// Collapse separators left behind by removals / insertions (no leading, trailing or
// doubled separators) — must run *after* FastFile's own view items are inserted.
void CMainWnd::TidyMenuSeparators(HMENU hMenu)
{
    if (!hMenu)
        return;
    bool prevSep = true;
    for (int pos = 0; pos < ::GetMenuItemCount(hMenu); ) {
        const UINT id = ::GetMenuItemID(hMenu, pos);
        const bool isSep = (id == 0 && ::GetSubMenu(hMenu, pos) == nullptr);
        if (isSep && prevSep) {
            ::DeleteMenu(hMenu, pos, MF_BYPOSITION);
            continue;
        }
        prevSep = isSep;
        ++pos;
    }
    while (::GetMenuItemCount(hMenu) > 0) {
        const int last = ::GetMenuItemCount(hMenu) - 1;
        const UINT id = ::GetMenuItemID(hMenu, last);
        if (id == 0 && ::GetSubMenu(hMenu, last) == nullptr)
            ::DeleteMenu(hMenu, last, MF_BYPOSITION);
        else
            break;
    }
}

bool CMainWnd::TrackPopupShellMenu(IContextMenu* pMenu, HMENU hMenu, POINT ptScreen,
    UINT idCmdFirst, UINT idShellMax, bool appendHiddenToggle,
    const std::vector<std::pair<UINT, std::wstring>>* extraItems, UINT* outExtraCmd)
{
    if (!pMenu || !hMenu)
        return false;

    // FastFile entries appended below the Shell verbs (quick-access rows use this for
    // 打开 / 从快速访问中取消固定). Their ids sit far above the Shell's range.
    if (extraItems && !extraItems->empty()) {
        ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        for (const auto& item : *extraItems)
            ::AppendMenuW(hMenu, MF_STRING, item.first, item.second.c_str());
    }
    if (appendHiddenToggle) {
        ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        UINT flags = MF_STRING | (m_showHidden ? MF_CHECKED : MF_UNCHECKED);
        ::AppendMenuW(hMenu, flags, kCmdToggleHidden, L"显示隐藏的项目");
    }

    // Hold IContextMenu2/3 so owner-draw + cascading submenus work during TrackPopupMenu.
    // Do NOT use TPM_NONOTIFY — Shell needs WM_INITMENUPOPUP / DRAWITEM / MEASUREITEM.
    m_pCtxMenu = pMenu;
    m_pCtxMenu2 = nullptr;
    m_pCtxMenu3 = nullptr;
    pMenu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void**>(&m_pCtxMenu2));
    pMenu->QueryInterface(IID_IContextMenu3, reinterpret_cast<void**>(&m_pCtxMenu3));

    UINT cmd = ::TrackPopupMenuEx(hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        ptScreen.x, ptScreen.y, m_hWnd, nullptr);

    IContextMenu2* pcm2 = m_pCtxMenu2;
    IContextMenu3* pcm3 = m_pCtxMenu3;
    m_pCtxMenu = nullptr;
    m_pCtxMenu2 = nullptr;
    m_pCtxMenu3 = nullptr;
    if (pcm3) pcm3->Release();
    if (pcm2) pcm2->Release();

    if (outExtraCmd) *outExtraCmd = 0;
    if (extraItems) {
        for (const auto& item : *extraItems) {
            if (cmd != 0 && cmd == item.first) {
                if (outExtraCmd) *outExtraCmd = item.first;
                return true;
            }
        }
    }
    if (cmd == kCmdToggleHidden) {
        ToggleShowHidden();
        return true;
    }

    // FastFile's own entries in the folder-background menu (see ShowShellBackgroundContextMenu).
    if (cmd == kCmdBgRefresh) {
        RefreshListing();
        return true;
    }
    if (cmd == kCmdBgPaste) {
        OnPasteClicked();
        return true;
    }
    if (cmd >= kCmdBgViewBase && cmd < kCmdBgViewBase + 6) {
        SetViewMode(static_cast<ViewMode>(cmd - kCmdBgViewBase));
        return true;
    }
    if (cmd >= kCmdBgSortBase && cmd < kCmdBgSortBase + 4) {
        const SortColumn col = static_cast<SortColumn>(cmd - kCmdBgSortBase);
        if (m_sortColumn == col)
            m_sortAscending = !m_sortAscending;
        else {
            m_sortColumn = col;
            m_sortAscending = true;
        }
        SortListingCache();
        UpdateHeaderSortIndicators();
        RebuildCurrentViewFromCache();
        return true;
    }
    if (cmd == kCmdBgSortBase + 4 || cmd == kCmdBgSortBase + 5) {
        m_sortAscending = (cmd == kCmdBgSortBase + 4);
        SortListingCache();
        UpdateHeaderSortIndicators();
        RebuildCurrentViewFromCache();
        return true;
    }

    if (cmd >= idCmdFirst && cmd < idShellMax) {
        wchar_t verb[128] = {};
        const bool hasVerb = SUCCEEDED(pMenu->GetCommandString(cmd - idCmdFirst,
            GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), _countof(verb)));

        // "属性" goes through the documented API instead of the menu's offset verb.
        // The offset verb is resolved against whatever folder object the menu was bound to,
        // and for a drive in 此电脑 that produced the *Computer folder's* sheet (系统关于)
        // rather than the drive's own property pages. SHObjectProperties() always targets
        // the selected path itself.
        if (hasVerb && ::_wcsicmp(verb, L"properties") == 0 && m_shellMenuPaths.size() == 1
            && ::SHObjectProperties(m_hWnd, SHOP_FILEPATH, m_shellMenuPaths.front().c_str(), nullptr)) {
            RefreshListing();
            return true;
        }

        CMINVOKECOMMANDINFOEX info = {};
        info.cbSize = sizeof(info);
        info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
        info.hwnd = m_hWnd;
        info.lpVerb = MAKEINTRESOURCEA(cmd - idCmdFirst);
        info.lpVerbW = MAKEINTRESOURCEW(cmd - idCmdFirst);
        info.nShow = SW_SHOWNORMAL;
        info.ptInvoke = ptScreen;
        HRESULT hr = pMenu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
        if (SUCCEEDED(hr) && hasVerb && m_shellMenuPaths.size() == 1) {
            if (_wcsicmp(verb, L"pintohome") == 0)
                PinQuickAccess(m_shellMenuPaths.front());
            else if (_wcsicmp(verb, L"unpinfromhome") == 0)
                UnpinQuickAccess(m_shellMenuPaths.front());
        }
        RefreshListing();
    }
    return true;
}

void CMainWnd::ShowBlankAreaContextMenu(POINT ptScreen)
{
    // Background right-clicks must be Shell-owned in every view. Do not mix a
    // FastFile fallback menu into the native Windows folder context menu.
    if (!ShowShellBackgroundContextMenu(m_currentPath, ptScreen))
        UpdateStatus(_T("无法显示 Windows 文件夹菜单"));
}

void CMainWnd::ShowTreeContextMenu(CTreeNodeUI* node, POINT ptScreen)
{
    if (!node) return;
    CDuiString ud = node->GetUserData();
    if (ud.IsEmpty()) return;
    std::wstring path = ud.GetData();
    if (IsThisPcPath(path) || path == kPendingMarker)
        return;
    std::vector<std::wstring> paths;
    paths.push_back(NormalizePath(path));
    if (!ShowShellContextMenu(paths, ptScreen)) {
        ClipboardItem it;
        it.path = paths[0];
        it.isDir = true;
        ShowFallbackContextMenu({ it }, ptScreen);
    }
}

bool CMainWnd::ShowShellBackgroundContextMenu(const std::wstring& folderPath, POINT ptScreen)
{
    PIDLIST_ABSOLUTE pidlFolder = nullptr;
    SFGAOF sfgao = 0;
    HRESULT hr = S_OK;
    if (folderPath.empty() || IsThisPcPath(folderPath)) {
        // The Computer folder has its own native background verbs (View, Sort,
        // Refresh, etc.) and must not fall through to FastFile's custom menu.
        hr = ::SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &pidlFolder);
    } else {
        hr = ::SHParseDisplayName(folderPath.c_str(), nullptr, &pidlFolder, 0, &sfgao);
    }
    if (FAILED(hr) || !pidlFolder) return false;

    // Bind from the desktop shell folder, as Explorer does, so this is the
    // directory background context rather than a FastFile-owned fallback.
    IShellFolder* pDesktop = nullptr;
    IShellFolder* pFolder = nullptr;
    hr = ::SHGetDesktopFolder(&pDesktop);
    if (SUCCEEDED(hr) && pDesktop) {
        hr = pDesktop->BindToObject(pidlFolder, nullptr, IID_IShellFolder,
            reinterpret_cast<void**>(&pFolder));
        pDesktop->Release();
    }
    ::CoTaskMemFree(pidlFolder);
    if (FAILED(hr) || !pFolder) return false;

    IContextMenu* pMenu = nullptr;
    hr = pFolder->CreateViewObject(m_hWnd, IID_IContextMenu, reinterpret_cast<void**>(&pMenu));
    pFolder->Release();
    if (FAILED(hr) || !pMenu) return false;

    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) {
        pMenu->Release();
        return false;
    }

    const UINT idCmdFirst = 1;
    const UINT idCmdLast = 0x7FFF;
    hr = pMenu->QueryContextMenu(hMenu, 0, idCmdFirst, idCmdLast,
        CMF_NORMAL | CMF_EXPLORE | CMF_EXTENDEDVERBS);
    if (FAILED(hr)) {
        ::DestroyMenu(hMenu);
        pMenu->Release();
        return false;
    }

    const UINT idShellMax = idCmdFirst + static_cast<UINT>(HRESULT_CODE(hr));

    // Drop the entries Windows itself would not show here (legacy PowerShell verb,
    // third-party "用 X 打开" verbs, empty cascading submenus).
    PruneShellMenu(pMenu, hMenu, idCmdFirst, idShellMax, true);

    // Explorer's folder-background menu leads with the view items that belong to the *view* -
    // 查看 / 排序方式 / 刷新 (and 粘贴) - not to IShellFolder. A plain CreateViewObject menu
    // therefore lacks them, which is why ours looked like a different, shorter menu than the
    // one Windows shows. Re-create them here, wired to FastFile's own actions.
    HMENU hView = ::CreatePopupMenu();
    HMENU hSort = ::CreatePopupMenu();
    if (hView && hSort) {
        auto checkView = [&](ViewMode m) -> UINT {
            return (m_viewMode == m) ? (MF_STRING | MF_CHECKED) : MF_STRING;
        };
        ::AppendMenuW(hView, checkView(ViewMode::ExtraLargeIcons), (UINT_PTR)(kCmdBgViewBase + 0), L"超大图标");
        ::AppendMenuW(hView, checkView(ViewMode::LargeIcons), (UINT_PTR)(kCmdBgViewBase + 1), L"大图标");
        ::AppendMenuW(hView, checkView(ViewMode::MediumIcons), (UINT_PTR)(kCmdBgViewBase + 2), L"中等图标");
        ::AppendMenuW(hView, checkView(ViewMode::List), (UINT_PTR)(kCmdBgViewBase + 3), L"列表");
        ::AppendMenuW(hView, checkView(ViewMode::Details), (UINT_PTR)(kCmdBgViewBase + 4), L"详细信息");
        ::AppendMenuW(hView, checkView(ViewMode::Tiles), (UINT_PTR)(kCmdBgViewBase + 5), L"平铺");

        auto checkSort = [&](SortColumn c) -> UINT {
            return (m_sortColumn == c) ? (MF_STRING | MF_CHECKED) : MF_STRING;
        };
        ::AppendMenuW(hSort, checkSort(SortColumn::Name), (UINT_PTR)(kCmdBgSortBase + 0), L"名称");
        ::AppendMenuW(hSort, checkSort(SortColumn::Modified), (UINT_PTR)(kCmdBgSortBase + 1), L"修改日期");
        ::AppendMenuW(hSort, checkSort(SortColumn::Type), (UINT_PTR)(kCmdBgSortBase + 2), L"类型");
        ::AppendMenuW(hSort, checkSort(SortColumn::Size), (UINT_PTR)(kCmdBgSortBase + 3), L"大小");
        ::AppendMenuW(hSort, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hSort, m_sortAscending ? (MF_STRING | MF_CHECKED) : MF_STRING,
            (UINT_PTR)(kCmdBgSortBase + 4), L"升序");
        ::AppendMenuW(hSort, !m_sortAscending ? (MF_STRING | MF_CHECKED) : MF_STRING,
            (UINT_PTR)(kCmdBgSortBase + 5), L"降序");

        int pos = 0;
        ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_POPUP,
            reinterpret_cast<UINT_PTR>(hView), L"查看");
        ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_POPUP,
            reinterpret_cast<UINT_PTR>(hSort), L"排序方式");
        ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_STRING, (UINT_PTR)kCmdBgRefresh, L"刷新");
        ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        // The Shell cannot see FastFile's own clipboard, so offer 粘贴 ourselves - but only
        // when the Shell has nothing of its own to paste, to avoid two identical entries.
        const bool shellCanPaste = ::IsClipboardFormatAvailable(CF_HDROP) != FALSE;
        if (!m_clipboard.empty() && !shellCanPaste) {
            ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_STRING, (UINT_PTR)kCmdBgPaste, L"粘贴");
            ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        }
    } else {
        if (hView) ::DestroyMenu(hView);
        if (hSort) ::DestroyMenu(hSort);
    }

    // Full Shell menu (IContextMenu2/3) plus the FastFile view entries added above.
    // (Tidy again: inserting 查看/排序方式/刷新/粘贴 above can double up the separators.)
    TidyMenuSeparators(hMenu);
    TrackPopupShellMenu(pMenu, hMenu, ptScreen, idCmdFirst, idShellMax, false);

    ::DestroyMenu(hMenu);
    pMenu->Release();
    return true;
}

// ---- Context menu (IContextMenu + fallback) ------------------------------

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
        ShowFallbackContextMenu(items, ptScreen);
}

bool CMainWnd::ShowShellContextMenu(const std::vector<std::wstring>& paths, POINT ptScreen,
    const std::vector<std::pair<UINT, std::wstring>>* extraItems, UINT* outExtraCmd)
{
    if (paths.empty()) return false;

    HRESULT hrInit = S_OK;
    // COM already initialized in wWinMain

    // Use parent folder of first item; all items should share parent for multi
    std::wstring parent = ParentPath(paths[0]);
    if (parent.empty()) {
        // drive root file?
        if (paths[0].size() >= 3 && paths[0][1] == L':')
            parent = paths[0].substr(0, 3);
        // Otherwise leave it empty — the desktop fallback below resolves the full path.
    }

    // Two ways to reach the Shell's item menu:
    //   1) bind the *parent* folder and parse the leaf names (ordinary files/folders);
    //   2) bind the desktop and parse the *full* path (drive roots in 此电脑, "shell:" items,
    //      anything whose parent folder cannot host it).
    // Explorer hands drives to the Computer folder; the desktop resolves the very same
    // objects, so a drive gets its real verbs (固定到快速访问 / 格式化 / 弹出 / 属性 …)
    // instead of FastFile's fallback menu.
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

    // A drive root has no usable parent ("I:" resolves as a *drive-relative* path and the
    // folder happily returns a PIDL for it, which then yields the wrong menu — Properties
    // opened 系统关于 instead of the drive's own property sheet). Skip straight to the
    // desktop-bound lookup, which resolves the real drive object.
    const bool driveRoot = (paths[0].size() >= 2 && paths[0][1] == L':'
        && (paths[0].size() == 2
            || (paths[0].size() == 3 && (paths[0][2] == L'\\' || paths[0][2] == L'/'))));

    bool ok = false;
    if (!driveRoot && !parent.empty())
        ok = bindParentFolder(parent);
    if (!ok)
        ok = bindDesktopFolder();
    if (!ok || pidlChildren.empty())
        return false;

    IContextMenu* pMenu = nullptr;
    if (ok && !pidlChildren.empty()) {
        std::vector<LPCITEMIDLIST> pidlArgs(pidlChildren.begin(), pidlChildren.end());
        const HRESULT hrMenu = pFolder->GetUIObjectOf(m_hWnd,
            static_cast<UINT>(pidlArgs.size()),
            pidlArgs.data(),
            IID_IContextMenu, nullptr, reinterpret_cast<void**>(&pMenu));
        if (FAILED(hrMenu)) pMenu = nullptr;
    }

    for (auto* p : pidlChildren)
        ::CoTaskMemFree(p);
    pFolder->Release();

    if (!pMenu) return false;

    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) {
        pMenu->Release();
        return false;
    }

    const UINT idCmdFirst = 1;
    const UINT idCmdLast = 0x7FFF;
    HRESULT hr = pMenu->QueryContextMenu(hMenu, 0, idCmdFirst, idCmdLast,
        CMF_NORMAL | CMF_EXPLORE);
    if (FAILED(hr)) {
        ::DestroyMenu(hMenu);
        pMenu->Release();
        return false;
    }

    const UINT idShellMax = idCmdFirst + static_cast<UINT>(HRESULT_CODE(hr));
    PruneShellMenu(pMenu, hMenu, idCmdFirst, idShellMax, false);
    // Full Shell menu with owner-draw / cascaded submenus via IContextMenu2/3
    m_shellMenuPaths = paths;
    TrackPopupShellMenu(pMenu, hMenu, ptScreen, idCmdFirst, idShellMax, false,
        extraItems, outExtraCmd);
    m_shellMenuPaths.clear();

    ::DestroyMenu(hMenu);
    pMenu->Release();
    return true;
}

void CMainWnd::ShowFallbackContextMenu(const std::vector<ClipboardItem>& items, POINT ptScreen)
{
    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;

    if (!items.empty()) {
        ::AppendMenuW(hMenu, MF_STRING, kCmdCtxOpen, L"打开");
        ::AppendMenuW(hMenu, MF_STRING, kCmdCtxCopy, L"复制");
        ::AppendMenuW(hMenu, MF_STRING, kCmdCtxDelete, L"删除到回收站");
        if (items.size() == 1)
            ::AppendMenuW(hMenu, MF_STRING, kCmdCtxRename, L"重命名");
        ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }
    ::AppendMenuW(hMenu, MF_STRING, kCmdCtxRefresh, L"刷新");
    ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    {
        UINT flags = MF_STRING | (m_showHidden ? MF_CHECKED : MF_UNCHECKED);
        ::AppendMenuW(hMenu, flags, kCmdToggleHidden, L"显示隐藏的项目");
    }

    UINT cmd = ::TrackPopupMenuEx(hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        ptScreen.x, ptScreen.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);

    switch (cmd) {
    case kCmdCtxOpen:
        if (!items.empty()) {
            if (items[0].isDir)
                NavigateTo(items[0].path, true);
            else
                ::ShellExecuteW(m_hWnd, L"open", items[0].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
    case kCmdCtxCopy: OnCopyClicked(); break;
    case kCmdCtxDelete: OnDeleteClicked(); break;
    case kCmdCtxRename: OnRenameClicked(); break;
    case kCmdCtxRefresh: RefreshListing(); break;
    case kCmdToggleHidden: ToggleShowHidden(); break;
    default: break;
    }
}
