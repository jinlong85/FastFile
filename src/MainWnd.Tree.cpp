// FastFile - left-hand directory tree
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"
#include "ShellPresentation.h"

// ---- Directory tree ------------------------------------------------------

void CMainWnd::InitDirectoryTree()
{
    if (!m_pDirTree) return;
    m_pDirTree->RemoveAll();

    CTreeNodeUI* root = AddTreeFolderNode(nullptr, kThisPcPath, L"此电脑");
    if (!root) return;

    wchar_t drives[512] = {};
    const DWORD n = ::GetLogicalDriveStringsW(_countof(drives) - 1, drives);
    if (n > 0 && n < _countof(drives)) {
        for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
            std::wstring title = FormatDriveDisplayName(p);
            CTreeNodeUI* driveNode = AddTreeFolderNode(root, p, title);
            if (driveNode)
                AttachPendingChild(driveNode);
        }
    }
    ExpandTreeNode(root, false);
}

void CMainWnd::StyleTreeNode(CTreeNodeUI* node, const std::wstring& title, bool hasChildrenHint)
{
    if (!node) return;
    // DuiLib CTreeNodeUI defaults to FixedWidth(250); left panel is ~220px, so labels
    // were clipped / only ellipsis remained visible. Stretch to list width instead.
    node->SetFixedWidth(0);
    node->SetFixedHeight(DpiScale(m_settings.NavigationRowHeight())); // Phase2: Win11 Explorer tree density
    node->SetVisibleCheckBtn(false);
    node->SetVisibleFolderBtn(hasChildrenHint);

    if (COptionUI* item = node->GetItemButton()) {
        item->SetText(title.c_str());
        item->SetAttribute(_T("align"), _T("left"));
        item->SetAttribute(_T("valign"), _T("vcenter"));
        item->SetAttribute(_T("font"), _T("4"));
        item->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        item->SetAttribute(_T("endellipsis"), _T("true"));
        item->SetMouseEnabled(false);
    }
    node->SetItemText(title.c_str());
    node->SetItemTextColor(UiTokens::ArgbTextPrimary);
    node->SetItemHotTextColor(UiTokens::ArgbTextPrimary);
    node->SetSelItemTextColor(UiTokens::ArgbTextPrimary);
    node->SetSelItemHotTextColor(UiTokens::ArgbTextPrimary);
        // Phase2: per-level indent (DuiLib hardcodes +16; retarget to TreeIndent).
    // GetTreeLevel() is declared but not defined in this DuiLib build — walk parents.
    if (CLabelUI* dotted = node->GetDottedLine()) {
        int level = 0;
        for (CTreeNodeUI* p = node->GetParentNode(); p; p = p->GetParentNode())
            ++level;
        if (level == 0) {
            // "此电脑" is a group header, not a peer of the drives: minimal indent and no
            // selection highlight (see SyncTreeToPath).
            dotted->SetFixedWidth(DpiScale(2));
            dotted->SetVisible(true);
        } else {
            dotted->SetFixedWidth(DpiScale(2 + level * UiTokens::TreeIndent));
            dotted->SetVisible(true);
        }
    }
    // Hover / selected bk via list attrs on tree host; also paint option button
    if (COptionUI* itemBtn = node->GetItemButton()) {
        itemBtn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorNavHover);
        itemBtn->SetAttribute(_T("selectedbkcolor"), UiTokens::ColorNavSelected);
    }

    if (CCheckBoxUI* folder = node->GetFolderButton()) {
        folder->SetFixedWidth(DpiScale(22));
        folder->SetAttribute(_T("align"), _T("center"));
        folder->SetAttribute(_T("valign"), _T("vcenter"));
        folder->SetAttribute(_T("font"), _T("6"));
        folder->SetAttribute(_T("textcolor"), UiTokens::ColorTextMuted);
        folder->SetText(hasChildrenHint ? _T("\xE76C") : _T("")); // Segoe MDL2: ChevronRight
    }
}

void CMainWnd::AttachPendingChild(CTreeNodeUI* parent)
{
    if (!parent) return;
    CTreeNodeUI* pending = new CTreeNodeUI(parent);
    pending->SetFixedWidth(0);
    pending->SetFixedHeight(0);
    pending->SetItemText(_T(""));
    pending->SetUserData(kPendingMarker);
    pending->SetVisibleCheckBtn(false);
    pending->SetVisibleFolderBtn(false);
    parent->AddChildNode(pending);
    pending->SetVisible(false);
    if (CCheckBoxUI* folder = parent->GetFolderButton()) {
        folder->Selected(true); // selected == collapsed in DuiLib TreeView
        folder->SetText(_T("\xE76C")); // ChevronRight
        folder->OnNotify += MakeDelegate(this, &CMainWnd::OnTreeFolderNotify);
    }
}

