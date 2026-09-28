// FastFile - favorites bar, quick access pins, left-nav splitter
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

void CMainWnd::OnFavoriteClicked(const CDuiString& name)
{
    if (name == _T("fav_thispc")) {
        AddTab(kThisPcPath, true);
        return;
    }
    if (name == _T("fav_documents")) {
        std::wstring p = GetKnownFolderPath(CSIDL_PERSONAL);
        if (p.empty()) { UpdateStatus(_T("无法定位文档文件夹")); return; }
        AddTab(p, true);
        return;
    }
    if (name == _T("fav_desktop")) {
        std::wstring p = GetKnownFolderPath(CSIDL_DESKTOPDIRECTORY);
        if (p.empty()) { UpdateStatus(_T("无法定位桌面")); return; }
        AddTab(p, true);
        return;
    }
    if (name == _T("fav_downloads")) {
        std::wstring p = GetDownloadsPath();
        if (p.empty()) { UpdateStatus(_T("无法定位下载文件夹")); return; }
        AddTab(p, true);
        return;
    }
}

void CMainWnd::UpdateFavoritesHighlight()
{
    const std::wstring docs = GetKnownFolderPath(CSIDL_PERSONAL);
    const std::wstring desk = GetKnownFolderPath(CSIDL_DESKTOPDIRECTORY);
    const std::wstring downs = GetDownloadsPath();

    struct FavBtn { LPCTSTR name; bool active; };
    const FavBtn btns[] = {
        { _T("fav_thispc"), IsThisPcPath(m_currentPath) },
        { _T("fav_documents"), PathEquals(m_currentPath, docs) },
        { _T("fav_desktop"), PathEquals(m_currentPath, desk) },
        { _T("fav_downloads"), PathEquals(m_currentPath, downs) },
    };

    for (const auto& b : btns) {
        auto* btn = static_cast<CButtonUI*>(m_PaintManager.FindControl(b.name));
        if (!btn) continue;
        if (b.active) {
            btn->SetAttribute(_T("bkcolor"), UiTokens::ColorNavSelected);
            btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
            btn->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
            btn->SetAttribute(_T("bordersize"), _T("0"));
        } else {
            btn->SetAttribute(_T("bkcolor"), UiTokens::ColorSurface);
            btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
            btn->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
            btn->SetAttribute(_T("bordersize"), _T("0"));
        }
        btn->Invalidate();
    }

    auto stylePin = [&](CContainerUI* host) {
        if (!host) return;
        const int n = host->GetCount();
        for (int i = 0; i < n; ++i) {
            CControlUI* c = host->GetItemAt(i);
            if (!c) continue;
            CDuiString ud = c->GetUserData();
            const bool active = !ud.IsEmpty() && PathEquals(m_currentPath, ud.GetData());
            if (active) {
                c->SetAttribute(_T("bkcolor"), UiTokens::ColorNavSelected);
                c->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
            } else {
                CDuiString nm = c->GetName();
                if (nm.Find(_T("fav_dyn_")) == 0)
                    c->SetAttribute(_T("bkcolor"), UiTokens::ColorSurface);
                else
                    c->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
                c->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
            }
            c->Invalidate();
        }
    };
    stylePin(m_pFavoritesStrip);
    stylePin(m_pLeftFavPins);
}

// ---- C: left Quick Access / This PC splitter -----------------------------

std::wstring CMainWnd::GetLeftNavFilePath()
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
    return dir + L"\\left_nav.ini";
}

void CMainWnd::ApplyLeftNavSplitterHeight(int designHeight)
{
    if (designHeight < UiTokens::LeftQuickMinH) designHeight = UiTokens::LeftQuickMinH;
    if (designHeight > 720) designHeight = 720;
    m_leftQuickDesignH = designHeight;
    if (m_pLeftQuick) {
        // DuiLib's native layout splitter is reliable for child hit testing;
        // the former custom label grip was not receiving all mouse messages.
        m_pLeftQuick->SetSepHeight(DpiScale(UiTokens::LeftNavSepH));
        m_pLeftQuick->SetMinHeight(DpiScale(UiTokens::LeftQuickMinH));
        m_pLeftQuick->SetMaxHeight(DpiScale(720));
        m_pLeftQuick->SetFixedHeight(DpiScale(designHeight));
        m_pLeftQuick->NeedParentUpdate();
    }
}

