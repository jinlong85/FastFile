// FastFile - favorites bar, quick access pins, left-nav splitter
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"
#include "FavoriteStarUI.h"
#include "FavoritesJson.h"
#include "ShellPresentation.h"

namespace {

bool IsCjk(wchar_t c)
{
    return (c >= 0x2E80 && c <= 0x9FFF)      // radicals .. CJK unified
        || (c >= 0xF900 && c <= 0xFAFF)      // compatibility ideographs
        || (c >= 0xFF00 && c <= 0xFF60);     // fullwidth forms
}

// Folder names like "绝密较量Jue mi ji" or "太平年.Swords into Plowshares" glue a Chinese
// title onto its pinyin/English translation, which reads as garbage in a 168px chip. When a
// name mixes CJK and Latin letters, keep just the leading CJK title (the user asked for
// "拆开或只留一个"), provided it is a real title (2+ characters). Names that are purely CJK,
// purely Latin, or CJK + digits are untouched.
std::wstring CleanFavoriteLabel(const std::wstring& raw)
{
    std::wstring name = raw;
    while (!name.empty() && (name.front() == L' ' || name.front() == L'\t'))
        name.erase(name.begin());
    while (!name.empty() && (name.back() == L' ' || name.back() == L'\t'))
        name.pop_back();
    if (name.empty())
        return name;

    bool hasCjk = false, hasLatin = false;
    for (wchar_t c : name) {
        if (IsCjk(c)) hasCjk = true;
        else if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')) hasLatin = true;
    }
    if (!hasCjk || !hasLatin)
        return name;

    // Leading CJK run (the title); stop at the first Latin/digit after it.
    std::wstring cjk;
    for (wchar_t c : name) {
        if (IsCjk(c)) {
            cjk.push_back(c);
        } else if (!cjk.empty() && c != L' ' && c != L'.' && c != L'-' && c != L'_') {
            break;
        }
    }
    return cjk.size() >= 2 ? cjk : name;
}

} // namespace

void CMainWnd::UpdateFavoritesHighlight()
{
    if (auto* star = static_cast<CFavoriteStarUI*>(m_PaintManager.FindControl(_T("btn_favorite_toggle")))) {
        const bool pinned = !IsThisPcPath(m_currentPath) && IsFavoritePinned(m_currentPath);
        star->SetPinned(pinned);
        star->SetToolTip(IsThisPcPath(m_currentPath) ? L"此电脑不能收藏" :
            pinned ? L"取消收藏当前文件夹" : L"收藏当前文件夹");
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
    UpdateQuickRowHighlight();
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
        UpdateLeftQuickAccessSpacing();
        m_pLeftQuick->NeedParentUpdate();
    }
}

// Grab band for the 快速访问 / 此电脑 divider. DuiLib's sep band only covers the container's
// last few pixels, so the visible line (the divider host below it) was not draggable at all;
// this band straddles the boundary like the pane dividers do.
bool CMainWnd::HitTestLeftNavDivider(int clientX, int clientY) const
{
    if (!m_pLeftQuick || !m_pLeftQuick->IsVisible()) return false;
    const RECT r = m_pLeftQuick->GetPos();
    if (r.bottom <= r.top || r.right <= r.left) return false;
    // DuiLib lays the next sibling out below the child's padding (CVerticalLayoutUI::SetPos
    // advances by height + padding.top + padding.bottom), and UpdateLeftQuickAccessSpacing
    // centres the rows with half the slack top and bottom. The visible divider therefore sits
    // padding.bottom below the control rect, not at r.bottom - a band around r.bottom missed
    // the line completely once the block was taller than its content.
    const RECT pad = m_pLeftQuick->GetPadding();
    const int boundary = r.bottom + pad.bottom;
    const int band = DpiScale(12);
    if (clientY < boundary - band || clientY > boundary + band) return false;
    return clientX >= r.left && clientX < r.right;
}

// Keep the Quick Access rows visually centred in their resizable region.
// In the old top-aligned layout all spare height accumulated below the last item,
// so “此电脑” looked glued to the top while the final folder floated far above the
// section divider. This also adapts when users add their own quick-access folders.
void CMainWnd::UpdateLeftQuickAccessSpacing()
{
    if (!m_pLeftQuick) return;
    const int rowCount = (std::max)(1, static_cast<int>(m_quickRows.size()));

    const int rowHeight = DpiScale(m_settings.NavigationRowHeight());
    const int height = m_pLeftQuick->GetFixedHeight();
    if (height <= 0 || rowHeight <= 0) return;
    const int slack = (std::max)(0, height - rowCount * rowHeight);
    const int top = slack / 2;
    const int bottom = slack - top;
    const int left = DpiScale(UiTokens::NavIconPad);
    const int right = DpiScale(2);
    const RECT old = m_pLeftQuick->GetPadding();
    if (old.left == left && old.top == top && old.right == right && old.bottom == bottom)
        return;
    m_pLeftQuick->SetPadding({ left, top, right, bottom });
    m_pLeftQuick->NeedParentUpdate();
}