CTreeNodeUI* CMainWnd::AddTreeFolderNode(CTreeNodeUI* parent, const std::wstring& path, const std::wstring& title)
{
    if (!m_pDirTree) return nullptr;
    CTreeNodeUI* node = new CTreeNodeUI(parent);
    node->SetUserData(path.c_str());
    StyleTreeNode(node, title, true);
    if (parent)
        parent->AddChildNode(node);
    else
        m_pDirTree->Add(node);
    // Re-apply text after insert (some DuiLib paths reset child attrs on Add)
    StyleTreeNode(node, title, true);
    ApplyTreeNodeIcon(node, path);
    return node;
}

bool CMainWnd::IsTreeKeyboardFocus() const
{
    if (m_shellBrowser && m_shellBrowser->OwnsWindow(::GetFocus())) return false;
    for (CControlUI* control = m_PaintManager.GetFocus(); control; control = control->GetParent())
        if (control == m_pDirTree) return true;
    return false;
}

bool CMainWnd::HandleTreeShortcut(WPARAM key)
{
    if (!m_pDirTree) return false;
    CTreeNodeUI* node = FindTreeNodeByPath(nullptr, m_currentPath);
    if (!node) return false;
    const bool expanded = node->GetFolderButton() && !node->GetFolderButton()->IsSelected();
    auto activate = [&](CTreeNodeUI* target) {
        if (!target || target->GetUserData().IsEmpty()) return;
        NavigateTo(target->GetUserData().GetData(), true);
        m_pDirTree->SetFocus();
    };
    if (key == VK_RIGHT || key == VK_ADD) {
        if (!expanded) ExpandTreeNode(node, false);
        else if (key == VK_RIGHT && node->GetCountChild() > 0) activate(node->GetChildNode(0));
        return true;
    }
    if (key == VK_LEFT || key == VK_SUBTRACT) {
        if (expanded) {
            m_pDirTree->SetItemExpand(false, node);
            if (auto* button = node->GetFolderButton()) { button->Selected(true); button->SetText(L"\xE76C"); }
        } else if (key == VK_LEFT) activate(node->GetParentNode());
        return true;
    }
    if (key == VK_UP || key == VK_DOWN || key == VK_HOME || key == VK_END) {
        std::vector<CTreeNodeUI*> visible;
        for (int i = 0; i < m_pDirTree->GetCount(); ++i) {
            CControlUI* control = m_pDirTree->GetItemAt(i);
            if (control && control->IsVisible() && control->GetInterface(DUI_CTR_TREENODE)) {
                auto* item = static_cast<CTreeNodeUI*>(control);
                if (item->GetUserData() != CDuiString(kPendingMarker)) visible.push_back(item);
            }
        }
        auto current = std::find(visible.begin(), visible.end(), node);
        if (current != visible.end()) {
            int index = int(current - visible.begin());
            if (key == VK_HOME) index = 0;
            else if (key == VK_END) index = int(visible.size()) - 1;
            else index += key == VK_DOWN ? 1 : -1;
            if (index >= 0 && index < int(visible.size())) activate(visible[index]);
        }
        return true;
    }
    if (key == VK_RETURN) { activate(node); return true; }
    return false;
}

bool CMainWnd::OnTreeFolderNotify(void* param)
{
    auto* pMsg = static_cast<TNotifyUI*>(param);
    if (!pMsg || pMsg->sType != DUI_MSGTYPE_SELECTCHANGED || !pMsg->pSender)
        return true;
    CControlUI* p = pMsg->pSender;
    while (p && !p->GetInterface(DUI_CTR_TREENODE))
        p = p->GetParent();
    if (!p) return true;

    auto* node = static_cast<CTreeNodeUI*>(p);
    CCheckBoxUI* folder = node->GetFolderButton();
    const bool collapsed = folder && folder->IsSelected();
    if (!collapsed) {
        EnsureTreeChildren(node);
        ExpandTreeNode(node, false);
        if (folder) folder->SetText(_T("\xE70D")); // ChevronDown
    } else {
        if (m_pDirTree)
            m_pDirTree->SetItemExpand(false, node);
        if (folder) folder->SetText(_T("\xE76C")); // ChevronRight
    }
    return true;
}