void CMainWnd::LoadLeftNavSplitter()
{
    int h = m_leftQuickDesignH;
    std::wstring file = GetLeftNavFilePath();
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
                if (line.compare(0, 16, L"LeftQuickHeight=") == 0)
                    h = _wtoi(line.c_str() + 16);
            }
        }
        fclose(fp);
    }
    ApplyLeftNavSplitterHeight(h);
}

void CMainWnd::SaveLeftNavSplitter() const
{
    if (m_pLeftQuick) {
        const int phy = m_pLeftQuick->GetFixedHeight();
        if (phy > 0 && m_dpi > 0) {
            const_cast<CMainWnd*>(this)->m_leftQuickDesignH =
                (std::max)(UiTokens::LeftQuickMinH, ::MulDiv(phy, 96, static_cast<int>(m_dpi)));
        }
    }
    std::wstring path = GetLeftNavFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || !fp)
        return;
    unsigned char bom[2] = { 0xFF, 0xFE };
    fwrite(bom, 1, 2, fp);
    wchar_t buf[128];
    swprintf_s(buf, L"[LeftNav]\nLeftQuickHeight=%d\n", m_leftQuickDesignH);
    fwrite(buf, sizeof(wchar_t), wcslen(buf), fp);
    fclose(fp);
}

void CMainWnd::CaptureLeftNavSplitterIfChanged()
{
    if (!m_pLeftQuick) return;
    const int phy = m_pLeftQuick->GetFixedHeight();
    if (phy <= 0 || m_dpi == 0) return;
    const int design = (std::max)(UiTokens::LeftQuickMinH, ::MulDiv(phy, 96, static_cast<int>(m_dpi)));
    if (design != m_leftQuickDesignH) {
        m_leftQuickDesignH = design;
        SaveLeftNavSplitter();
    }
}

// ===== C: Favorites bar persistence + UI =====================================

std::wstring CMainWnd::GetFavoritesFilePath()
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
    return dir + L"\\favorites.txt";
}

std::wstring CMainWnd::GetQuickAccessFilePath()
{
    std::wstring path = GetFavoritesFilePath();
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"quick_access.txt" : path.substr(0, slash + 1) + L"quick_access.txt";
}

void CMainWnd::LoadQuickAccess()
{
    m_quickAccess.clear();
    FILE* fp = nullptr;
    const std::wstring file = GetQuickAccessFilePath();
    if (_wfopen_s(&fp, file.c_str(), L"rb") != 0 || !fp) return;
    fseek(fp, 0, SEEK_END);
    const long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 2) { fclose(fp); return; }
    std::wstring content(static_cast<size_t>(size) / sizeof(wchar_t), L'\0');
    fread(&content[0], 1, size, fp);
    fclose(fp);
    if (!content.empty() && content[0] == 0xFEFF) content.erase(content.begin());
    size_t pos = 0;
    while (pos < content.size()) {
        const size_t eol = content.find(L'\n', pos);
        std::wstring line = content.substr(pos, (eol == std::wstring::npos ? content.size() : eol) - pos);
        pos = eol == std::wstring::npos ? content.size() : eol + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const std::wstring path = NormalizePath(line);
        if (path.empty() || IsQuickAccessPinned(path)) continue;
        DWORD attrs = ::GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        FavoriteItem item{ path, GetLeafName(path) };
        if (item.displayName.empty()) item.displayName = path;
        m_quickAccess.push_back(std::move(item));
    }
}