// One place for the Quick Access row metrics. Every row (built-in or pinned) is created at
// runtime, so they share the row padding, icon offset and text padding. DuiLib offsets a
// laid-out child by its own padding (CVerticalLayoutUI::SetPos), which is what keeps the
// rows inset from the panel edge and makes the hover/selected band match the old XML rows.
void CMainWnd::ApplyQuickAccessRow(CControlUI* row, const std::wstring& iconBmp)
{
    if (!row) return;
    const int pad = DpiScale(UiTokens::NavIconPad);
    const int iconPx = DpiScale(UiTokens::NavIconPx);
    row->SetAttribute(_T("font"), _T("4"));   // Segoe UI 12 with system CJK fallback, shared with tree
    CDuiString rowPad;
    rowPad.Format(_T("%d,0,%d,0"), pad, pad);
    row->SetAttribute(_T("padding"), rowPad.GetData());

    if (!iconBmp.empty()) {
        int bh = row->GetFixedHeight();
        if (bh <= 0) bh = DpiScale(m_settings.NavigationRowHeight());
        int y = (bh - iconPx) / 2;
        if (y < 0) y = 0;
        ApplyControlForeIcon(row, iconBmp, iconPx, pad, y, false);
    }

    CDuiString textPad;
    // icon inset + icon + gap, measured from the row's padded box
    textPad.Format(_T("%d,0,%d,0"),
        pad + iconPx + DpiScale(UiTokens::NavIconTextGap), DpiScale(UiTokens::NavTextPadR));
    row->SetAttribute(_T("textpadding"), textPad.GetData());
    row->Invalidate();
}

void CMainWnd::LoadLeftNavSplitter()
{
    int h = m_leftQuickDesignH;
    int leftW = m_leftPanelDesignW;
    int previewW = m_previewPaneDesignW;
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
                else if (line.compare(0, 14, L"QuickFitRows2=") == 0)
                    m_quickFitRows = _wtoi(line.c_str() + 14);
                else if (line.compare(0, 11, L"LeftPanelW=") == 0)
                    leftW = _wtoi(line.c_str() + 11);
                else if (line.compare(0, 9, L"PreviewW=") == 0)
                    previewW = _wtoi(line.c_str() + 9);
            }
        }
        fclose(fp);
    }
    ApplyLeftNavSplitterHeight(h);
    ApplyPaneWidths(leftW, previewW);
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
    wchar_t buf[160];
    swprintf_s(buf,
        L"[LeftNav]\nLeftQuickHeight=%d\nLeftPanelW=%d\nPreviewW=%d\nQuickFitRows2=%d\n",
        m_leftQuickDesignH, m_leftPanelDesignW, m_previewPaneDesignW, m_quickFitRows);
    fwrite(buf, sizeof(wchar_t), wcslen(buf), fp);
    fclose(fp);
}

// Sidebar / preview widths are design units (persisted) -> physical for the layout.
void CMainWnd::ApplyPaneWidths(int leftDesignW, int previewDesignW)
{
    if (leftDesignW < 150) leftDesignW = 150;
    if (leftDesignW > 520) leftDesignW = 520;
    // Keep saved pane widths consistent with the preview's XML minimum.  Older
    // left_nav.ini files may contain the former 180px minimum, which is too narrow
    // for the two-column metadata table.
    if (previewDesignW < 260) previewDesignW = 260;
    if (previewDesignW > 760) previewDesignW = 760;
    m_leftPanelDesignW = leftDesignW;
    m_previewPaneDesignW = previewDesignW;
    if (m_pLeftPanel) {
        m_pLeftPanel->SetMinWidth(DpiScale(150));
        m_pLeftPanel->SetMaxWidth(DpiScale(520));
        m_pLeftPanel->SetFixedWidth(DpiScale(leftDesignW));
    }
    if (m_pPreviewPane) {
        m_pPreviewPane->SetMinWidth(DpiScale(260));
        m_pPreviewPane->SetMaxWidth(DpiScale(760));
        m_pPreviewPane->SetFixedWidth(DpiScale(previewDesignW));
    }
    // Widths changed -> the fitted preview thumb (and the list column split) must follow.
    if (m_pLeftPanel || m_pPreviewPane) {
        if (m_pLeftPanel) m_pLeftPanel->NeedParentUpdate();
        if (m_pPreviewPane) m_pPreviewPane->NeedParentUpdate();
    }
}

// ---- Pane dividers (sidebar / preview) -----------------------------------
// DuiLib's own sep band is only a few pixels wide and sits strictly inside the padded
// area, which made the splitters almost impossible to grab. We hit-test a band that
// straddles the divider line instead, so the cursor turns into the resize arrow as soon
// as the mouse gets near it.
int CMainWnd::PaneDividerBandPx() const
{
    return (std::max)(8, DpiScale(8));   // +/- this many px around the divider line
}

int CMainWnd::HitTestPaneDivider(int clientX, int clientY) const
{
    // The side pane's scroll track and draggable thumb take priority over the nearby
    // width divider.  Otherwise the divider's generous grab band steals the cursor
    // and click from the scroll thumb at the tree's right edge.
    if (IsPaneScrollBarHit(clientX, clientY))
        return 0;

    const int band = PaneDividerBandPx();
    if (m_pLeftPanel) {
        const RECT r = m_pLeftPanel->GetPos();
        // The divider is only active beside the actual body pane.  Its x-coordinate is
        // shared with the tabs/toolbars above, so ignoring y here made those controls
        // incorrectly show the horizontal-resize cursor and start a pane drag.
        if (r.right > r.left && r.bottom > r.top
            && clientY >= r.top && clientY < r.bottom
            && clientX >= r.right - band && clientX <= r.right + band)
            return 1;
    }
    // The preview pane has no band of its own: its width grip is merged into the
    // preview scroll rail (IsPreviewScrollBarHit), which hugs the divider on the
    // pane's side.  A +/- band here used to swallow the file list's own scrollbar,
    // which sits immediately left of the divider, so dragging that bar resized the
    // preview instead of scrolling the list.
    return 0;
}

