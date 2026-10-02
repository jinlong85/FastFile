// FastFile - toolbar dropdown menus and Shell context menus
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

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
    ::AppendMenuW(hMenu, (m_undoStack.empty() && !(!m_historyStarted && m_shellBrowser && m_shellBrowser->InvokeHistory(false, false))) ? (MF_STRING | MF_GRAYED) : MF_STRING,
        4, L"撤销\tCtrl+Z");
    ::AppendMenuW(hMenu, (m_redoStack.empty() && !(!m_historyStarted && m_shellBrowser && m_shellBrowser->InvokeHistory(true, false))) ? (MF_STRING | MF_GRAYED) : MF_STRING,
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
            const bool hasVerb=SUCCEEDED(pMenu->GetCommandString(id - idCmdFirst, GCS_VERBW, nullptr,
                    reinterpret_cast<LPSTR>(verb), _countof(verb)));
            if (hasVerb && ::_wcsicmp(verb, L"Powershell") == 0) {
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

void CMainWnd::AddInternalFolderOpenMenu(IContextMenu* menu,HMENU popup,UINT first,UINT last,
    const std::vector<std::wstring>& paths)
{
    if(paths.empty())return;
    for(const auto& path:paths) {
        const DWORD attributes=GetFileAttributesW(path.c_str());
        if(!IsThisPcPath(path) && (attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY)))return;
    }
    bool found=false;
    for(int pos=0;pos<GetMenuItemCount(popup);++pos) {
        const UINT id=GetMenuItemID(popup,pos);wchar_t verb[128]{};
        if(id>=first && id<last && SUCCEEDED(menu->GetCommandString(id-first,GCS_VERBW,nullptr,reinterpret_cast<LPSTR>(verb),_countof(verb)))
            && (_wcsicmp(verb,L"opennewwindow")==0 || _wcsicmp(verb,L"opennewtab")==0)) {
            MENUITEMINFOW item{};item.cbSize=sizeof(item);item.fMask=MIIM_STRING;
            item.dwTypeData=const_cast<wchar_t*>(L"在新选项卡中打开");
            SetMenuItemInfoW(popup,pos,TRUE,&item);found=true;
        }
    }
    // Some Shell providers omit their new-window verb for embedded hosts.
    if(!found)InsertMenuW(popup,1,MF_BYPOSITION|MF_STRING,kCmdShellNewTab,L"在新选项卡中打开");
}

// Collapse separators left behind by removals / insertions (no leading, trailing or
// doubled separators) — must run *after* FastFile's own view items are inserted.
// Shell separators carry ids such as 0, -1, 0x7FFD or 0x7FFE, so detection is by
// MFT_SEPARATOR (see ShellMenuUtil.h), never by "id == 0".
void CMainWnd::TidyMenuSeparators(HMENU hMenu)
{
    ShellMenuUtil::NormalizeSeparators(hMenu, true);
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

    // Every Shell menu FastFile shows is normalized last: no leading / trailing / stacked
    // separators after pruning and after FastFile's own entries were mixed in.
    TidyMenuSeparators(hMenu);

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
    if(cmd==kCmdShellNewTab) {
        HandleInternalFolderOpenVerb(L"opennewtab",m_shellMenuPaths);return true;
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
    if (cmd == kCmdBgUndo) { OnUndo(); return true; }
    if (cmd == kCmdBgRedo) { OnRedo(); return true; }
    if (cmd >= kCmdBgViewBase && cmd < kCmdBgViewBase + 8) {
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
        if (m_shellBrowser && m_shellBrowser->IsCreated() && m_shellBrowser->IsVisible()) {
            m_shellBrowser->SetSort(static_cast<int>(m_sortColumn), m_sortAscending);
            return true;
        }
        SortListingCache();
        UpdateHeaderSortIndicators();
        RebuildCurrentViewFromCache();
        return true;
    }
    if (cmd == kCmdBgSortBase + 4 || cmd == kCmdBgSortBase + 5) {
        m_sortAscending = (cmd == kCmdBgSortBase + 4);
        if (m_shellBrowser && m_shellBrowser->IsCreated() && m_shellBrowser->IsVisible()) {
            m_shellBrowser->SetSort(static_cast<int>(m_sortColumn), m_sortAscending);
            return true;
        }
        SortListingCache();
        UpdateHeaderSortIndicators();
        RebuildCurrentViewFromCache();
        return true;
    }

    if (cmd >= idCmdFirst && cmd < idShellMax) {
        wchar_t verb[128] = {};
        const bool hasVerb = SUCCEEDED(pMenu->GetCommandString(cmd - idCmdFirst,
            GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), _countof(verb)));

        if(hasVerb && HandleInternalFolderOpenVerb(verb,m_shellMenuPaths))return true;
        if (hasVerb && HandleRoutedShellVerb(verb)) return true;

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

// Shell verbs FastFile runs itself so its Ctrl+Z history and the Explorer undo stack stay
// in step, and file operations use the same native-progress engine everywhere:
//   undo / redo   -> FastFile history (falls back to Explorer's own record when FastFile has none)
//   refresh       -> FastFile refresh (Shell view, details pane and tree)
//   paste         -> folder background only, when the clipboard holds real file-system items
//   delete        -> item menus whose items are all real file-system paths (Shift = permanent)
// Anything else (virtual items, pastes of non-file data, 粘贴快捷方式 ...) stays with the Shell,
// which shows Explorer's own progress UI; FastFile adds no status-bar progress to it.
bool CMainWnd::HandleRoutedShellVerb(const std::wstring& verb)
{
    if (_wcsicmp(verb.c_str(), L"undo") == 0) { OnUndo(); return true; }
    if (_wcsicmp(verb.c_str(), L"redo") == 0) { OnRedo(); return true; }
    if (m_shellMenuBackground && _wcsicmp(verb.c_str(), L"refresh") == 0) { RefreshListing(); return true; }
    if (m_shellMenuBackground && _wcsicmp(verb.c_str(), L"paste") == 0) {
        std::vector<ClipboardItem> items; bool cut = false;
        const DWORD attributes = GetFileAttributesW(m_currentPath.c_str());
        if (!PathEquals(m_shellMenuFolder, m_currentPath) || IsThisPcPath(m_currentPath)
            || attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)
            || !ReadFileClipboard(items, cut)) return false;
        OnPasteClicked();
        return true;
    }
    if (!m_shellMenuBackground && _wcsicmp(verb.c_str(), L"delete") == 0 && !m_shellMenuPaths.empty()) {
        std::vector<ClipboardItem> items;
        for (const auto& path : m_shellMenuPaths) {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (IsThisPcPath(path) || attributes == INVALID_FILE_ATTRIBUTES || ParentPath(path).empty()) return false;
            items.push_back({path, (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
        }
        DeletePaths(items, ::GetKeyState(VK_SHIFT) < 0);
        return true;
    }
    return false;
}

bool CMainWnd::HandleInternalFolderOpenVerb(const std::wstring& verb,const std::vector<std::wstring>& paths)
{
    const bool newTab=_wcsicmp(verb.c_str(),L"opennewwindow")==0 || _wcsicmp(verb.c_str(),L"opennewtab")==0;
    if((!newTab && _wcsicmp(verb.c_str(),L"open")!=0 && _wcsicmp(verb.c_str(),L"explore")!=0) || paths.empty())return false;
    for(const auto& path:paths) {
        const DWORD attributes=GetFileAttributesW(path.c_str());
        if(!IsThisPcPath(path) && (attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY)))return false;
    }
    for(size_t i=0;i<paths.size();++i) {
        auto* target=new(std::nothrow) std::wstring(paths[i]);
        if(target && !::PostMessageW(m_hWnd,kMsgShellFolderOpen,newTab ? 2 : (i!=0),reinterpret_cast<LPARAM>(target)))delete target;
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

static const wchar_t* HistoryKindLabel(int kind)
{
    switch (kind) {
    case 0: case 4: return L"重命名";   // Rename, ShellRename
    case 1: return L"新建";             // CreateFolder
    case 2: return L"移动";
    case 3: return L"复制";
    case 5: return L"删除";             // ShellDelete
    }
    return L"";
}

// FastFile's own 查看 / 排序方式 submenus: they drive FastFile's view state (memorised view
// modes, 26 px row spacing, details headers, sort indicators), not the Shell view directly.
HMENU CMainWnd::CreateBackgroundViewSubmenu() const
{
    HMENU hView = ::CreatePopupMenu();
    if (!hView) return nullptr;
    auto add = [&](ViewMode mode, const wchar_t* text) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_ID | MIIM_STRING | MIIM_FTYPE | MIIM_STATE;
        info.fType = MFT_STRING | MFT_RADIOCHECK;
        info.fState = m_viewMode == mode ? MFS_CHECKED : MFS_UNCHECKED;
        info.wID = static_cast<UINT>(kCmdBgViewBase + static_cast<int>(mode));
        info.dwTypeData = const_cast<wchar_t*>(text);
        ::InsertMenuItemW(hView, ::GetMenuItemCount(hView), TRUE, &info);
    };
    add(ViewMode::ExtraLargeIcons, L"超大图标(&X)");
    add(ViewMode::LargeIcons, L"大图标(&R)");
    add(ViewMode::MediumIcons, L"中等图标(&M)");
    add(ViewMode::SmallIcons, L"小图标(&N)");
    add(ViewMode::List, L"列表(&L)");
    add(ViewMode::Details, L"详细信息(&D)");
    add(ViewMode::Tiles, L"平铺(&S)");
    add(ViewMode::Content, L"内容(&T)");
    return hView;
}

HMENU CMainWnd::CreateBackgroundSortSubmenu() const
{
    HMENU hSort = ::CreatePopupMenu();
    if (!hSort) return nullptr;
    auto add = [&](UINT_PTR id, bool checked, const wchar_t* text) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_ID | MIIM_STRING | MIIM_FTYPE | MIIM_STATE;
        info.fType = MFT_STRING | MFT_RADIOCHECK;
        info.fState = checked ? MFS_CHECKED : MFS_UNCHECKED;
        info.wID = static_cast<UINT>(id);
        info.dwTypeData = const_cast<wchar_t*>(text);
        ::InsertMenuItemW(hSort, ::GetMenuItemCount(hSort), TRUE, &info);
    };
    add(kCmdBgSortBase + 0, m_sortColumn == SortColumn::Name, L"名称(&N)");
    add(kCmdBgSortBase + 1, m_sortColumn == SortColumn::Modified, L"修改日期");
    add(kCmdBgSortBase + 2, m_sortColumn == SortColumn::Type, L"类型");
    add(kCmdBgSortBase + 3, m_sortColumn == SortColumn::Size, L"大小");
    ::AppendMenuW(hSort, MF_SEPARATOR, 0, nullptr);
    add(kCmdBgSortBase + 4, m_sortAscending, L"递增(&A)");
    add(kCmdBgSortBase + 5, !m_sortAscending, L"递减(&D)");
    return hSort;
}

bool CMainWnd::ShellBrowserShowsFolder(const std::wstring& folderPath) const
{
    if (!m_shellBrowser || !m_shellBrowser->IsCreated() || !m_shellBrowser->IsVisible()) return false;
    const std::wstring current = m_shellBrowser->CurrentPath();
    if (folderPath.empty() || IsThisPcPath(folderPath)) return IsThisPcPath(current);
    return !IsThisPcPath(current) && PathEquals(current, folderPath);
}

// Builds the folder-background menu. Preferred source is the visible ExplorerBrowser view
// (IShellView::GetItemObject(SVGIO_BACKGROUND)): exactly the menu Explorer shows, including
// 粘贴 / 粘贴快捷方式 / 撤销 / 分组依据 / 自定义文件夹 as Windows provides them. Folders that
// are not on screen (the 此电脑 quick row, search results) bind the folder's own
// IShellFolder::CreateViewObject menu and get FastFile's 查看 / 排序方式 / 刷新 on top.
bool CMainWnd::BuildShellBackgroundMenu(const std::wstring& folderPath, IContextMenu** outMenu,
    HMENU* outPopup, UINT* outShellMax, bool* outFromView)
{
    if (!outMenu || !outPopup || !outShellMax) return false;
    *outMenu = nullptr; *outPopup = nullptr; *outShellMax = 0;
    if (outFromView) *outFromView = false;

    IContextMenu* pMenu = nullptr;
    bool fromView = false;
    if (ShellBrowserShowsFolder(folderPath)
        && SUCCEEDED(m_shellBrowser->CreateBackgroundContextMenu(&pMenu)) && pMenu)
        fromView = true;

    if (!pMenu) {
        PIDLIST_ABSOLUTE pidlFolder = nullptr;
        SFGAOF sfgao = 0;
        HRESULT hr = (folderPath.empty() || IsThisPcPath(folderPath))
            ? ::SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &pidlFolder)
            : ::SHParseDisplayName(folderPath.c_str(), nullptr, &pidlFolder, 0, &sfgao);
        if (FAILED(hr) || !pidlFolder) return false;
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
        hr = pFolder->CreateViewObject(m_hWnd, IID_IContextMenu, reinterpret_cast<void**>(&pMenu));
        pFolder->Release();
        if (FAILED(hr) || !pMenu) return false;
    }

    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) { pMenu->Release(); return false; }
    const UINT idCmdFirst = 1;
    const UINT idCmdLast = 0x7FFF;
    UINT flags = CMF_NORMAL | CMF_EXPLORE;
    if (::GetKeyState(VK_SHIFT) < 0) flags |= CMF_EXTENDEDVERBS; // Explorer: Shift+right-click
    const HRESULT hr = pMenu->QueryContextMenu(hMenu, 0, idCmdFirst, idCmdLast, flags);
    if (FAILED(hr)) {
        ::DestroyMenu(hMenu);
        pMenu->Release();
        return false;
    }
    const UINT idShellMax = idCmdFirst + static_cast<UINT>(HRESULT_CODE(hr));

    // Drop the entries Windows itself would not show here (legacy PowerShell verb,
    // third-party "用 X 打开" verbs, empty cascading submenus such as 授予访问权限).
    PruneShellMenu(pMenu, hMenu, idCmdFirst, idShellMax, true);

    // 查看 / 排序方式 must keep driving FastFile's view. In the view menu swap the Shell's
    // submenus for FastFile's (label and position stay native); otherwise insert them.
    HMENU hView = CreateBackgroundViewSubmenu();
    HMENU hSort = CreateBackgroundSortSubmenu();
    auto swapSubmenu = [&](const wchar_t* verb, HMENU& replacement) {
        const int pos = ShellMenuUtil::FindVerb(pMenu, hMenu, idCmdFirst, idShellMax, verb);
        if (pos < 0 || !replacement) return false;
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_SUBMENU;
        if (!::GetMenuItemInfoW(hMenu, pos, TRUE, &info) || !info.hSubMenu) return false;
        HMENU original = info.hSubMenu;
        info.hSubMenu = replacement;
        if (!::SetMenuItemInfoW(hMenu, pos, TRUE, &info)) return false;
        // Keep the detached Shell submenu alive until the menu loop ends, so its handle
        // value cannot be reused while the Shell still remembers it.
        m_retiredShellMenus.push_back(original);
        replacement = nullptr;
        return true;
    };
    const bool viewSwapped = swapSubmenu(L"view", hView);
    const bool sortSwapped = swapSubmenu(L"arrange", hSort);
    int pos = 0;
    if (!viewSwapped && hView) {
        ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(hView), L"查看(&V)");
        hView = nullptr;
    }
    if (!sortSwapped && hSort) {
        ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(hSort), L"排序方式(&O)");
        hSort = nullptr;
    }
    if (hView) ::DestroyMenu(hView);
    if (hSort) ::DestroyMenu(hSort);
    if (ShellMenuUtil::FindVerb(pMenu, hMenu, idCmdFirst, idShellMax, L"refresh") < 0)
        ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_STRING, kCmdBgRefresh, L"刷新(&E)");
    if (pos > 0) ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    // A bare IShellFolder menu has no 粘贴; offer FastFile's when Windows holds files.
    const DWORD attributes = GetFileAttributesW(folderPath.c_str());
    if (ShellMenuUtil::FindVerb(pMenu, hMenu, idCmdFirst, idShellMax, L"paste") < 0
        && !IsThisPcPath(folderPath) && attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) && ::IsClipboardFormatAvailable(CF_HDROP)) {
        ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_STRING, kCmdBgPaste, L"粘贴(&P)");
        ::InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    }

    // 撤销 / 重做 follow FastFile's history (which itself falls back to Explorer's record
    // while FastFile has none), so the entry, its label and Ctrl+Z always agree.
    auto history = [&](bool redo) {
        const auto& stack = redo ? m_redoStack : m_undoStack;
        const bool available = !stack.empty() ||
            (!m_historyStarted && m_shellBrowser && m_shellBrowser->InvokeHistory(redo, false));
        const int nativePos = ShellMenuUtil::FindVerb(pMenu, hMenu, idCmdFirst, idShellMax, redo ? L"redo" : L"undo");
        std::wstring label;
        if (!stack.empty()) {
            label = std::wstring(redo ? L"重做 " : L"撤销 ") + HistoryKindLabel(static_cast<int>(stack.back().kind))
                + (redo ? L"(&Y)\tCtrl+Y" : L"(&U)\tCtrl+Z");
        }
        if (nativePos >= 0) {
            if (!available) { ::DeleteMenu(hMenu, nativePos, MF_BYPOSITION); return; }
            if (!label.empty()) {
                MENUITEMINFOW info{};
                info.cbSize = sizeof(info);
                info.fMask = MIIM_STRING;
                info.dwTypeData = const_cast<wchar_t*>(label.c_str());
                ::SetMenuItemInfoW(hMenu, nativePos, TRUE, &info);
                ::EnableMenuItem(hMenu, nativePos, MF_BYPOSITION | MF_ENABLED);
            }
            return;
        }
        if (!available) return;
        if (label.empty()) label = redo ? L"重做(&Y)\tCtrl+Y" : L"撤销(&U)\tCtrl+Z";
        auto findId = [&](UINT_PTR id) {
            for (int i = 0; i < ::GetMenuItemCount(hMenu); ++i)
                if (::GetMenuItemID(hMenu, i) == static_cast<UINT>(id)) return i;
            return -1;
        };
        // Explorer order: 粘贴, 粘贴快捷方式, 撤销, 重做.
        int anchor = redo ? findId(kCmdBgUndo) : -1;
        if (anchor < 0 && redo) anchor = ShellMenuUtil::FindVerb(pMenu, hMenu, idCmdFirst, idShellMax, L"undo");
        for (const wchar_t* verb : {L"pastelink", L"paste", L"refresh"}) {
            if (anchor >= 0) break;
            anchor = ShellMenuUtil::FindVerb(pMenu, hMenu, idCmdFirst, idShellMax, verb);
        }
        if (anchor < 0) anchor = (std::max)(findId(kCmdBgPaste), findId(kCmdBgRefresh));
        ::InsertMenuW(hMenu, anchor + 1, MF_BYPOSITION | MF_STRING, redo ? kCmdBgRedo : kCmdBgUndo, label.c_str());
    };
    history(false);
    history(true);

    TidyMenuSeparators(hMenu);
    *outMenu = pMenu;
    *outPopup = hMenu;
    *outShellMax = idShellMax;
    if (outFromView) *outFromView = fromView;
    return true;
}