void CMainWnd::SaveQuickAccess() const
{
    FILE* fp = nullptr;
    const std::wstring file = GetQuickAccessFilePath();
    if (_wfopen_s(&fp, file.c_str(), L"wb") != 0 || !fp) return;
    const wchar_t bom = 0xFEFF;
    fwrite(&bom, sizeof(bom), 1, fp);
    for (const auto& item : m_quickAccess) {
        fwrite(item.path.c_str(), sizeof(wchar_t), item.path.size(), fp);
        const wchar_t nl = L'\n';
        fwrite(&nl, sizeof(nl), 1, fp);
    }
    fclose(fp);
}

bool CMainWnd::IsQuickAccessPinned(const std::wstring& path) const
{
    for (const auto& item : m_quickAccess)
        if (PathEquals(item.path, path)) return true;
    return false;
}

bool CMainWnd::PinQuickAccess(const std::wstring& path)
{
    const std::wstring normalized = NormalizePath(path);
    if (normalized.empty() || IsThisPcPath(normalized) || IsQuickAccessPinned(normalized)) return false;
    DWORD attrs = ::GetFileAttributesW(normalized.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;
    FavoriteItem item{ normalized, GetLeafName(normalized) };
    if (item.displayName.empty()) item.displayName = normalized;
    m_quickAccess.push_back(std::move(item));
    SaveQuickAccess();
    RebuildLeftPinnedFavorites();
    return true;
}

bool CMainWnd::UnpinQuickAccess(const std::wstring& path)
{
    const auto end = std::remove_if(m_quickAccess.begin(), m_quickAccess.end(),
        [&](const FavoriteItem& item) { return PathEquals(item.path, path); });
    if (end == m_quickAccess.end()) return false;
    m_quickAccess.erase(end, m_quickAccess.end());
    SaveQuickAccess();
    RebuildLeftPinnedFavorites();
    return true;
}

void CMainWnd::LoadFavorites()
{
    m_favorites.clear();
    std::wstring file = GetFavoritesFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, file.c_str(), L"rb") != 0 || !fp)
        return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 2) { fclose(fp); return; }
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
        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
        std::wstring path = NormalizePath(line);
        if (path.empty()) continue;
        DWORD attrs = ::GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
            continue;
        if (IsFavoritePinned(path)) continue;
        FavoriteItem it;
        it.path = path;
        it.displayName = GetLeafName(path);
        if (it.displayName.empty()) it.displayName = path;
        m_favorites.push_back(std::move(it));
    }
}

void CMainWnd::SaveFavorites() const
{
    std::wstring file = GetFavoritesFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, file.c_str(), L"wb") != 0 || !fp)
        return;
    wchar_t bom = 0xFEFF;
    fwrite(&bom, sizeof(bom), 1, fp);
    for (const auto& it : m_favorites) {
        fwrite(it.path.c_str(), sizeof(wchar_t), it.path.size(), fp);
        wchar_t nl = L'\n';
        fwrite(&nl, sizeof(nl), 1, fp);
    }
    fclose(fp);
}

bool CMainWnd::IsFavoritePinned(const std::wstring& path) const
{
    std::wstring n = NormalizePath(path);
    for (const auto& it : m_favorites) {
        if (PathEquals(it.path, n)) return true;
    }
    return false;
}