bool CMainWnd::IsPaneScrollBarHit(int clientX, int clientY) const
{
    const auto contains = [clientX, clientY](CScrollBarUI* bar) {
        if (!bar || !bar->IsVisible()) return false;
        const RECT r = bar->GetPos();
        return clientX >= r.left && clientX < r.right
            && clientY >= r.top && clientY < r.bottom;
    };
    if (m_pDirTree && (contains(m_pDirTree->GetVerticalScrollBar())
        || contains(m_pDirTree->GetHorizontalScrollBar())))
        return true;
    return IsPreviewScrollBarHit(clientX, clientY);
}

bool CMainWnd::IsPreviewScrollBarHit(int clientX, int clientY) const
{
    // Single merged surface: the rail column that doubles as the preview scrollbar.
    // Vertical drags scroll the preview, horizontal drags resize the pane, and the
    // cursor turns into the resize arrow here (WM_SETCURSOR).
    if (!m_pPreviewRail || !m_pPreviewRail->IsVisible()) return false;
    if (m_pPreviewPane && !m_pPreviewPane->IsVisible()) return false;
    const RECT r = m_pPreviewRail->GetPos();
    if (r.right <= r.left || r.bottom <= r.top) return false;
    return clientX >= r.left && clientX < r.right
        && clientY >= r.top && clientY < r.bottom;
}

void CMainWnd::ApplyPaneDragWidth(int kind, int physicalWidth)
{
    const int minD = (kind == 1) ? 150 : 260;
    const int maxD = (kind == 1) ? 520 : 760;
    int w = physicalWidth;
    if (w < DpiScale(minD)) w = DpiScale(minD);
    if (w > DpiScale(maxD)) w = DpiScale(maxD);
    CContainerUI* pane = (kind == 1) ? m_pLeftPanel : m_pPreviewPane;
    if (!pane) return;
    if (pane->GetFixedWidth() == w) return;
    pane->SetFixedWidth(w);
    pane->NeedParentUpdate();
}

void CMainWnd::CapturePaneWidthsIfChanged()
{
    if (m_dpi == 0) return;
    bool changed = false;
    if (m_pLeftPanel) {
        const int phy = m_pLeftPanel->GetFixedWidth();
        if (phy > 0) {
            const int design = ::MulDiv(phy, 96, static_cast<int>(m_dpi));
            if (design != m_leftPanelDesignW) { m_leftPanelDesignW = design; changed = true; }
        }
    }
    if (m_pPreviewPane) {
        const int phy = m_pPreviewPane->GetFixedWidth();
        if (phy > 0) {
            const int design = ::MulDiv(phy, 96, static_cast<int>(m_dpi));
            if (design != m_previewPaneDesignW) { m_previewPaneDesignW = design; changed = true; }
        }
    }
    if (changed) SaveLeftNavSplitter();
}

void CMainWnd::CaptureLeftNavSplitterIfChanged()
{
    if (!m_pLeftQuick) return;
    const int phy = m_pLeftQuick->GetFixedHeight();
    if (phy <= 0 || m_dpi == 0) return;
    const int design = (std::max)(UiTokens::LeftQuickMinH, ::MulDiv(phy, 96, static_cast<int>(m_dpi)));
    if (design != m_leftQuickDesignH) {
        m_leftQuickDesignH = design;
        UpdateLeftQuickAccessSpacing();
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
    return dir + L"\\favorites.json";
}

std::wstring CMainWnd::GetQuickAccessFilePath()
{
    std::wstring path = GetFavoritesFilePath();
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"quick_access.txt" : path.substr(0, slash + 1) + L"quick_access.txt";
}

HRESULT CMainWnd::InvokeQuickVerb(HWND owner, const std::wstring& identity, const char* verb, const std::vector<BYTE>& shellId)
{
    PIDLIST_ABSOLUTE absolute = shellId.empty() ? nullptr : ILCloneFull(reinterpret_cast<PCIDLIST_ABSOLUTE>(shellId.data()));
    HRESULT hr = shellId.empty() ? SHParseDisplayName(identity.c_str(), nullptr, &absolute, 0, nullptr) : (absolute ? S_OK : E_OUTOFMEMORY);
    if (FAILED(hr)) return hr;
    IShellFolder* parent = nullptr;
    PCUITEMID_CHILD child = nullptr;
    hr = SHBindToParent(absolute, IID_PPV_ARGS(&parent), &child);
    IContextMenu* menu = nullptr;
    if (SUCCEEDED(hr)) hr = parent->GetUIObjectOf(owner, 1, &child, IID_IContextMenu, nullptr, reinterpret_cast<void**>(&menu));
    HMENU popup = CreatePopupMenu();
    if (SUCCEEDED(hr) && !popup) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) hr = menu->QueryContextMenu(popup, 0, 1, 0x7fff, CMF_NORMAL);
    if (SUCCEEDED(hr)) {
        const UINT last = 1 + HRESULT_CODE(hr);
        hr = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        for (UINT id = 1; id < last; ++id) {
            char name[128]{};
            if (SUCCEEDED(menu->GetCommandString(id - 1, GCS_VERBA, nullptr, name, sizeof(name))) && _stricmp(name, verb) == 0) {
                CMINVOKECOMMANDINFO invoke{};
                invoke.cbSize = sizeof(invoke); invoke.hwnd = owner;
                invoke.fMask = CMIC_MASK_NOASYNC;
                invoke.lpVerb = MAKEINTRESOURCEA(id - 1); invoke.nShow = SW_SHOWNORMAL;
                hr = menu->InvokeCommand(&invoke);
                break;
            }
        }
    }
    if (popup) DestroyMenu(popup);
    if (menu) menu->Release();
    if (parent) parent->Release();
    CoTaskMemFree(absolute);
    return hr;
}