void CMainWnd::ExpandTreeNode(CTreeNodeUI* node, bool navigate)
{
    if (!node || !m_pDirTree) return;
    EnsureTreeChildren(node);

    // Show direct children but PRESERVE each child's own expand/collapse state.
    // Previously we forced every child folder button to Selected(true)=collapsed,
    // which made SyncTreeToPath / single-click navigate wipe expanded branches.
    // Pending placeholders stay hidden; newly attached children already start collapsed.
    const int n = node->GetCountChild();
    for (int i = 0; i < n; ++i) {
        CTreeNodeUI* c = node->GetChildNode(i);
        if (!c) continue;
        if (c->GetUserData() == CDuiString(kPendingMarker)) {
            c->SetVisible(false);
            continue;
        }
        c->SetVisible(true);
    }
    if (CCheckBoxUI* fb = node->GetFolderButton()) {
        fb->Selected(false);
        fb->SetText(_T("\xE70D")); // ChevronDown
    }
    m_pDirTree->SetItemExpand(true, node);

    // SetItemExpand may reveal pending under already-expanded descendants — re-hide.
    std::function<void(CTreeNodeUI*)> hidePending = [&](CTreeNodeUI* n) {
        if (!n) return;
        for (int i = 0; i < n->GetCountChild(); ++i) {
            CTreeNodeUI* c = n->GetChildNode(i);
            if (!c) continue;
            if (c->GetUserData() == CDuiString(kPendingMarker))
                c->SetVisible(false);
            else
                hidePending(c);
        }
    };
    hidePending(node);

    if (navigate) {
        CDuiString ud = node->GetUserData();
        if (!ud.IsEmpty() && ud != CDuiString(kPendingMarker))
            NavigateTo(ud.GetData(), true);
    }
}

void CMainWnd::EnsureTreeChildren(CTreeNodeUI* node)
{
    if (!node || !m_pDirTree) return;
    CDuiString ud = node->GetUserData();
    if (ud.IsEmpty() || ud == CDuiString(kPendingMarker))
        return;
    if (ud == CDuiString(kThisPcPath))
        return;

    bool hasPending = false;
    const int childCount = node->GetCountChild();
    if (childCount == 1) {
        CTreeNodeUI* c = node->GetChildNode(0);
        if (c && c->GetUserData() == CDuiString(kPendingMarker))
            hasPending = true;
    } else if (childCount > 0) {
        return; // already loaded
    }

    if (hasPending) {
        CTreeNodeUI* pending = node->GetChildNode(0);
        node->RemoveAt(pending);
    }

    const std::wstring path = ud.GetData();
    std::wstring pattern = path;
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
        pattern.push_back(L'\\');
    pattern += L"*";

    WIN32_FIND_DATAW fd = {};
    HANDLE h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
        FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE)
        return;

    std::vector<std::wstring> subdirs;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        if (ShouldHideByAttributes(fd.dwFileAttributes))
            continue;
        subdirs.push_back(fd.cFileName);
        if (static_cast<int>(subdirs.size()) >= 500)
            break;
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);

    std::sort(subdirs.begin(), subdirs.end(), [](const std::wstring& a, const std::wstring& b) {
        return ::_wcsicmp(a.c_str(), b.c_str()) < 0;
    });

    for (const auto& name : subdirs) {
        std::wstring full = JoinPath(path, name);
        std::wstring displayName = GetShellDisplayName(full);
        CTreeNodeUI* child = AddTreeFolderNode(node, full, displayName.empty() ? name : displayName);
        if (!child) continue;
        AttachPendingChild(child);
        child->SetVisible(false); // parent ExpandTreeNode will show
    }
}

void CMainWnd::OnTreeNodeActivate(CTreeNodeUI* node)
{
    if (!node) return;
    CDuiString ud = node->GetUserData();
    if (ud.IsEmpty() || ud == CDuiString(kPendingMarker))
        return;
    // Single click: select + navigate only. Do NOT expand/collapse here —
    // +/- folder button (OnTreeFolderNotify) owns expand/collapse so that
    // clicking a folder never auto-collapses already-expanded branches.
    node->Select(!IsThisPcPath(ud.GetData()) && !IsQuickAccessPinned(ud.GetData()));
    NavigateTo(ud.GetData(), true);
}