bool CMainWnd::PinFavorite(const std::wstring& path)
{
    std::wstring n = NormalizePath(path);
    if (n.empty() || IsThisPcPath(n)) return false;
    DWORD attrs = ::GetFileAttributesW(n.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return false;
    if (IsFavoritePinned(n)) return false;
    FavoriteItem it;
    it.path = n;
    it.displayName = GetLeafName(n);
    if (it.displayName.empty()) it.displayName = n;
    m_favorites.push_back(std::move(it));
    SaveFavorites();
    RebuildFavoritesBar();
    return true;
}

bool CMainWnd::UnpinFavorite(const std::wstring& path)
{
    std::wstring n = NormalizePath(path);
    auto it = std::remove_if(m_favorites.begin(), m_favorites.end(),
        [&](const FavoriteItem& f) { return PathEquals(f.path, n); });
    if (it == m_favorites.end()) return false;
    m_favorites.erase(it, m_favorites.end());
    SaveFavorites();
    RebuildFavoritesBar();
    return true;
}

bool CMainWnd::IsOverFavoritesBar(POINT ptClient) const
{
    auto contains = [&](CControlUI* c) -> bool {
        if (!c || !c->IsVisible()) return false;
        RECT rc = c->GetPos();
        return ptClient.x >= rc.left && ptClient.x < rc.right
            && ptClient.y >= rc.top && ptClient.y < rc.bottom;
    };
    if (contains(m_pFavoritesBar)) return true;
    if (contains(m_pFavoritesStrip)) return true;
    return false;
}

void CMainWnd::RebuildFavoritesBar()
{
    if (!m_pFavoritesStrip) return;
    m_pFavoritesStrip->RemoveAll();
    const bool empty = m_favorites.empty();
    m_pFavoritesStrip->SetVisible(!empty);
    int stripW = 0;

    // Match fav_bar_label: font 0 (FontBody), vertically centered icon+text (no clip).
    const int iconPx = DpiScale(UiTokens::FavIconPx);
    const int btnH = DpiScale(UiTokens::FavChipH);

    for (size_t i = 0; i < m_favorites.size(); ++i) {
        const auto& fav = m_favorites[i];
        auto* btn = new CButtonUI;
        CDuiString name;
        name.Format(_T("fav_pin_%d"), (int)i);
        btn->SetName(name);
        btn->SetText(fav.displayName.c_str());
        btn->SetUserData(fav.path.c_str());
        btn->SetFixedHeight(btnH);
        btn->SetAttribute(_T("align"), _T("left"));
        btn->SetAttribute(_T("valign"), _T("vcenter"));
        btn->SetAttribute(_T("font"), _T("0"));
        btn->SetAttribute(_T("bkcolor"), _T("#00FFFFFF"));
        btn->SetAttribute(_T("hotbkcolor"), _T("#FFE8E8E8"));
        btn->SetAttribute(_T("pushedbkcolor"), _T("#FFDADADA"));
        btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        btn->SetAttribute(_T("bordercolor"), _T("#00FFFFFF"));
        btn->SetAttribute(_T("bordersize"), _T("0"));
        btn->SetAttribute(_T("endellipsis"), _T("true"));
        {
            CDuiString tp;
            // left room for 16px icon + gap; no vertical pad (valign centers)
            tp.Format(_T("%d,0,%d,0"), DpiScale(UiTokens::FavIconPx + 8), DpiScale(6));
            btn->SetAttribute(_T("textpadding"), tp);
        }
        // CJK-friendly width (~13px/glyph @12pt) + icon gutter
        int w = DpiScale(static_cast<int>(fav.displayName.size()) * 13 + 36);
        if (w < DpiScale(84)) w = DpiScale(84);
        if (w > DpiScale(240)) w = DpiScale(240);
        btn->SetFixedWidth(w);
        btn->SetToolTip(fav.path.c_str());

        std::wstring bmp = GetShellIconBmp(fav.path, true, iconPx);
        if (bmp.empty()) bmp = GetStockIconBmp(SIID_FOLDER, iconPx);
        if (!bmp.empty()) {
            const int y = (btnH - iconPx) / 2; // vertical center with label
            ApplyControlForeIcon(btn, bmp, iconPx, DpiScale(4), y, false);
        }
        m_pFavoritesStrip->Add(btn);
        stripW += w;
    }

    if (!empty)
        m_pFavoritesStrip->SetFixedWidth((std::max)(DpiScale(1), stripW));

    if (CControlUI* hint = m_PaintManager.FindControl(_T("fav_bar_hint"))) {
        hint->SetVisible(empty);
    }
    m_pFavoritesStrip->NeedUpdate();
    if (m_pFavoritesBar) m_pFavoritesBar->NeedUpdate();
    UpdateFavoritesHighlight();
}

void CMainWnd::RebuildLeftPinnedFavorites()
{
    if (!m_pLeftFavPins) return;
    m_pLeftFavPins->RemoveAll();
    m_pLeftFavPins->SetVisible(!m_quickAccess.empty());
    const int iconPx = DpiScale(UiTokens::NavIconPx);
    const int rowH = DpiScale(UiTokens::NavRowH);
    for (size_t i = 0; i < m_quickAccess.size(); ++i) {
        const auto& entry = m_quickAccess[i];
        auto* btn = new CButtonUI;
        CDuiString name;
        name.Format(_T("fav_dyn_%d"), (int)i);
        btn->SetName(name);
        btn->SetText(entry.displayName.c_str());
        btn->SetUserData(entry.path.c_str());
        btn->SetFixedHeight(rowH);
        btn->SetAttribute(_T("align"), _T("left"));
        btn->SetAttribute(_T("valign"), _T("vcenter"));
        btn->SetAttribute(_T("font"), _T("0"));
        btn->SetAttribute(_T("endellipsis"), _T("true"));
        btn->SetAttribute(_T("bkcolor"), UiTokens::ColorSurface);
        btn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorNavHover);
        btn->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorNavSelected);
        btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        CDuiString textPad;
        textPad.Format(_T("%d,0,%d,0"), DpiScale(UiTokens::NavIconPad + UiTokens::NavIconPx + UiTokens::NavIconTextGap + 4), DpiScale(UiTokens::NavTextPadR));
        btn->SetAttribute(_T("textpadding"), textPad.GetData());
        std::wstring icon = GetShellIconBmp(entry.path, true, iconPx);
        if (icon.empty()) icon = GetStockIconBmp(SIID_FOLDER, iconPx);
        if (!icon.empty())
            ApplyControlForeIcon(btn, icon, iconPx, DpiScale(UiTokens::NavIconPad + 4), (rowH - iconPx) / 2, false);
        m_pLeftFavPins->Add(btn);
    }
    const int minimum = UiTokens::LeftQuickMinH + static_cast<int>(m_quickAccess.size()) * UiTokens::NavRowH;
    if (m_leftQuickDesignH < minimum)
        ApplyLeftNavSplitterHeight(minimum);
    m_pLeftFavPins->NeedUpdate();
    if (m_pLeftQuick) m_pLeftQuick->NeedUpdate();
}