HRESULT CMainWnd::ReadSystemQuickRows(std::vector<QuickRow>& rows, const std::wstring& source)
{
    // Shell owns ranking, pin order, privacy filtering and cloud/virtual items.
    // Never parse or rewrite AutomaticDestinations or synthesize recent entries.
    PIDLIST_ABSOLUTE root = nullptr;
    HRESULT hr = SHParseDisplayName(source.empty() ? L"shell:::{f874310e-b6b7-47dc-bc84-b9e6b38f5903}" : source.c_str(), nullptr, &root, 0, nullptr);
    if (FAILED(hr) && source.empty())
        hr = SHParseDisplayName(L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}", nullptr, &root, 0, nullptr);
    if (FAILED(hr)) return hr;
    IShellFolder* desktop = nullptr;
    IShellFolder* folder = nullptr;
    hr = SHGetDesktopFolder(&desktop);
    if (SUCCEEDED(hr)) hr = desktop->BindToObject(root, nullptr, IID_PPV_ARGS(&folder));
    if (desktop) desktop->Release();
    IEnumIDList* items = nullptr;
    if (SUCCEEDED(hr)) hr = folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &items);
    std::vector<QuickRow> result;
    if (SUCCEEDED(hr) && items) {
        PITEMID_CHILD child = nullptr;
        while ((hr = items->Next(1, &child, nullptr)) == S_OK) {
            QuickRow row;
            PIDLIST_ABSOLUTE absolute = ILCombine(root, child);
            if (absolute) {
                const auto bytes = reinterpret_cast<const BYTE*>(absolute);
                row.shellId.assign(bytes, bytes + ILGetSize(absolute));
            }
            PWSTR text = nullptr;
            HRESULT itemResult = absolute ? SHGetNameFromIDList(absolute, SIGDN_DESKTOPABSOLUTEPARSING, &text) : E_OUTOFMEMORY;
            if (SUCCEEDED(itemResult)) { row.shellPath = text; CoTaskMemFree(text); text = nullptr; }
            if (SUCCEEDED(itemResult)) itemResult = SHGetNameFromIDList(absolute, SIGDN_NORMALDISPLAY, &text);
            if (SUCCEEDED(itemResult)) { row.label = text; CoTaskMemFree(text); text = nullptr; }
            // Home item identities may be virtual; keep them usable if no file-system path exists.
            if (absolute && SUCCEEDED(SHGetNameFromIDList(absolute, SIGDN_FILESYSPATH, &text))) {
                row.path = text; CoTaskMemFree(text); text = nullptr;
            } else row.path = row.shellPath;
            SFGAOF attributes = SFGAO_FOLDER;
            PCUITEMID_CHILD relative = child;
            if (SUCCEEDED(itemResult)) itemResult = folder->GetAttributesOf(1, &relative, &attributes);
            row.isFolder = (attributes & SFGAO_FOLDER) != 0;
            IShellFolder2* properties = nullptr;
            if (SUCCEEDED(itemResult) && (source.empty() || source.find(L"shell:::{") == 0)
                && SUCCEEDED(folder->QueryInterface(IID_PPV_ARGS(&properties)))) {
                VARIANT pinned{};
                if (SUCCEEDED(properties->GetDetailsEx(relative, &PKEY_Home_IsPinned, &pinned)) && pinned.vt == VT_BOOL)
                    row.pinned = pinned.boolVal != VARIANT_FALSE;
                VariantClear(&pinned);
                properties->Release();
            }
            if (absolute) CoTaskMemFree(absolute);
            CoTaskMemFree(child); child = nullptr;
            if (FAILED(itemResult)) { hr = itemResult; break; }
            result.push_back(std::move(row));
        }
    }
    if (items) items->Release();
    if (folder) folder->Release();
    CoTaskMemFree(root);
    if (FAILED(hr) && source.empty()) return ReadSystemQuickRows(rows, L"shell:::{679f85cb-0220-4080-b29b-5540cc05aab6}");
    if (SUCCEEDED(hr)) rows.swap(result); // S_FALSE is a valid empty enumeration.
    return hr;
}

void CMainWnd::ApplyQuickSnapshot(const QuickSnapshot& snapshot)
{
    if (FAILED(snapshot.result)) return; // transient failure does not erase a valid snapshot
    GUITHREADINFO interaction{}; interaction.cbSize = sizeof(interaction);
    if (m_quickDragIndex >= 0 || m_inDoDragDrop || m_pCtxMenu2 || m_pCtxMenu3
        || (GetGUIThreadInfo(0, &interaction) && (interaction.flags & GUI_INMENUMODE)))
        return; // do not destroy rows during a press, native menu or nested OLE loop
    const auto equal = [](const QuickRow& a, const QuickRow& b) {
        return a.path == b.path && a.label == b.label && a.shellPath == b.shellPath
            && a.shellId == b.shellId && a.isFolder == b.isFolder && a.pinned == b.pinned;
    };
    if (m_quickRows.size() == snapshot.rows.size() && std::equal(m_quickRows.begin(), m_quickRows.end(), snapshot.rows.begin(), equal)) return;
    m_quickRows = snapshot.rows;
    RebuildLeftQuickRows();
}