bool CMainWnd::ShowShellBackgroundContextMenu(const std::wstring& folderPath, POINT ptScreen)
{
    IContextMenu* pMenu = nullptr;
    HMENU hMenu = nullptr;
    UINT idShellMax = 0;
    if (!BuildShellBackgroundMenu(folderPath, &pMenu, &hMenu, &idShellMax, nullptr)) return false;
    // Full Shell menu (IContextMenu2/3) plus the FastFile view entries added above.
    m_shellMenuBackground = true;
    m_shellMenuFolder = folderPath;
    TrackPopupShellMenu(pMenu, hMenu, ptScreen, 1, idShellMax, false);
    m_shellMenuBackground = false;
    m_shellMenuFolder.clear();
    ::DestroyMenu(hMenu);
    pMenu->Release();
    ReleaseRetiredShellMenus();
    return true;
}

void CMainWnd::ReleaseRetiredShellMenus()
{
    for (HMENU menu : m_retiredShellMenus) ::DestroyMenu(menu);
    m_retiredShellMenus.clear();
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

    std::vector<std::pair<UINT, std::wstring>> extraItems;
    if (items.size() == 1
        && !ParentPath(items[0].path).empty()
        && ::GetFileAttributesW(items[0].path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        extraItems.emplace_back(static_cast<UINT>(kCmdShellRename), L"重命名");
    }
    UINT extraCommand = 0;
    if (!ShowShellContextMenu(paths, ptScreen,
            extraItems.empty() ? nullptr : &extraItems, &extraCommand)) {
        ShowFallbackContextMenu(items, ptScreen);
        return;
    }
    if (extraCommand == kCmdShellRename)
        OnRenameClicked();
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
    AddInternalFolderOpenMenu(pMenu,hMenu,idCmdFirst,idShellMax,paths);
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
                ShellPresentation::OpenDefaultFile(m_hWnd,items[0].path);
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
