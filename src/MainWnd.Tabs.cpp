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
        m_pTabStrip->RemoveAll();
        // A zero-width layout is treated as flexible by DuiLib and moves the + button
        // toward the centre of an empty tab bar. Keep a minimal fixed anchor instead.
        m_pTabStrip->SetFixedWidth(DpiScale(1));
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
    m_pTabStrip->RemoveAll();
    int tabStripW = 0;
    const int tabIconPx = DpiScale(UiTokens::TabIconPx);
    const int tabIconPad = DpiScale(UiTokens::SpaceSm);
    const int tabTextGap = DpiScale(UiTokens::SpaceXs);
    const int tabTextPadR = DpiScale(UiTokens::SpaceSm);
    const int tabH = DpiScale(UiTokens::HitTabH);

    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        auto* host = new CHorizontalLayoutUI;
        host->SetFixedHeight(tabH);
        host->SetAttribute(_T("padding"), _T("0,0,2,0"));

        CDuiString btnName, closeName;
        btnName.Format(_T("tab_btn_%d"), i);
        closeName.Format(_T("tab_close_%d"), i);

        const std::wstring title = TabTitleForPath(m_tabs[i].path);
        auto* btn = new CButtonUI;
        btn->SetName(btnName);
        btn->SetText(title.c_str());
        btn->SetAttribute(_T("align"), _T("center"));
        btn->SetAttribute(_T("valign"), _T("vcenter"));
        btn->SetAttribute(_T("endellipsis"), _T("true"));
        btn->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
        btn->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
        btn->SetAttribute(_T("bordersize"), _T("0"));
        btn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorHover);
        btn->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorPressed);
        {
            CDuiString tp;
            tp.Format(_T("%d,0,%d,0"),
                tabIconPad + tabIconPx + tabTextGap, tabTextPadR);
            btn->SetAttribute(_T("textpadding"), tp);
        }
        if (i == m_activeTab) {
            btn->SetAttribute(_T("bkcolor"), UiTokens::ColorTabActive);
            btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
            btn->SetAttribute(_T("font"), _T("0"));
            btn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorTabActiveHot);
        } else {
            btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextTabIdle);
        }
        int textW = MeasureTextWidth(title);
        if (textW <= 0)
            textW = DpiScale(static_cast<int>(title.size()) * 8);   // fallback estimate
        int w = textW + tabIconPad + tabIconPx + tabTextGap + tabTextPadR;
        if (w < DpiScale(UiTokens::TabMinW)) w = DpiScale(UiTokens::TabMinW);
        if (w > DpiScale(UiTokens::TabMaxW)) w = DpiScale(UiTokens::TabMaxW);
        btn->SetFixedWidth(w);
        SIZE tabRound = { DpiScale(UiTokens::RadiusControl), DpiScale(UiTokens::RadiusControl) };
        btn->SetBorderRound(tabRound);
        std::wstring tabIcon = IsThisPcPath(m_tabs[i].path)
            ? GetStockIconBmp(SIID_DESKTOPPC, tabIconPx)
            : GetShellIconBmp(m_tabs[i].path, true, tabIconPx);
        if (tabIcon.empty())
            tabIcon = GetStockIconBmp(SIID_FOLDER, tabIconPx);
        if (!tabIcon.empty())
            ApplyControlForeIcon(btn, tabIcon, tabIconPx, tabIconPad,
                (tabH - tabIconPx) / 2, false);
        m_tabs[i].button = btn;

        auto* closeBtn = new CButtonUI;
        closeBtn->SetName(closeName);
        closeBtn->SetText(_T("×"));
        closeBtn->SetFixedWidth(DpiScale(UiTokens::TabCloseW));
        closeBtn->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
        closeBtn->SetAttribute(_T("bordersize"), _T("0"));
        closeBtn->SetAttribute(_T("textcolor"), UiTokens::ColorTextMuted);
        closeBtn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorHover);
        closeBtn->SetAttribute(_T("hottextcolor"), UiTokens::ColorDanger);

        host->SetFixedWidth(w + DpiScale(UiTokens::TabCloseW));
        host->Add(btn);
        host->Add(closeBtn);
        m_pTabStrip->Add(host);
        tabStripW += w + DpiScale(UiTokens::TabCloseW);
    }
    // The strip must occupy only its actual content so the static + button stays
    // immediately after the final tab; the following spacer consumes the remainder.
    m_pTabStrip->SetFixedWidth((std::max)(DpiScale(1), tabStripW));
    m_pTabStrip->NeedUpdate();
    m_updatingTabs = false;
}

void CMainWnd::AddTab(const std::wstring& path, bool activate)
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
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (PathEquals(m_tabs[i].path, target)) {
            if (activate)
                ActivateTab(i);
            return;
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