void CMainWnd::LoadQuickAccess()
{
    if (!m_hWnd || m_quickReadPending || m_quickReadStopping) return;
    if (m_quickReadThread.joinable()) m_quickReadThread.join();
    m_quickReadPending = true;
    const HWND target = m_hWnd;
    const std::wstring source = m_quickReadSource;
    m_quickReadThread = std::thread([this, target, source] {
        auto snapshot = std::make_unique<QuickSnapshot>();
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        snapshot->result = SUCCEEDED(init) ? ReadSystemQuickRows(snapshot->rows, source) : init;
        if (SUCCEEDED(init)) CoUninitialize();
        if (!m_quickReadStopping && ::PostMessageW(target, kMsgQuickAccessReady, 0, reinterpret_cast<LPARAM>(snapshot.get()))) snapshot.release();
    });
}

void CMainWnd::StopQuickAccessSync()
{
    m_quickReadStopping = true;
    if (m_hWnd) KillTimer(m_hWnd, kTimerQuickAccessSync);
    if (m_quickReadThread.joinable()) m_quickReadThread.join();
    MSG message{};
    while (m_hWnd && PeekMessageW(&message, m_hWnd, kMsgQuickAccessReady, kMsgQuickAccessReady, PM_REMOVE))
        delete reinterpret_cast<QuickSnapshot*>(message.lParam);
    m_quickReadPending = false;
}

void CMainWnd::SaveQuickAccess() const { } // system owns persistence

bool CMainWnd::IsQuickAccessPinned(const std::wstring& path) const
{
    for (const auto& row : m_quickRows)
        if (row.pinned && PathEquals(row.path, path)) return true;
    return false;
}

bool CMainWnd::PinQuickAccess(const std::wstring& path)
{
    const bool success = SUCCEEDED(InvokeQuickVerb(m_hWnd, path, "pintohome"));
    LoadQuickAccess();
    return success;
}

bool CMainWnd::UnpinQuickAccess(const std::wstring& path)
{
    for (const auto& row : m_quickRows) {
        if (row.pinned && PathEquals(row.path, path)) {
            const bool success = SUCCEEDED(InvokeQuickVerb(m_hWnd, row.shellPath, "unpinfromhome", row.shellId));
            LoadQuickAccess();
            return success;
        }
    }
    return false;
}
void CMainWnd::LoadFavorites()
{
    m_favorites.clear();
    const std::wstring file = GetFavoritesFilePath();
    FILE* fp = nullptr;
    bool legacy = false;
    if (_wfopen_s(&fp, file.c_str(), L"rb") != 0 || !fp) {
        const std::wstring oldFile = file.substr(0, file.size() - 4) + L"txt";
        if (_wfopen_s(&fp, oldFile.c_str(), L"rb") != 0 || !fp) return;
        legacy = true;
    }
    fseek(fp, 0, SEEK_END);
    const long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0) { fclose(fp); return; }
    std::string bytes(static_cast<size_t>(size), '\0');
    const size_t read = fread(bytes.data(), 1, bytes.size(), fp);
    fclose(fp);
    if (read != bytes.size()) return;
    std::vector<std::wstring> paths;
    if (legacy) {
        if (bytes.size() % sizeof(wchar_t)) return;
        std::wstring content(bytes.size() / sizeof(wchar_t), L'\0');
        memcpy(content.data(), bytes.data(), bytes.size());
        if (!content.empty() && content.front() == 0xFEFF) content.erase(content.begin());
        std::wistringstream lines(content);
        std::wstring line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            if (!line.empty() && line.front() != L'#' && line.front() != L';') paths.push_back(line);
        }
    } else {
        if (bytes.compare(0, 3, "\xef\xbb\xbf") == 0) bytes.erase(0, 3);
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (!count) return;
        std::wstring content(count, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), content.data(), count);
        if (!FavoritesJson::Decode(content, paths)) return;
    }
    for (const auto& line : paths) {
        std::wstring path = NormalizePath(line);
        if (path.empty()) continue;
        DWORD attrs = ::GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
            continue;
        if (IsFavoritePinned(path)) continue;
        FavoriteItem it;
        it.path = path;
        it.displayName = CleanFavoriteLabel(GetShellDisplayName(path));
        if (it.displayName.empty())
            it.displayName = CleanFavoriteLabel(GetLeafName(path));
        if (it.displayName.empty()) it.displayName = path;
        m_favorites.push_back(std::move(it));
    }
    if (legacy) SaveFavorites();
}

void CMainWnd::SaveFavorites() const
{
    const std::wstring file = GetFavoritesFilePath();
    const std::wstring temp = file + L".tmp";
    std::vector<std::wstring> paths;
    for (const auto& item : m_favorites) paths.push_back(item.path);
    const std::string content = FavoritesJson::Encode(paths);
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, temp.c_str(), L"wb") != 0 || !fp) return;
    const bool written = fwrite(content.data(), 1, content.size(), fp) == content.size();
    const bool closed = fclose(fp) == 0;
    if (written && closed)
        ::MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
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
    // Clean at pin time: a folder named "绝密较量Jue mi ji" is stored as "绝密较量" so the
    // chip never shows Chinese glued to its pinyin translation.
    it.displayName = CleanFavoriteLabel(GetShellDisplayName(n));
    if (it.displayName.empty()) it.displayName = CleanFavoriteLabel(GetLeafName(n));
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