void CMainWnd::OnPinnedFavoriteClick(CControlUI* btn)
{
    if (!btn) return;
    CDuiString ud = btn->GetUserData();
    if (ud.IsEmpty()) return;
    AddTab(ud.GetData(), true);
}

void CMainWnd::ShowFavoriteContextMenu(CControlUI* btn, POINT ptScreen)
{
    if (!btn) return;
    CDuiString ud = btn->GetUserData();
    if (ud.IsEmpty()) return;
    std::wstring path = ud.GetData();
    const bool quickAccess = btn->GetName().Find(_T("fav_dyn_")) == 0;

    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;
    ::AppendMenuW(hMenu, MF_STRING, kCmdFavOpen, L"\u6253\u5f00");
    ::AppendMenuW(hMenu, MF_STRING, kCmdFavUnpin,
        quickAccess ? L"\u4ece\u5feb\u901f\u8bbf\u95ee\u53d6\u6d88\u56fa\u5b9a" : L"\u4ece\u6536\u85cf\u680f\u53d6\u6d88\u56fa\u5b9a");
    UINT cmd = ::TrackPopupMenuEx(hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        ptScreen.x, ptScreen.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
    if (cmd == kCmdFavOpen) {
        AddTab(path, true);
    } else if (cmd == kCmdFavUnpin) {
        if ((quickAccess ? UnpinQuickAccess(path) : UnpinFavorite(path)))
            UpdateStatus(quickAccess ? _T("已从快速访问取消固定") : _T("已从收藏栏取消固定"));
    }
}