CTreeNodeUI* CMainWnd::FindTreeNodeByPath(CTreeNodeUI* parent, const std::wstring& path) const
{
    if (!m_pDirTree) return nullptr;
    if (parent) {
        CDuiString ud = parent->GetUserData();
        if (!ud.IsEmpty() && PathEquals(ud.GetData(), path))
            return parent;
        const int n = parent->GetCountChild();
        for (int i = 0; i < n; ++i) {
            CTreeNodeUI* c = parent->GetChildNode(i);
            if (!c) continue;
            if (c->GetUserData() == CDuiString(kPendingMarker))
                continue;
            CTreeNodeUI* found = FindTreeNodeByPath(c, path);
            if (found) return found;
        }
        return nullptr;
    }
    const int n = m_pDirTree->GetCount();
    for (int i = 0; i < n; ++i) {
        CControlUI* p = m_pDirTree->GetItemAt(i);
        if (!p || !p->GetInterface(DUI_CTR_TREENODE)) continue;
        auto* node = static_cast<CTreeNodeUI*>(p);
        if (node->GetParentNode() != nullptr) continue;
        CTreeNodeUI* found = FindTreeNodeByPath(node, path);
        if (found) return found;
    }
    return nullptr;
}

void CMainWnd::SyncTreeToPath(const std::wstring& path)
{
    if (!m_pDirTree || m_syncingTree) return;
    m_syncingTree = true;
    m_treeRevealPath.clear();
    for (int i = 0; i < m_pDirTree->GetCount(); ++i) {
        if (auto* row = static_cast<CTreeNodeUI*>(m_pDirTree->GetItemAt(i)->GetInterface(DUI_CTR_TREENODE)))
            row->Select(false, false);
    }
    const bool quickOwnsSelection = IsThisPcPath(path) || IsQuickAccessPinned(path);

    if (IsThisPcPath(path)) {
        CTreeNodeUI* root = FindTreeNodeByPath(nullptr, kThisPcPath);
        if (root) {
            ExpandTreeNode(root, false);
            // The tree root is a group header: the quick-access row owns the "This PC"
            // selection, so the root never paints a selected background (double highlight).
            root->Select(false);
        }
        // Entering This PC: show the drive list from the top.
        if (m_pDirTree) {
            SIZE scroll = m_pDirTree->GetScrollPos();
            if (scroll.cy != 0) {
                scroll.cy = 0;
                m_pDirTree->SetScrollPos(scroll);
            }
        }
        m_syncingTree = false;
        return;
    }

    std::wstring norm = NormalizePath(path);
    if (norm.empty()) { m_syncingTree = false; return; }

    const auto chain = ShellPresentation::AncestorPaths(norm);

    CTreeNodeUI* node = FindTreeNodeByPath(nullptr, kThisPcPath);
    if (node)
        ExpandTreeNode(node, false);

    CTreeNodeUI* last = node;
    for (const auto& prefix : chain) {
        if (last) ExpandTreeNode(last, false);
        CTreeNodeUI* found = FindTreeNodeByPath(last, prefix);
        if (!found) found = FindTreeNodeByPath(nullptr, prefix);
        if (!found && last) {
            // Explicit navigation must also reveal a hidden folder or one beyond the
            // lazy enumeration cap. Its identity and display name still come from Shell.
            DWORD attrs = GetFileAttributesW(prefix.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring title = GetShellDisplayName(prefix);
                if (title.empty()) title = GetLeafName(prefix);
                found = AddTreeFolderNode(last, prefix, title);
                if (found) { AttachPendingChild(found); found->SetVisible(true); }
            }
        }
        if (!found) break;
        last = found;
    }
    if (last && PathEquals(last->GetUserData().GetData(), norm)) {
        last->Select(!quickOwnsSelection, false);
        m_treeRevealPath = norm;
        m_pDirTree->NeedUpdate();
    }

    m_syncingTree = false;
}

// Run after layout, not while ExpandTreeNode still has stale/zero child rectangles.
void CMainWnd::RevealSyncedTreeNode()
{
    if (!m_pDirTree || m_treeRevealPath.empty()) return;
    CTreeNodeUI* target = FindTreeNodeByPath(nullptr, m_treeRevealPath);
    if (!target || !target->IsVisible()) return;
    CContainerUI* viewport = m_pDirTree->GetList();
    if (!viewport) return;
    RECT view = viewport->GetPos();
    RECT item = target->GetPos();
    if (item.bottom <= item.top || view.bottom <= view.top) return;
    int dy = item.top < view.top ? item.top - view.top :
        item.bottom > view.bottom ? item.bottom - view.bottom : 0;
    if (dy) {
        SIZE scroll = viewport->GetScrollPos();
        scroll.cy += dy;
        viewport->SetScrollPos(scroll);
    }
    m_treeRevealPath.clear();
}