void CMainWnd::SetFavoritesBarVisible(bool visible)
{
    m_favoritesBarVisible = visible;
    if (m_pFavoritesBar) {
        m_pFavoritesBar->SetVisible(visible);
        m_pFavoritesBar->NeedParentUpdate();   // hidden bands release their height
    }
    if (m_pFavoritesStrip)
        m_pFavoritesStrip->SetVisible(visible && !m_favorites.empty());
    UpdateStatus(visible ? _T("已显示收藏栏") : _T("已隐藏收藏栏（「查看」菜单可恢复）"));
}

void CMainWnd::RebuildFavoritesBar()
{
    if (!m_pFavoritesStrip) return;
    m_pFavoritesStrip->RemoveAll();
    const bool empty = m_favorites.empty();
    m_pFavoritesStrip->SetVisible(!empty);
    int stripW = 0;

    // Compact chip metrics (96-DPI design -> physical): 25 tall, 4 radius, 8 padding,
    // 8 icon->label gap, 8 between chips, <=168 wide, DT_END_ELLIPSIS beyond that.
    const int iconPx = DpiScale(UiTokens::FavIconPx);
    const int btnH = DpiScale(m_settings.favoritesHeight-4);
    const int padX = DpiScale(UiTokens::FavChipPadX);
    const int iconGap = DpiScale(UiTokens::FavChipIconGap);
    const int chipGap = DpiScale(UiTokens::FavChipGap);
    const int maxChipW = DpiScale(UiTokens::FavChipMaxW);
    const int minChipW = DpiScale(72);
    {
        CDuiString cp;
        cp.Format(_T("%d"), chipGap);
        m_pFavoritesStrip->SetAttribute(_T("childpadding"), cp);
    }
    std::vector<int> widths;
    widths.reserve(m_favorites.size());

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
        btn->SetAttribute(_T("hotbkcolor"), _T("#14000000"));
        btn->SetAttribute(_T("pushedbkcolor"), _T("#22000000"));
        btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        btn->SetAttribute(_T("bordercolor"), _T("#00FFFFFF"));
        btn->SetAttribute(_T("bordersize"), _T("0"));
        btn->SetAttribute(_T("endellipsis"), _T("true"));
        btn->SetBorderRound({ DpiScale(UiTokens::RadiusControl), DpiScale(UiTokens::RadiusControl) });
        {
            CDuiString tp;
            // left room for the icon + gap, then the right pad; valign centres the text
            tp.Format(_T("%d,0,%d,0"), padX + iconPx + iconGap, padX);
            btn->SetAttribute(_T("textpadding"), tp);
        }
        // Size the chip to its actual label rather than estimating from the character count.
        int w = padX + iconPx + iconGap + MeasureTextWidth(fav.displayName) + padX;
        if (w < minChipW) w = minChipW;
        if (w > maxChipW) w = maxChipW;
        widths.push_back(w);
        btn->SetFixedWidth(w);
        btn->SetToolTip(fav.path.c_str());

        std::wstring bmp = GetShellIconBmp(fav.path, true, iconPx);
        if (bmp.empty()) bmp = GetStockIconBmp(SIID_FOLDER, iconPx);
        if (!bmp.empty()) {
            const int y = (btnH - iconPx) / 2; // vertical center with label
            ApplyControlForeIcon(btn, bmp, iconPx, padX, y, false);
        }
        m_pFavoritesStrip->Add(btn);
        stripW += w;
    }
    // Remember the natural widths: the row is not laid out yet during startup, so the
    // shrink-to-fit pass runs from the layout-sync timer (and on every width change).
    m_favChipNatural = widths;
    m_favBarFitW = 0;

    if (!empty)
        m_pFavoritesStrip->SetFixedWidth((std::max)(DpiScale(1), stripW));

    if (CControlUI* hint = m_PaintManager.FindControl(_T("fav_bar_hint"))) {
        hint->SetVisible(empty);
    }
    m_pFavoritesStrip->NeedUpdate();
    if (m_pFavoritesBar) m_pFavoritesBar->NeedUpdate();
    UpdateFavoritesHighlight();
}

// Fit the chip viewport to the space to the right of the star and its gap. Chips retain
// their natural widths and scroll when needed; the star never participates in sizing.
void CMainWnd::RefitFavoritesChips()
{
    if (!m_pFavoritesStrip || !m_pFavoritesBar) return;
    if (m_favChipNatural.empty()) return;
    const RECT bar = m_pFavoritesBar->GetPos();
    const int barW = static_cast<int>(bar.right - bar.left);
    if (barW <= DpiScale(UiTokens::FavStarHitSize + UiTokens::FavStarGap + 16)) return;   // layout not ready

    // Content width, never an equal split: a chip is as wide as its own label needs
    // (72..168 design px). When the row runs out of space the strip scrolls instead of
    // chopping every name down to two glyphs.
    const int availW = (std::max)(0,
        barW - DpiScale(8) * 2 - DpiScale(UiTokens::FavStarHitSize + UiTokens::FavStarGap));
    int total = 0;
    for (int w : m_favChipNatural) total += w;
    total += DpiScale(UiTokens::FavChipGap) * (static_cast<int>(m_favChipNatural.size()) - 1);

    const int maxScroll = (std::max)(0, total - availW);
    if (m_favScrollX > maxScroll) m_favScrollX = maxScroll;
    if (m_favScrollX < 0) m_favScrollX = 0;

    if (barW != m_favBarFitW || m_favScrollX != m_favScrollApplied) {
        m_favBarFitW = barW;
        m_favScrollApplied = m_favScrollX;
        // Negative inset shifts the chips left inside the clipped row - DuiLib does not offer
        // a scrollable horizontal container here, and this keeps every chip at its own width.
        CDuiString inset;
        inset.Format(_T("%d,0,0,0"), -m_favScrollX);
        m_pFavoritesStrip->SetAttribute(_T("inset"), inset);
        m_pFavoritesStrip->SetFixedWidth((std::max)(DpiScale(1), (std::min)(total, availW)));
        m_pFavoritesStrip->NeedUpdate();
        if (m_pFavoritesBar) m_pFavoritesBar->NeedUpdate();
    }
}

// Wheel over the favourites row scrolls the chips horizontally (Explorer scrolls the strip).
void CMainWnd::ScrollFavoritesBy(int dx)
{
    if (m_favChipNatural.empty()) return;
    m_favScrollX += dx;
    RefitFavoritesChips();
    m_favScrollApplied = -1;          // force the re-apply even if the value clamped back
    RefitFavoritesChips();
}

void CMainWnd::EnsureDefaultQuickRows() { } // no app-owned default pins

void CMainWnd::BuildDefaultQuickRows()
{
    LoadQuickAccess();
}

// Project the system snapshot without replacing names, ranking or pin order.
void CMainWnd::RebuildLeftQuickRows()
{
    if (!m_pLeftQuickRows) return;
    m_pLeftQuickRows->RemoveAll();
    const int iconPx = DpiScale(UiTokens::NavIconPx);
    const int rowH = DpiScale(m_settings.NavigationRowHeight());
    for (size_t i = 0; i < m_quickRows.size(); ++i) {
        const QuickRow& row = m_quickRows[i];
        auto* btn = new CButtonUI;
        CDuiString name;
        name.Format(_T("fav_row_%d"), static_cast<int>(i));
        btn->SetName(name);
        // Navigation-pane labels follow the shell's localized name so a pinned
        // "D:\...\Pictures" reads "图片" instead of the raw folder name.
        std::wstring label = row.label;
        btn->SetText(label.c_str());
        btn->SetUserData(row.path.c_str());
        btn->SetFixedHeight(rowH);
        btn->SetAttribute(_T("align"), _T("left"));
        btn->SetAttribute(_T("valign"), _T("vcenter"));
        btn->SetAttribute(_T("endellipsis"), _T("true"));
        btn->SetAttribute(_T("bkcolor"), UiTokens::ColorSurface);
        btn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorNavHover);
        btn->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorNavSelected);
        btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        btn->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
        btn->SetAttribute(_T("bordersize"), _T("0"));
        btn->SetBorderRound({ DpiScale(UiTokens::RadiusControl), DpiScale(UiTokens::RadiusControl) });
        // The row also carries the path as its tooltip so a truncated label stays readable.
        btn->SetToolTip(row.isThisPc ? L"此电脑" : row.path.c_str());

        std::wstring icon;
        if (row.isThisPc)
            icon = GetStockIconBmp(SIID_DESKTOPPC, iconPx);
        else
            icon = GetShellIconBmp(row.path, row.isFolder, iconPx);
        if (icon.empty()) icon = GetStockIconBmp(SIID_FOLDER, iconPx);
        ApplyQuickAccessRow(btn, icon);
        m_pLeftQuickRows->Add(btn);
    }

    // DuiLib *adds* a child's padding to the space it consumes in a vertical layout
    // (CVerticalLayoutUI::SetPos: cyFixed += sz.cy + padding.top + padding.bottom), and
    // UpdateLeftQuickAccessSpacing puts half the slack above and half below. The height the
    // 快速访问 block actually occupies is therefore 2 * fixed - content, so the minimum has to
    // leave just a small inset around the rows instead of one extra row per pin.
    const int contentH = static_cast<int>(m_quickRows.size()) * m_settings.NavigationRowHeight();
    const int minimum = (std::max)(UiTokens::LeftQuickMinH, (std::min)(contentH, 240) + 2 * UiTokens::SpaceXs);
    m_pLeftQuickRows->EnableScrollBar(true, false);
    if (m_pLeftQuick)
        m_pLeftQuick->SetMinHeight(DpiScale(minimum));

    // Size the block to its content the first time we see a given row set (fresh install, a
    // new pin, a removed pin). The old code recomputed an inflated minimum every layout pass,
    // which both looked far too tall and pinned the splitter at that height. After this one
    // fit the user's own drag is kept across launches.
    const int rowCount = static_cast<int>(m_quickRows.size());
    if (m_quickFitRows != rowCount) {
        m_quickFitRows = rowCount;
        ApplyLeftNavSplitterHeight(minimum);   // compact: rows plus a small inset
        SaveLeftNavSplitter();
    } else if (m_leftQuickDesignH < minimum) {
        ApplyLeftNavSplitterHeight(minimum);
    }
    UpdateLeftQuickAccessSpacing();
    UpdateQuickRowHighlight();
    m_pLeftQuickRows->NeedUpdate();
    if (m_pLeftQuick) m_pLeftQuick->NeedUpdate();
}

// Highlights the row that matches the folder being shown (This PC matches the Computer view).
void CMainWnd::UpdateQuickRowHighlight()
{
    if (!m_pLeftQuickRows) return;
    const int n = m_pLeftQuickRows->GetCount();
    bool quickOwnsSelection = false;
    for (int i = 0; i < n && i < static_cast<int>(m_quickRows.size()); ++i) {
        CControlUI* c = m_pLeftQuickRows->GetItemAt(i);
        if (!c) continue;
        const QuickRow& row = m_quickRows[i];
        const bool active = row.isThisPc
            ? IsThisPcPath(m_currentPath)
            : PathEquals(m_currentPath, row.path);
        quickOwnsSelection = quickOwnsSelection || active;
        c->SetAttribute(_T("bkcolor"),
            active ? UiTokens::ColorNavSelected : UiTokens::ColorSurface);
        c->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        c->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
        c->SetAttribute(_T("bordersize"), _T("0"));
        c->Invalidate();
    }
    if (quickOwnsSelection && m_pDirTree) {
        for (int i = 0; i < m_pDirTree->GetCount(); ++i) {
            if (auto* row = static_cast<CTreeNodeUI*>(m_pDirTree->GetItemAt(i)->GetInterface(DUI_CTR_TREENODE)))
                row->Select(false, false);
        }
    }
}

int CMainWnd::HitTestQuickRow(POINT ptClient) const
{
    if (!m_pLeftQuickRows || !m_pLeftQuickRows->IsVisible()) return -1;
    const int n = m_pLeftQuickRows->GetCount();
    for (int i = 0; i < n; ++i) {
        CControlUI* c = m_pLeftQuickRows->GetItemAt(i);
        if (!c || !c->IsVisible()) continue;
        const RECT r = c->GetPos();
        if (ptClient.x >= r.left && ptClient.x < r.right
            && ptClient.y >= r.top && ptClient.y < r.bottom)
            return i;
    }
    return -1;
}

void CMainWnd::ActivateQuickRow(int index)
{
    if (index < 0 || index >= static_cast<int>(m_quickRows.size())) return;
    const QuickRow row = m_quickRows[index];
    if (!row.isFolder) {
        if (!ShellPresentation::OpenDefaultFile(m_hWnd, row.path)) UpdateStatus(L"无法打开最近使用的文件");
        return;
    }
    OpenQuickAccessTab(row.isThisPc ? std::wstring(kThisPcPath) : row.path);
}

void CMainWnd::MoveQuickRow(int, int) { } // retain system order

// Native Shell item menu for a quick-access row: exactly the menu Windows shows for that
// entry (固定到快速访问 / 从快速访问中取消固定 come from Windows itself). 此电脑 gets the
// Computer item's own menu. FastFile adds nothing and handles no command itself.
void CMainWnd::ShowQuickRowContextMenu(int index, POINT ptScreen)
{
    if (index < 0 || index >= static_cast<int>(m_quickRows.size())) return;
    const QuickRow row = m_quickRows[index];
    bool shown = false;
    if (row.isThisPc) {
        shown = ShowThisPcContextMenu(ptScreen, false);
    } else if (!row.shellId.empty()) {
        // The Home (快速访问) identity keeps the menu bound to the system's own entry.
        shown = ShowPidlContextMenu(reinterpret_cast<PCIDLIST_ABSOLUTE>(row.shellId.data()), ptScreen, false);
    } else if (!row.shellPath.empty()) {
        PIDLIST_ABSOLUTE absolute = nullptr;
        if (SUCCEEDED(SHParseDisplayName(row.shellPath.c_str(), nullptr, &absolute, 0, nullptr)) && absolute) {
            shown = ShowPidlContextMenu(absolute, ptScreen, false);
            CoTaskMemFree(absolute);
        }
    }
    if (!shown && !row.path.empty()) shown = ShowShellContextMenu({ row.path }, ptScreen);
    if (!shown) UpdateStatus(_T("无法显示 Windows 右键菜单"));
}

void CMainWnd::OnPinnedFavoriteClick(CControlUI* btn)
{
    if (!btn) return;
    CDuiString ud = btn->GetUserData();
    if (ud.IsEmpty()) return;
    // Both Ctrl+click and plain click reuse a tab that already shows the folder.
    if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        AddTab(ud.GetData(), true);
        return;
    }
    OpenQuickAccessTab(ud.GetData());
}

void CMainWnd::OpenQuickAccessTab(const std::wstring& path)
{
    // Navigating from a shortcut must still sync the tree: the requirement is that the
    // highlighted/expanded node always matches the folder on screen, whatever the entry point.
    AddTab(path, true);
}

// Favourite chips show the target folder's native Windows item menu (此电脑 its own).
void CMainWnd::ShowFavoriteContextMenu(CControlUI* btn, POINT ptScreen)
{
    if (!btn) return;
    CDuiString ud = btn->GetUserData();
    if (ud.IsEmpty()) return;
    const std::wstring path = ud.GetData();
    const bool shown = (path.empty() || IsThisPcPath(path)) ? ShowThisPcContextMenu(ptScreen, false)
        : ShowShellContextMenu({ path }, ptScreen);
    if (!shown) UpdateStatus(_T("无法显示 Windows 右键菜单"));
}
