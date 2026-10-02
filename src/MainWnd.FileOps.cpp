// FastFile - file operations and the background copy engine
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"
#include "FastFileCore.h"

namespace {

constexpr int kInlineRenameEditId = 0x6F32;

static LRESULT CALLBACK InlineRenameEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto oldProc = reinterpret_cast<WNDPROC>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_KEYDOWN && (wParam == VK_RETURN || wParam == VK_ESCAPE)) {
        HWND owner = ::GetParent(hwnd);
        ::PostMessageW(owner, wParam == VK_RETURN
            ? WM_APP + 0x451 : WM_APP + 0x452, 0, 0);
        return 0;
    }
    if (msg == WM_NCDESTROY)
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    return oldProc ? ::CallWindowProcW(oldProc, hwnd, msg, wParam, lParam)
                   : ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- tiny text prompt dialog (in-memory DLGTEMPLATE) ----
struct PromptState {
    const wchar_t* title;
    const wchar_t* prompt;
    wchar_t* buf;
    int bufChars;
};

static INT_PTR CALLBACK PromptDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        auto* st = reinterpret_cast<PromptState*>(lParam);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, lParam);
        SetWindowTextW(hDlg, st->title ? st->title : L"");
        SetDlgItemTextW(hDlg, 1001, st->prompt ? st->prompt : L"");
        SetDlgItemTextW(hDlg, 1002, st->buf);
        SendDlgItemMessageW(hDlg, 1002, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(hDlg, 1002));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            auto* st = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));
            GetDlgItemTextW(hDlg, 1002, st->buf, st->bufChars);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

} // namespace

// ---- Clipboard / multi-select / file ops ---------------------------------

void CMainWnd::CollectSelectedItems(std::vector<ClipboardItem>& out) const
{
    out.clear();
    if (IsTreeKeyboardFocus()) {
        if (!IsThisPcPath(m_currentPath)) out.push_back({m_currentPath, true});
        return;
    }

    if (m_shellBrowser && m_shellBrowser->IsCreated() && m_shellBrowser->IsVisible()) {
        std::vector<std::pair<std::wstring, bool>> selected;
        if (m_shellBrowser->GetSelection(selected)) {
            out.reserve(selected.size());
            for (auto& item : selected) {
                ClipboardItem value;
                value.path = std::move(item.first);
                value.isDir = item.second;
                out.push_back(std::move(value));
            }
        }
        return; // never fall back to stale self-drawn selections while Shell owns the view
    }

    if (IsTileViewMode() && m_pIconTiles) {
        const int n = m_pIconTiles->GetCount();
        for (int i = 0; i < n; ++i) {
            CControlUI* p = m_pIconTiles->GetItemAt(i);
            if (!p) continue;
            if ((p->GetTag() & 0x100) == 0) continue;
            CDuiString ud = p->GetUserData();
            if (ud.IsEmpty()) continue;
            ClipboardItem item;
            item.path = ud.GetData();
            item.isDir = (p->GetTag() & 1) != 0;
            out.push_back(std::move(item));
        }
        if (!out.empty())
            return;
        // fallback: focused tile
        CControlUI* focus = m_PaintManager.GetFocus();
        if (focus && focus->GetParent() == m_pIconTiles && !focus->GetUserData().IsEmpty()) {
            ClipboardItem item;
            item.path = focus->GetUserData().GetData();
            item.isDir = (focus->GetTag() & 1) != 0;
            out.push_back(std::move(item));
        }
        return;
    }

    // Details view: selection lives per entry (m_detailsSel), not on the recycled list rows -
    // a row only represents an entry while it is inside the virtual window.
    const size_t total = (std::min)(m_detailsEntries.size(), m_detailsSel.size());
    for (size_t i = 0; i < total; ++i) {
        if (!m_detailsSel[i]) continue;
        ClipboardItem item;
        item.path = m_detailsEntries[i].fullPath;
        item.isDir = m_detailsEntries[i].isDir;
        out.push_back(std::move(item));
    }
}

bool CMainWnd::PublishFileClipboard(const std::vector<ClipboardItem>& items, bool cut)
{
    std::wstring paths;
    for (const auto& item : items) { paths += item.path; paths.push_back(L'\0'); }
    paths.push_back(L'\0');
    HGLOBAL drop = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
        sizeof(DROPFILES) + paths.size() * sizeof(wchar_t));
    HGLOBAL effect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!drop || !effect) {
        if (drop) GlobalFree(drop);
        if (effect) GlobalFree(effect);
        return false;
    }
    auto* header = static_cast<DROPFILES*>(GlobalLock(drop));
    auto* preferred = static_cast<DWORD*>(GlobalLock(effect));
    if (!header || !preferred) {
        if (header) GlobalUnlock(drop);
        if (preferred) GlobalUnlock(effect);
        GlobalFree(drop); GlobalFree(effect); return false;
    }
    header->pFiles = sizeof(DROPFILES); header->fWide = TRUE;
    memcpy(reinterpret_cast<BYTE*>(header) + sizeof(DROPFILES), paths.data(), paths.size() * sizeof(wchar_t));
    *preferred = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
    GlobalUnlock(drop); GlobalUnlock(effect);
    if (!OpenClipboard(m_hWnd)) { GlobalFree(drop); GlobalFree(effect); return false; }
    bool ok = EmptyClipboard() && SetClipboardData(CF_HDROP, drop);
    if (ok) drop = nullptr; // Windows owns each successfully published handle.
    const UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    if (ok && SetClipboardData(format, effect)) effect = nullptr;
    else ok = false;
    CloseClipboard();
    if (drop) GlobalFree(drop);
    if (effect) GlobalFree(effect);
    return ok;
}

bool CMainWnd::ReadFileClipboard(std::vector<ClipboardItem>& items, bool& cut) const
{
    items.clear(); cut = false;
    if (!OpenClipboard(m_hWnd)) return false;
    HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    const UINT count = drop ? DragQueryFileW(drop, 0xffffffff, nullptr, 0) : 0;
    for (UINT i = 0; i < count; ++i) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(length + 1, L'\0');
        if (!length || !DragQueryFileW(drop, i, path.data(), length + 1)) continue;
        path.resize(length);
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
            items.push_back({std::move(path), (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
    }
    HGLOBAL effect = GetClipboardData(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));
    if (effect && GlobalSize(effect) >= sizeof(DWORD)) {
        auto* preferred = static_cast<const DWORD*>(GlobalLock(effect));
        if (preferred) { cut = (*preferred & DROPEFFECT_MOVE) != 0; GlobalUnlock(effect); }
    }
    CloseClipboard();
    return !items.empty();
}

void CMainWnd::OnCopyClicked()
{
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.empty()) {
        UpdateStatus(_T("请先选中要复制的文件或文件夹（支持 Ctrl/Shift 多选）"));
        return;
    }
    if (!PublishFileClipboard(items, false)) {
        UpdateStatus(_T("无法访问系统剪贴板，请稍后重试")); return;
    }
    m_clipboard = std::move(items);
    m_clipboardIsCut = false;
    CDuiString tip;
    tip.Format(_T("已复制 %d 项 — 切换到目标面板后点「粘贴」（可跨左右面板）"),
        static_cast<int>(m_clipboard.size()));
    UpdateStatus(tip.GetData());
    ApplyCopyUiState();
}

void CMainWnd::OnCopyPaths()
{
    std::vector<ClipboardItem> items; CollectSelectedItems(items);
    if (items.empty()) { UpdateStatus(_T("请先选中要复制路径的项目")); return; }
    std::wstring text;
    for (const auto& item : items) {
        if (!text.empty()) text += L"\r\n";
        text += L"\"" + item.path + L"\"";
    }
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (!data) return;
    void* memory = GlobalLock(data);
    if (!memory) { GlobalFree(data); return; }
    memcpy(memory, text.c_str(), (text.size() + 1) * sizeof(wchar_t)); GlobalUnlock(data);
    if (!OpenClipboard(m_hWnd)) { GlobalFree(data); return; }
    const bool ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data);
    CloseClipboard(); if (!ok) GlobalFree(data);
    UpdateStatus(ok ? _T("已复制完整路径") : _T("无法访问系统剪贴板"));
}

void CMainWnd::OnPasteClicked()
{
    if (m_shellHistoryPending) { UpdateStatus(_T("正在完成 Windows 文件操作，请稍候")); return; }
    if (m_copyRunning.load()) {
        UpdateStatus(_T("已有复制任务在进行，请等待或取消"));
        return;
    }
    if (!ReadFileClipboard(m_clipboard, m_clipboardIsCut)) {
        UpdateStatus(_T("剪贴板为空 — 先选中项目并点「复制」"));
        return;
    }
    if (m_currentPath.empty()) {
        UpdateStatus(_T("当前目录无效"));
        return;
    }

    for (const auto& it : m_clipboard) {
        if (!it.isDir) continue;
        std::wstring src = NormalizePath(it.path);
        std::wstring dst = NormalizePath(m_currentPath);
        if (src.empty() || dst.empty()) continue;
        if (_wcsicmp(src.c_str(), dst.c_str()) == 0) {
            UpdateStatus(_T("不能粘贴到自身"));
            return;
        }
        std::wstring prefix = src;
        if (prefix.back() != L'\\') prefix.push_back(L'\\');
        if (dst.size() >= prefix.size()
            && _wcsnicmp(dst.c_str(), prefix.c_str(), static_cast<int>(prefix.size())) == 0) {
            UpdateStatus(_T("不能粘贴到源文件夹内部"));
            return;
        }
    }

    if (m_clipboardIsCut) {
        // Cut + paste runs through the background job engine so a move gets the same
        // in-app progress readout and cancel button as a copy (no Shell dialog).
        std::vector<ClipboardItem> moving = m_clipboard;
        m_clipboard.clear();
        m_clipboardIsCut = false;
        ApplyCopyUiState();
        m_lastCopyDest = m_currentPath;
        StartCopyJob(std::move(moving), m_currentPath, /*move*/ true);
        return;
    }

    m_lastCopyDest = m_currentPath;
    StartCopyJob(m_clipboard, m_currentPath);
}

void CMainWnd::OnCancelCopyClicked()
{
    if (!m_copyRunning.load()) return;
    m_copyCancel.store(true);
    UpdateStatus(m_jobIsMove ? _T("正在取消移动…") : _T("正在取消复制…"));
}

void CMainWnd::OnDeleteClicked(bool permanent)
{
    if (m_shellHistoryPending) { UpdateStatus(_T("正在完成 Windows 文件操作，请稍候")); return; }
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.empty()) {
        UpdateStatus(_T("请先选中要删除的项目"));
        return;
    }

    if (permanent) {
        CDuiString msg;
        if (items.size() == 1) {
            msg.Format(_T("确定将「%s」永久删除吗？\n\n该项目不会进入回收站，无法通过资源管理器还原。"),
                GetLeafName(items[0].path).c_str());
        } else {
            msg.Format(_T("确定将选中的 %d 项永久删除吗？\n\n这些项目不会进入回收站。"),
                static_cast<int>(items.size()));
        }
        if (::MessageBoxW(m_hWnd, msg.GetData(), L"FastFile - 确认删除",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            UpdateStatus(_T("已取消删除"));
            return;
        }
    }

    std::vector<std::wstring> completed;
    const bool allDeleted = DeleteItems(items, permanent, &completed);
    if (!completed.empty()) {
        if (!permanent) {
            UndoRecord record; record.kind = UndoRecord::Kind::ShellDelete;
            for (const auto& path : completed) record.moved.emplace_back(path, path);
            ClearRedoHistory(); m_historyStarted = true;
            m_undoStack.push_back(std::move(record));
        } else ClearRedoHistory();
        if (allDeleted) {
            CDuiString tip;
            tip.Format(permanent ? _T("已永久删除 %d 项") : _T("已删除到回收站 %d 项"),
                static_cast<int>(completed.size()));
            UpdateStatus(tip.GetData());
        }
    }
    // Cancellation can follow a partially completed batch: always refresh.
    RefreshAfterHistory();
}

DWORD CMainWnd::DeleteOperationFlags(bool permanent)
{
    // Keep Shell error/elevation UI available. Only routine confirmation is
    // suppressed; WANTNUKEWARNING still warns when recycling is impossible.
    return FOF_NOCONFIRMATION | FOFX_SHOWELEVATIONPROMPT |
        (permanent ? 0 : FOF_ALLOWUNDO | FOFX_ADDUNDORECORD |
            FOFX_RECYCLEONDELETE | FOF_WANTNUKEWARNING);
}

bool CMainWnd::DeleteItems(const std::vector<ClipboardItem>& items, bool permanent,
    std::vector<std::wstring>* completed)
{
    if (completed) completed->clear();
    if (items.empty()) return false;
    IFileOperation* operation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&operation));
    if (SUCCEEDED(hr)) hr = operation->SetOwnerWindow(m_hWnd);
    if (SUCCEEDED(hr)) hr = operation->SetOperationFlags(DeleteOperationFlags(permanent));
    std::vector<std::wstring> existing;
    for (const auto& item : items) {
        if (FAILED(hr)) break;
        IShellItem* source = nullptr;
        hr = SHCreateItemFromParsingName(item.path.c_str(), nullptr, IID_PPV_ARGS(&source));
        if (SUCCEEDED(hr)) {
            if (GetFileAttributesW(item.path.c_str()) != INVALID_FILE_ATTRIBUTES)
                existing.push_back(item.path);
            hr = operation->DeleteItem(source, nullptr);
            source->Release();
        }
    }
    bool performed = false;
    BOOL aborted = FALSE;
    if (SUCCEEDED(hr)) {
        performed = true;
        hr = operation->PerformOperations();
        const HRESULT result = operation->GetAnyOperationsAborted(&aborted);
        if (SUCCEEDED(hr) && FAILED(result)) hr = result;
    }
    if (operation) operation->Release();
    // Do not include missing inputs, skipped items or access-denied paths in
    // history. A canceled batch can still have successfully recycled items.
    size_t count = 0;
    if (performed) for (const auto& path : existing) {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                ++count;
                if (completed) completed->push_back(path);
            }
        }
    }
    if (FAILED(hr) || aborted || count != items.size()) {
        CDuiString tip;
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || hr == E_ABORT || aborted)
            tip.Format(_T("删除已取消或部分项目已跳过，已处理 %d / %d 项"), int(count), int(items.size()));
        else {
            wchar_t* message = nullptr;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, DWORD(hr), 0,
                reinterpret_cast<wchar_t*>(&message), 0, nullptr);
            tip.Format(_T("删除未全部完成，已处理 %d / %d 项：%s (0x%08X)"),
                int(count), int(items.size()), message ? message : L"Windows 未完成该操作", DWORD(hr));
            if (message) LocalFree(message);
        }
        UpdateStatus(tip.GetData());
        return false;
    }
    return true;
}

void CMainWnd::OnRenameClicked()
{
    if (m_shellHistoryPending) { UpdateStatus(_T("正在完成 Windows 文件操作，请稍候")); return; }
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.empty()) {
        UpdateStatus(_T("请先选中要重命名的一项"));
        return;
    }
    if (items.size() != 1) {
        UpdateStatus(_T("重命名一次只能选中一项"));
        return;
    }

    if (IsTreeKeyboardFocus()) {
        std::wstring name;
        if (PromptText(m_hWnd, L"重命名文件夹", L"文件夹名称：", GetLeafName(items[0].path).c_str(), name) &&
            RenameItem(items[0], name)) NavigateToNow(JoinPath(ParentPath(items[0].path), name), true);
        return;
    }

    if (m_shellBrowser && m_shellBrowser->IsCreated() && m_shellBrowser->IsVisible()) {
        m_pendingShellRename = items[0].path;
        if (!m_shellBrowser->BeginRename()) {
            m_pendingShellRename.clear();
            UpdateStatus(_T("无法启动 Windows 原生重命名"));
        }
        return;
    }

    if (!BeginInlineRename(items[0]))
        UpdateStatus(_T("无法在当前视图中编辑名称"));
}

bool CMainWnd::GetInlineRenameRect(const ClipboardItem& item, RECT& rect)
{
    if (IsTileViewMode() && m_pIconTiles) {
        CControlUI* tile = nullptr;
        for (int i = 0; i < m_pIconTiles->GetCount(); ++i) {
            CControlUI* candidate = m_pIconTiles->GetItemAt(i);
            if (candidate && candidate->GetUserData() == item.path.c_str()) {
                tile = candidate;
                break;
            }
        }
        if (!tile)
            return false;
        const RECT tileRect = tile->GetPos();
        rect = tileRect;
        if (m_viewMode == ViewMode::List) {
            rect.left += DpiScale(24);
            rect.top += (tileRect.bottom - tileRect.top - DpiScale(22)) / 2;
            rect.bottom = rect.top + DpiScale(22);
        } else if (m_viewMode == ViewMode::Tiles) {
            rect.left += DpiScale(56);
            rect.top += (tileRect.bottom - tileRect.top - DpiScale(17)) / 2;
            rect.bottom = rect.top + DpiScale(22);
        } else {
            rect.left += DpiScale(4);
            rect.right -= DpiScale(4);
            rect.top = tileRect.bottom - DpiScale(26);
            rect.bottom = tileRect.bottom - DpiScale(4);
        }
        return rect.right - rect.left >= DpiScale(40);
    }

    if (m_pFileList && m_pFileList->IsVisible()) {
        int entry = -1;
        for (size_t i = 0; i < m_detailsEntries.size(); ++i) {
            if (m_detailsEntries[i].fullPath == item.path) {
                entry = static_cast<int>(i);
                break;
            }
        }
        if (entry < 0)
            return false;
        DetailsEnsureEntryVisible(entry);
        for (int i = 0; i < m_detailsPoolRows; ++i) {
            const int boundEntry = m_detailsFirst + i;
            if (boundEntry != entry)
                continue;
            auto* row = static_cast<CListContainerElementUI*>(m_pFileList->GetItemAt(i + 1));
            auto* nameCol = row ? static_cast<CHorizontalLayoutUI*>(row->GetItemAt(0)) : nullptr;
            if (!nameCol)
                return false;
            rect = nameCol->GetPos();
            rect.left += DpiScale(UiTokens::DetailsIconPadL + UiTokens::DetailsIconPx
                + UiTokens::DetailsIconTextGap);
            rect.top += DpiScale(2);
            rect.bottom -= DpiScale(2);
            return rect.right - rect.left >= DpiScale(40);
        }
    }
    return false;
}

bool CMainWnd::BeginInlineRename(const ClipboardItem& item)
{
    if (m_renameEdit)
        CancelInlineRename();
    RECT rect = {};
    if (!GetInlineRenameRect(item, rect))
        return false;

    const std::wstring name = GetLeafName(item.path);
    HWND edit = ::CreateWindowExW(0, L"EDIT", name.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        m_hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInlineRenameEditId)),
        ::GetModuleHandleW(nullptr), nullptr);
    if (!edit)
        return false;

    auto oldProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(edit, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(InlineRenameEditProc)));
    ::SetWindowLongPtrW(edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(oldProc));
    HFONT font = m_PaintManager.GetFont(0);
    if (font)
        ::SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    m_renameEdit = edit;
    m_renameOriginalPath = item.path;
    m_renameIsDirectory = item.isDir;
    ::SetFocus(edit);
    const size_t selectEnd = FastFileCore::RenameSelectionEnd(name, item.isDir);
    ::SendMessageW(edit, EM_SETSEL, 0, static_cast<LPARAM>(selectEnd));
    return true;
}

void CMainWnd::CommitInlineRename()
{
    if (!m_renameEdit || m_finishingInlineRename)
        return;
    m_finishingInlineRename = true;
    wchar_t text[MAX_PATH] = {};
    ::GetWindowTextW(m_renameEdit, text, MAX_PATH);
    HWND edit = m_renameEdit;
    m_renameEdit = nullptr;
    ::DestroyWindow(edit);
    const std::wstring oldPath = std::move(m_renameOriginalPath);
    m_renameOriginalPath.clear();
    const bool wasDirectory = m_renameIsDirectory;
    m_renameIsDirectory = false;
    m_finishingInlineRename = false;

    const std::wstring entered(text);
    if (entered.empty() || !FastFileCore::IsValidLeafName(entered)) {
        UpdateStatus(_T("名称无效"));
        return;
    }
    const std::wstring oldName = GetLeafName(oldPath);
    if (_wcsicmp(entered.c_str(), oldName.c_str()) == 0) {
        UpdateStatus(_T("名称未更改"));
        return;
    }
    ClipboardItem item;
    item.path = oldPath;
    item.isDir = wasDirectory;
    if (RenameItem(item, entered)) {
        UpdateStatus(_T("重命名完成（Ctrl+Z 可撤销）"));
        RefreshListing();
    }
}

void CMainWnd::CancelInlineRename()
{
    if (!m_renameEdit)
        return;
    m_finishingInlineRename = true;
    HWND edit = m_renameEdit;
    m_renameEdit = nullptr;
    m_renameOriginalPath.clear();
    m_renameIsDirectory = false;
    ::DestroyWindow(edit);
    m_finishingInlineRename = false;
    UpdateStatus(_T("已取消重命名"));
}

bool CMainWnd::RenameItem(const ClipboardItem& item, const std::wstring& newName)
{
    std::wstring parent = ParentPath(item.path);
    if (parent.empty()) parent = item.path; // shouldn't happen
    std::wstring dest = JoinPath(parent, newName);
    if (::GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) {
        UpdateStatus(_T("目标名称已存在"));
        return false;
    }
    if (!::MoveFileW(item.path.c_str(), dest.c_str())) {
        CDuiString tip;
        tip.Format(_T("重命名失败 (%lu)"), ::GetLastError());
        UpdateStatus(tip.GetData());
        return false;
    }
    PushUndo(UndoRecord::Kind::Rename, item.path, dest);
    return true;
}

void CMainWnd::OnNewFolderClicked()
{
    if (m_shellHistoryPending) { UpdateStatus(_T("正在完成 Windows 文件操作，请稍候")); return; }
    CreateNewFolder();
}

void CMainWnd::OnCutClicked()
{
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.empty()) {
        UpdateStatus(_T("请先选择要剪切的文件或文件夹"));
        return;
    }
    if (!PublishFileClipboard(items, true)) {
        UpdateStatus(_T("无法访问系统剪贴板，请稍后重试")); return;
    }
    m_clipboard = std::move(items);
    m_clipboardIsCut = true;
    CDuiString tip;
    tip.Format(_T("已剪切 %d 项 — 切换到目标目录后点「粘贴」即可移动"),
        static_cast<int>(m_clipboard.size()));
    UpdateStatus(tip.GetData());
    ApplyCopyUiState();
}

void CMainWnd::OnShareClicked()
{
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.empty()) {
        UpdateStatus(_T("请先选择要共享的项目"));
        return;
    }
    // Prefer Shell "share" verb on first selected item (Win10/11 modern share when registered)
    const std::wstring& path = items.front().path;
    HINSTANCE hi = ::ShellExecuteW(m_hWnd, L"share", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(hi) > 32) {
        UpdateStatus(_T("已打开共享"));
        return;
    }
    // Fallback: copy path list to clipboard as text so user can paste into chat/email
    std::wstring text;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) text += L"\r\n";
        text += items[i].path;
    }
    if (::OpenClipboard(m_hWnd)) {
        ::EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            void* p = ::GlobalLock(h);
            if (p) {
                memcpy(p, text.c_str(), bytes);
                ::GlobalUnlock(h);
                ::SetClipboardData(CF_UNICODETEXT, h);
            }
        }
        ::CloseClipboard();
        UpdateStatus(_T("系统共享不可用 — 已复制路径到剪贴板"));
    } else {
        UpdateStatus(_T("共享不可用，请使用右键菜单"));
    }
}

bool CMainWnd::CreateNewFolder()
{
    if (m_currentPath.empty()) {
        UpdateStatus(_T("当前目录无效"));
        return false;
    }
    std::wstring name = L"新建文件夹";
    std::wstring entered;
    if (!PromptText(m_hWnd, L"新建文件夹", L"文件夹名称：", name.c_str(), entered)) {
        UpdateStatus(_T("已取消新建"));
        return false;
    }
    if (!FastFileCore::IsValidLeafName(entered)) {
        UpdateStatus(_T("名称无效"));
        return false;
    }
    std::wstring dest = UniqueDestPath(JoinPath(m_currentPath, entered));
    if (!::CreateDirectoryW(dest.c_str(), nullptr)) {
        CDuiString tip;
        tip.Format(_T("创建失败 (%lu)"), ::GetLastError());
        UpdateStatus(tip.GetData());
        return false;
    }
    CDuiString tip;
    tip.Format(_T("已创建: %s（Ctrl+Z 可撤销）"), GetLeafName(dest).c_str());
    UpdateStatus(tip.GetData());
    PushUndo(UndoRecord::Kind::CreateFolder, std::wstring(), dest);
    RefreshListing();
    return true;
}

bool CMainWnd::PromptText(HWND owner, const wchar_t* title, const wchar_t* prompt,
    const wchar_t* initial, std::wstring& out)
{
    // In-memory dialog template (4 controls)
    alignas(4) BYTE raw[2048];
    memset(raw, 0, sizeof(raw));
    auto* pdt = reinterpret_cast<DLGTEMPLATE*>(raw);
    pdt->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    pdt->cdit = 4;
    pdt->cx = 220;
    pdt->cy = 82;

    BYTE* p = reinterpret_cast<BYTE*>(pdt + 1);
    auto write_word = [&](WORD v) {
        *reinterpret_cast<WORD*>(p) = v; p += 2;
    };
    auto write_wsz = [&](const wchar_t* s) {
        if (!s) s = L"";
        while (*s) { write_word(static_cast<WORD>(*s++)); }
        write_word(0);
    };
    auto align4 = [&]() {
        ULONG_PTR a = reinterpret_cast<ULONG_PTR>(p);
        while (a & 3) { *p++ = 0; ++a; }
    };

    write_word(0); // menu
    write_word(0); // class
    write_wsz(title ? title : L"");

    auto add_item = [&](DWORD style, short x, short y, short cx, short cy,
                        WORD id, WORD classAtom, const wchar_t* txt) {
        align4();
        auto* it = reinterpret_cast<DLGITEMTEMPLATE*>(p);
        it->style = style | WS_CHILD | WS_VISIBLE;
        it->dwExtendedStyle = 0;
        it->x = x; it->y = y; it->cx = cx; it->cy = cy;
        it->id = id;
        p = reinterpret_cast<BYTE*>(it + 1);
        write_word(0xFFFF);
        write_word(classAtom);
        write_wsz(txt);
        write_word(0); // creation data
    };

    add_item(SS_LEFT, 8, 8, 200, 12, 1001, 0x0082, prompt ? prompt : L"");
    add_item(ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 8, 24, 204, 14, 1002, 0x0081, L"");
    add_item(BS_DEFPUSHBUTTON | WS_TABSTOP, 70, 50, 50, 16, IDOK, 0x0080, L"确定");
    add_item(BS_PUSHBUTTON | WS_TABSTOP, 130, 50, 50, 16, IDCANCEL, 0x0080, L"取消");

    wchar_t initialBuf[MAX_PATH] = {};
    if (initial)
        wcsncpy_s(initialBuf, initial, _TRUNCATE);

    PromptState st{ title, prompt, initialBuf, MAX_PATH };
    INT_PTR r = ::DialogBoxIndirectParamW(
        ::GetModuleHandleW(nullptr), pdt, owner, PromptDlgProc,
        reinterpret_cast<LPARAM>(&st));
    if (r != IDOK) return false;
    out.assign(initialBuf);
    while (!out.empty() && (out.back() == L' ' || out.back() == L'\t')) out.pop_back();
    while (!out.empty() && (out.front() == L' ' || out.front() == L'\t')) out.erase(out.begin());
    return !out.empty();
}

// ---- Background copy -----------------------------------------------------

void CMainWnd::ApplyCopyUiState()
{
    const bool running = m_copyRunning.load();
    if (m_pBtnCancelCopy) {
        m_pBtnCancelCopy->SetVisible(running);
        m_pBtnCancelCopy->SetEnabled(running);
    }
    // The command bar buttons (cut/copy/paste/rename/share/delete) follow the selection and
    // clipboard state instead of being hard-wired here.
    UpdateCommandBarState();
}

void CMainWnd::StopCopyThread(bool wait)
{
    m_copyCancel.store(true);
    if (m_copyThread.joinable()) {
        if (wait)
            m_copyThread.join();
        else
            m_copyThread.detach();
    }
    m_copyRunning.store(false);
}

void CMainWnd::StartCopyJob(std::vector<ClipboardItem> items, std::wstring destDir, bool move)
{
    StopCopyThread(true);

    m_copyCancel.store(false);
    m_copyRunning.store(true);
    m_jobIsMove = move;
    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress = CopyProgressSnapshot{};
        m_progress.state = CopyProgressSnapshot::State::Running;
        m_progress.filesTotal = static_cast<int>(items.size());
        m_moveUndoPairs.clear();
    }
    m_workerBytesBase = 0;
    m_workerFileSize = 0;

    ApplyCopyUiState();
    UpdateStatus(move ? _T("正在准备移动…（可继续浏览目录）")
                      : _T("正在准备复制…（可继续浏览目录）"));

    const bool moveJob = move;
    m_copyThread = std::thread([this, items = std::move(items), destDir = std::move(destDir), moveJob]() mutable {
        CopyWorkerMain(this, std::move(items), std::move(destDir), moveJob);
    });
}

void CMainWnd::OnCopyProgressMessage()
{
    CopyProgressSnapshot snap;
    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        snap = m_progress;
    }

    const wchar_t* verb = m_jobIsMove ? L"移动" : L"复制";
    wchar_t buf[512] = {};
    const wchar_t* name = snap.current[0] ? snap.current : L"…";
    if (snap.bytesTotal > 0) {
        const double pct = (100.0 * static_cast<double>(snap.bytesDone))
            / static_cast<double>(snap.bytesTotal);
        swprintf_s(buf,
            L"%s中 %d/%d  ·  %s / %s (%.0f%%)  ·  %s  ·  可继续浏览",
            verb, snap.filesDone, snap.filesTotal,
            FormatFileSize(snap.bytesDone).c_str(),
            FormatFileSize(snap.bytesTotal).c_str(),
            pct, name);
    } else {
        swprintf_s(buf, L"%s中 %d/%d  ·  %s  ·  可继续浏览",
            verb, snap.filesDone, snap.filesTotal, name);
    }
    UpdateStatus(buf);
}

void CMainWnd::OnCopyFinishedMessage(WPARAM resultCode)
{
    if (m_copyThread.joinable())
        m_copyThread.join();
    m_copyRunning.store(false);
    ApplyCopyUiState();

    CopyProgressSnapshot snap;
    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        snap = m_progress;
    }

    const bool wasMove = m_jobIsMove;
    const wchar_t* verb = wasMove ? _T("移动") : _T("复制");

    {
        // One undo step for the whole move, so a single Ctrl+Z puts every item back.
        std::vector<std::pair<std::wstring, std::wstring>> pairs;
        {
            std::lock_guard<std::mutex> lock(m_progressMutex);
            pairs.swap(m_moveUndoPairs);
        }
        if (!pairs.empty()) {
            UndoRecord record;
            record.kind = wasMove ? UndoRecord::Kind::Move : UndoRecord::Kind::Copy;
            record.moved = std::move(pairs);
            ClearRedoHistory(); m_historyStarted = true;
            m_undoStack.push_back(std::move(record));
        }
    }

    CDuiString tip;
    if (resultCode == 2)
        tip.Format(_T("%s已取消（完成 %d/%d）"), verb, snap.filesDone, snap.filesTotal);
    else if (resultCode == 1)
        tip.Format(_T("%s失败 (错误 %lu)，已完成 %d/%d"),
            verb, snap.lastError, snap.filesDone, snap.filesTotal);
    else if (wasMove && !m_undoStack.empty())
        tip.Format(_T("移动完成：%d 个文件 → %s（Ctrl+Z 可撤销）"),
            snap.filesDone, m_lastCopyDest.c_str());
    else
        tip.Format(_T("复制完成：%d 个文件 → %s"),
            snap.filesDone, m_lastCopyDest.c_str());
    UpdateStatus(tip.GetData());

    m_jobIsMove = false;

    // A move empties the source folder too, so refresh whenever the job was a move.
    if (wasMove || _wcsicmp(m_currentPath.c_str(), m_lastCopyDest.c_str()) == 0)
        RefreshListing();
}

std::wstring CMainWnd::JoinPath(const std::wstring& dir, const std::wstring& name)
{
    if (dir.empty()) return name;
    std::wstring r = dir;
    if (r.back() != L'\\' && r.back() != L'/')
        r.push_back(L'\\');
    r += name;
    return r;
}

std::wstring CMainWnd::UniqueDestPath(const std::wstring& destPath)
{
    if (::GetFileAttributesW(destPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return destPath;

    size_t slash = destPath.find_last_of(L"\\/");
    std::wstring name = (slash == std::wstring::npos) ? destPath : destPath.substr(slash + 1);
    std::wstring dir = (slash == std::wstring::npos) ? L"" : destPath.substr(0, slash);

    std::wstring base, ext;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        base = name.substr(0, dot);
        ext = name.substr(dot);
    } else {
        base = name;
        ext.clear();
    }

    for (int n = 1; n < 10000; ++n) {
        wchar_t suffix[64] = {};
        if (n == 1) wcscpy_s(suffix, L" - 副本");
        else swprintf_s(suffix, L" - 副本 (%d)", n);
        std::wstring candidate = JoinPath(dir, base + suffix + ext);
        if (::GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES)
            return candidate;
    }
    return destPath;
}

ULONGLONG CMainWnd::CalcPathBytes(const std::wstring& path, bool isDir, std::atomic<bool>& cancel)
{
    if (cancel.load()) return 0;
    if (!isDir) {
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
            return (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        return 0;
    }
    ULONGLONG total = 0;
    std::wstring pattern = JoinPath(path, L"*");
    WIN32_FIND_DATAW fd = {};
    HANDLE h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
        FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (cancel.load()) break;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring child = JoinPath(path, fd.cFileName);
        const bool childDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (childDir) total += CalcPathBytes(child, true, cancel);
        else total += (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return total;
}

ULONGLONG CMainWnd::CalcTotalBytes(const std::vector<ClipboardItem>& items, std::atomic<bool>& cancel)
{
    ULONGLONG total = 0;
    for (const auto& it : items) {
        if (cancel.load()) break;
        total += CalcPathBytes(it.path, it.isDir, cancel);
    }
    return total;
}

int CMainWnd::CountFilesInPath(const std::wstring& path, bool isDir, std::atomic<bool>& cancel)
{
    if (cancel.load()) return 0;
    if (!isDir) return 1;
    int n = 0;
    std::wstring pattern = JoinPath(path, L"*");
    WIN32_FIND_DATAW fd = {};
    HANDLE h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
        FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (cancel.load()) break;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring child = JoinPath(path, fd.cFileName);
        const bool childDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        n += CountFilesInPath(child, childDir, cancel);
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return n;
}

int CMainWnd::CountFiles(const std::vector<ClipboardItem>& items, std::atomic<bool>& cancel)
{
    int n = 0;
    for (const auto& it : items) {
        if (cancel.load()) break;
        n += CountFilesInPath(it.path, it.isDir, cancel);
    }
    return n;
}

void CMainWnd::PostProgress(CMainWnd* self)
{
    if (self && self->m_hWnd)
        ::PostMessageW(self->m_hWnd, kMsgCopyProgress, 0, 0);
}

DWORD CALLBACK CMainWnd::CopyProgressRoutine(
    LARGE_INTEGER TotalFileSize,
    LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER /*StreamSize*/,
    LARGE_INTEGER /*StreamBytesTransferred*/,
    DWORD /*dwStreamNumber*/,
    DWORD /*dwCallbackReason*/,
    HANDLE /*hSourceFile*/,
    HANDLE /*hDestinationFile*/,
    LPVOID lpData)
{
    auto* self = static_cast<CMainWnd*>(lpData);
    if (!self) return PROGRESS_CONTINUE;
    if (self->m_copyCancel.load()) return PROGRESS_CANCEL;

    self->m_workerFileSize = static_cast<ULONGLONG>(TotalFileSize.QuadPart);
    {
        std::lock_guard<std::mutex> lock(self->m_progressMutex);
        self->m_progress.bytesDone = self->m_workerBytesBase
            + static_cast<ULONGLONG>(TotalBytesTransferred.QuadPart);
    }
    PostProgress(self);
    return PROGRESS_CONTINUE;
}

bool CMainWnd::CopyOneFile(CMainWnd* self, const std::wstring& src, const std::wstring& dst)
{
    if (self->m_copyCancel.load()) return false;

    {
        std::lock_guard<std::mutex> lock(self->m_progressMutex);
        size_t slash = src.find_last_of(L"\\/");
        const wchar_t* leaf = (slash == std::wstring::npos) ? src.c_str() : src.c_str() + slash + 1;
        wcsncpy_s(self->m_progress.current, leaf, _TRUNCATE);
    }
    PostProgress(self);

    self->m_workerFileSize = 0;
    BOOL ok = ::CopyFileExW(src.c_str(), dst.c_str(), CopyProgressRoutine, self, nullptr, 0);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err == ERROR_REQUEST_ABORTED || self->m_copyCancel.load())
            return false;
        std::lock_guard<std::mutex> lock(self->m_progressMutex);
        self->m_progress.lastError = err;
        return false;
    }

    self->m_workerBytesBase += self->m_workerFileSize;
    {
        std::lock_guard<std::mutex> lock(self->m_progressMutex);
        self->m_progress.filesDone += 1;
        self->m_progress.bytesDone = self->m_workerBytesBase;
    }
    PostProgress(self);
    return true;
}

bool CMainWnd::CopyDirectoryRecursive(CMainWnd* self, const std::wstring& src, const std::wstring& dst)
{
    if (self->m_copyCancel.load()) return false;

    if (!::CreateDirectoryW(dst.c_str(), nullptr)) {
        DWORD err = ::GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            std::lock_guard<std::mutex> lock(self->m_progressMutex);
            self->m_progress.lastError = err;
            return false;
        }
    }

    std::wstring pattern = JoinPath(src, L"*");
    WIN32_FIND_DATAW fd = {};
    HANDLE h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
        FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) return true;
        std::lock_guard<std::mutex> lock(self->m_progressMutex);
        self->m_progress.lastError = err;
        return false;
    }

    bool ok = true;
    do {
        if (self->m_copyCancel.load()) { ok = false; break; }
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring childSrc = JoinPath(src, fd.cFileName);
        std::wstring childDst = JoinPath(dst, fd.cFileName);
        const bool childDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (childDir) {
            if (!CopyDirectoryRecursive(self, childSrc, childDst)) { ok = false; break; }
        } else {
            if (!CopyOneFile(self, childSrc, childDst)) { ok = false; break; }
        }
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return ok;
}

void CMainWnd::CopyWorkerMain(CMainWnd* self,
    std::vector<ClipboardItem> items,
    std::wstring destDir,
    bool move)
{
    WPARAM result = 0;

    // Measure each item once: the totals feed the progress readout, while the per-item
    // values let a same-volume rename advance the bar (it emits no byte-level progress).
    std::vector<ULONGLONG> itemBytes(items.size(), 0);
    std::vector<int> itemFiles(items.size(), 0);
    ULONGLONG bytesTotal = 0;
    int fileTotal = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (self->m_copyCancel.load()) { result = 2; break; }
        itemBytes[i] = CalcPathBytes(items[i].path, items[i].isDir, self->m_copyCancel);
        itemFiles[i] = CountFilesInPath(items[i].path, items[i].isDir, self->m_copyCancel);
        bytesTotal += itemBytes[i];
        fileTotal += itemFiles[i];
    }
    {
        std::lock_guard<std::mutex> lock(self->m_progressMutex);
        self->m_progress.filesTotal = fileTotal;
        self->m_progress.bytesTotal = bytesTotal;
        self->m_progress.filesDone = 0;
        self->m_progress.bytesDone = 0;
        self->m_progress.state = CopyProgressSnapshot::State::Running;
    }
    PostProgress(self);

    self->m_workerBytesBase = 0;
    self->m_workerFileSize = 0;

    for (size_t i = 0; result != 2 && i < items.size(); ++i) {
        if (self->m_copyCancel.load()) { result = 2; break; }
        const ClipboardItem& it = items[i];

        size_t slash = it.path.find_last_of(L"\\/");
        std::wstring leaf = (slash == std::wstring::npos) ? it.path : it.path.substr(slash + 1);
        std::wstring dest = UniqueDestPath(JoinPath(destDir, leaf));

        const bool ok = move
            ? MoveOneItem(self, it, dest, itemFiles[i], itemBytes[i])
            : (it.isDir ? CopyDirectoryRecursive(self, it.path, dest)
                        : CopyOneFile(self, it.path, dest));
        if (!ok) {
            result = self->m_copyCancel.load() ? 2 : 1;
            break;
        }
        {
            std::lock_guard<std::mutex> lock(self->m_progressMutex);
            self->m_moveUndoPairs.emplace_back(it.path, dest);
        }
    }

    {
        std::lock_guard<std::mutex> lock(self->m_progressMutex);
        if (result == 2)
            self->m_progress.state = CopyProgressSnapshot::State::Cancelled;
        else if (result == 1)
            self->m_progress.state = CopyProgressSnapshot::State::Failed;
        else
            self->m_progress.state = CopyProgressSnapshot::State::Done;
    }

    if (self->m_hWnd)
        ::PostMessageW(self->m_hWnd, kMsgCopyFinished, result, 0);
}

// ---- Move --------------------------------------------------------------

bool CMainWnd::MoveOneItem(CMainWnd* self, const ClipboardItem& item, const std::wstring& dest,
    int itemFiles, ULONGLONG itemBytes)
{
    if (self->m_copyCancel.load()) return false;

    {
        std::lock_guard<std::mutex> lock(self->m_progressMutex);
        wcsncpy_s(self->m_progress.current, GetLeafName(item.path).c_str(), _TRUNCATE);
    }
    PostProgress(self);

    // Fast path: same volume, so this is an atomic rename that moves no data.
    // MOVEFILE_COPY_ALLOWED is deliberately NOT passed - a cross-volume move must fail
    // here so our own engine runs it with progress and a working cancel button.
    if (::MoveFileExW(item.path.c_str(), dest.c_str(), 0)) {
        self->m_workerBytesBase += itemBytes;
        {
            std::lock_guard<std::mutex> lock(self->m_progressMutex);
            self->m_progress.filesDone += itemFiles;
            self->m_progress.bytesDone = self->m_workerBytesBase;
        }
        PostProgress(self);
        return true;
    }

    const DWORD err = ::GetLastError();
    if (err != ERROR_NOT_SAME_DEVICE) {
        std::lock_guard<std::mutex> lock(self->m_progressMutex);
        self->m_progress.lastError = err;
        return false;
    }

    // Cross-volume: copy with progress, then drop the source permanently (not to the bin).
    const bool copied = item.isDir ? CopyDirectoryRecursive(self, item.path, dest)
                                   : CopyOneFile(self, item.path, dest);
    if (!copied) return false;
    if (!DeleteTreePermanent(item.path)) {
        std::lock_guard<std::mutex> lock(self->m_progressMutex);
        self->m_progress.lastError = ::GetLastError();
        return false;
    }
    return true;
}

bool CMainWnd::DeleteTreePermanent(const std::wstring& path)
{
    std::wstring from = path;
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR;
    const int r = ::SHFileOperationW(&op);
    return r == 0 && !op.fAnyOperationsAborted;
}

// ---- Undo (Ctrl+Z) -----------------------------------------------------

void CMainWnd::PushUndo(UndoRecord::Kind kind, std::wstring from, std::wstring to)
{
    ClearRedoHistory(); m_historyStarted = true;
    if (from.empty() && to.empty()) return;
    m_undoStack.push_back(UndoRecord{ kind, std::move(from), std::move(to) });
    constexpr size_t kMaxUndoRecords = 50;
    if (m_undoStack.size() > kMaxUndoRecords) {
        m_undoStack.erase(m_undoStack.begin(),
            m_undoStack.begin() + (m_undoStack.size() - kMaxUndoRecords));
    }
}

void CMainWnd::PushMoveUndo(std::vector<std::pair<std::wstring, std::wstring>> pairs)
{
    if (pairs.empty()) return;
    ClearRedoHistory(); m_historyStarted = true;
    UndoRecord rec;
    rec.kind = UndoRecord::Kind::Move;
    rec.moved = std::move(pairs);
    m_undoStack.push_back(std::move(rec));
    constexpr size_t kMaxUndoRecords = 50;
    if (m_undoStack.size() > kMaxUndoRecords) {
        m_undoStack.erase(m_undoStack.begin(),
            m_undoStack.begin() + (m_undoStack.size() - kMaxUndoRecords));
    }
}

void CMainWnd::TrackShellRename(WPARAM change, LPARAM process)
{
    PIDLIST_ABSOLUTE* paths = nullptr;
    LONG event = 0;
    HANDLE lock = SHChangeNotification_Lock(reinterpret_cast<HANDLE>(change), DWORD(process), &paths, &event);
    if (!lock) return;
    std::wstring from, to;
    PWSTR path = nullptr;
    if (paths && paths[0] && SUCCEEDED(SHGetNameFromIDList(paths[0], SIGDN_FILESYSPATH, &path))) {
        from = path; CoTaskMemFree(path); path = nullptr;
    }
    if (paths && paths[1] && SUCCEEDED(SHGetNameFromIDList(paths[1], SIGDN_FILESYSPATH, &path))) {
        to = path; CoTaskMemFree(path);
    }
    SHChangeNotification_Unlock(lock);
    if (_wcsicmp(from.c_str(), to.c_str()) == 0) return;
    for (auto i = m_appRenameNotifications.begin(); i != m_appRenameNotifications.end(); ++i) {
        if (_wcsicmp(i->first.c_str(), from.c_str()) == 0 && _wcsicmp(i->second.c_str(), to.c_str()) == 0) {
            m_appRenameNotifications.erase(i); return;
        }
    }
    if (m_shellHistoryPending || m_copyRunning || !m_shellBrowser || !m_shellBrowser->IsVisible()) return;
    bool selected = false;
    for (const auto& path : m_recentShellSelection)
        selected = selected || _wcsicmp(path.c_str(), from.c_str()) == 0 || _wcsicmp(path.c_str(), to.c_str()) == 0;
    const bool requested = !m_pendingShellRename.empty() &&
        _wcsicmp(NormalizePath(from).c_str(), NormalizePath(m_pendingShellRename).c_str()) == 0;
    const bool inCurrentFolder = _wcsicmp(ParentPath(from).c_str(), NormalizePath(m_currentPath).c_str()) == 0;
    if (!from.empty() && !to.empty() && (requested || (selected && inCurrentFolder)) &&
        _wcsicmp(ParentPath(from).c_str(), ParentPath(to).c_str()) == 0) {
        m_pendingShellRename.clear();
        PushUndo(UndoRecord::Kind::ShellRename, std::move(from), std::move(to));
    }
}

void CMainWnd::ClearRedoHistory()
{
    // Backup folders are created by copy undo, contain only those copies, and are
    // kept until redo is discarded. Never follow a substituted reparse directory.
    for (const auto& record : m_redoStack) {
        for (const auto& pair : record.backups) {
            const auto folder = ParentPath(pair.second);
            const DWORD attributes = GetFileAttributesW(folder.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                DeleteTreePermanent(pair.second);
                RemoveDirectoryW(folder.c_str());
            }
        }
    }
    m_redoStack.clear();
}

bool CMainWnd::ReplayHistory(UndoRecord& record, bool redo)
{
    auto exists = [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; };
    auto relocate = [&](const std::wstring& from, const std::wstring& to) {
        if (!exists(from)) return exists(to); // retry after a partial operation
        if (exists(to)) return false; // never overwrite an unrelated replacement
        return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED) != FALSE;
    };
    switch (record.kind) {
    case UndoRecord::Kind::Rename: {
        const auto& from = redo ? record.from : record.to;
        const auto& to = redo ? record.to : record.from;
        if (!relocate(from, to)) return false;
        m_appRenameNotifications.emplace_back(from, to);
        if (m_appRenameNotifications.size() > 32) m_appRenameNotifications.erase(m_appRenameNotifications.begin());
        SHChangeNotify(SHCNE_RENAMEITEM, SHCNF_PATHW | SHCNF_FLUSH, from.c_str(), to.c_str());
        return true;
    }
    case UndoRecord::Kind::CreateFolder:
        return redo ? CreateDirectoryW(record.to.c_str(), nullptr) != FALSE
                    : RemoveDirectoryW(record.to.c_str()) != FALSE;
    case UndoRecord::Kind::Move: {
        bool ok = true;
        if (redo) {
            for (const auto& pair : record.moved) ok = relocate(pair.first, pair.second) && ok;
        } else {
            for (auto i = record.moved.rbegin(); i != record.moved.rend(); ++i)
                ok = relocate(i->second, i->first) && ok;
        }
        return ok;
    }
    case UndoRecord::Kind::Copy: {
        if (record.backups.empty()) {
            for (const auto& pair : record.moved) {
                GUID guid{}; wchar_t id[40]{};
                if (FAILED(CoCreateGuid(&guid))) return false;
                StringFromGUID2(guid, id, _countof(id));
                const auto folder = ParentPath(pair.second) + L"\\.FastFileUndo-" + id;
                record.backups.emplace_back(pair.second, folder + L"\\item");
            }
        }
        bool ok = true;
        for (const auto& pair : record.backups) {
            const auto folder = ParentPath(pair.second);
            if (!redo) {
                const DWORD attributes = GetFileAttributesW(folder.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
                if (attributes == INVALID_FILE_ATTRIBUTES && !CreateDirectoryW(folder.c_str(), nullptr)) return false;
                SetFileAttributesW(folder.c_str(), FILE_ATTRIBUTE_HIDDEN);
            }
            ok = (redo ? relocate(pair.second, pair.first) : relocate(pair.first, pair.second)) && ok;
            if (redo) RemoveDirectoryW(folder.c_str());
        }
        return ok;
    }
    case UndoRecord::Kind::ShellRename:
    case UndoRecord::Kind::ShellDelete: {
        if (!m_shellBrowser) return false;
        m_pendingShellRename.clear();
        if (record.kind == UndoRecord::Kind::ShellRename) {
            if (!exists(redo ? record.from : record.to) || exists(redo ? record.to : record.from)) return false;
        } else {
            for (const auto& pair : record.moved) if (exists(pair.first) != redo) return false;
        }
        // Change notifications may arrive after the completion timer. Keep the
        // expected rename excluded until its notification is consumed, so an
        // undo is never mistaken for a new user rename that clears redo.
        const bool rename = record.kind == UndoRecord::Kind::ShellRename;
        const std::pair<std::wstring, std::wstring> notification{
            redo ? record.from : record.to, redo ? record.to : record.from};
        if (rename) m_appRenameNotifications.push_back(notification);
        m_shellHistoryPending = true; m_shellHistoryRedo = redo;
        if (!m_shellBrowser->InvokeHistory(redo)) {
            m_shellHistoryPending = false;
            if (rename) {
                auto i = std::find(m_appRenameNotifications.begin(), m_appRenameNotifications.end(), notification);
                if (i != m_appRenameNotifications.end()) m_appRenameNotifications.erase(i);
            }
            return false;
        }
        // Shell menu verbs can post the operation even when NOASYNC is supplied.
        // Commit the history cursor only after the filesystem confirms completion.
        m_shellHistoryStarted = GetTickCount();
        SetTimer(m_hWnd, kTimerShellHistory, 50, nullptr);
        return true;
    }
    }
    return false;
}

void CMainWnd::OnUndo()
{
    if (m_shellHistoryPending) { UpdateStatus(_T("正在完成 Windows 撤销/重做，请稍候")); return; }
    if (m_copyRunning.load()) { UpdateStatus(_T("有复制/移动任务在进行，完成后再撤销")); return; }
    if (m_undoStack.empty()) {
        if (!m_historyStarted && m_shellBrowser && m_shellBrowser->InvokeHistory(false)) {
            RefreshListing(); UpdateStatus(_T("已撤销 Windows 文件操作"));
        } else UpdateStatus(_T("没有可撤销的操作"));
        return;
    }
    auto& record = m_undoStack.back();
    if (ReplayHistory(record, false)) {
        if (m_shellHistoryPending) { UpdateStatus(_T("正在撤销 Windows 文件操作…")); return; }
        m_redoStack.push_back(std::move(record)); m_undoStack.pop_back();
        UpdateStatus(_T("已撤销（Ctrl+Y 可重做）"));
    } else UpdateStatus(_T("撤销未完成：项目已变化、存在同名项目或无法访问，请检查后重试"));
    RefreshAfterHistory();
}

void CMainWnd::OnRedo()
{
    if (m_shellHistoryPending) { UpdateStatus(_T("正在完成 Windows 撤销/重做，请稍候")); return; }
    if (m_copyRunning.load()) { UpdateStatus(_T("有复制/移动任务在进行，完成后再重做")); return; }
    if (m_redoStack.empty()) {
        if (!m_historyStarted && m_shellBrowser && m_shellBrowser->InvokeHistory(true)) {
            RefreshListing(); UpdateStatus(_T("已重做 Windows 文件操作"));
        } else UpdateStatus(_T("没有可重做的操作"));
        return;
    }
    auto& record = m_redoStack.back();
    if (ReplayHistory(record, true)) {
        if (m_shellHistoryPending) { UpdateStatus(_T("正在重做 Windows 文件操作…")); return; }
        m_undoStack.push_back(std::move(record)); m_redoStack.pop_back();
        UpdateStatus(_T("已重做"));
    } else UpdateStatus(_T("重做未完成：项目已变化、存在同名项目或无法访问，请检查后重试"));
    RefreshAfterHistory();
}

void CMainWnd::RefreshAfterHistory()
{
    // Undoing a copied/new folder can remove the directory currently being viewed.
    // Return to its nearest existing parent instead of retaining an invalid tab path.
    if (!IsThisPcPath(m_currentPath) && GetFileAttributesW(m_currentPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring parent = ParentPath(m_currentPath);
        while (!parent.empty() && !IsThisPcPath(parent) && GetFileAttributesW(parent.c_str()) == INVALID_FILE_ATTRIBUTES) {
            const auto next = ParentPath(parent); if (next == parent) break; parent = next;
        }
        NavigateToNow(parent.empty() ? kThisPcPath : parent, true);
    } else RefreshListing();
}

void CMainWnd::FinishShellHistory()
{
    if (!m_shellHistoryPending) return;
    auto& source = m_shellHistoryRedo ? m_redoStack : m_undoStack;
    auto& target = m_shellHistoryRedo ? m_undoStack : m_redoStack;
    if (source.empty()) { KillTimer(m_hWnd, kTimerShellHistory); m_shellHistoryPending = false; return; }
    const auto& record = source.back();
    auto exists = [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; };
    bool complete = true;
    if (record.kind == UndoRecord::Kind::ShellRename)
        complete = exists(m_shellHistoryRedo ? record.to : record.from) &&
            !exists(m_shellHistoryRedo ? record.from : record.to);
    else for (const auto& pair : record.moved) complete = (exists(pair.first) != m_shellHistoryRedo) && complete;
    if (complete || GetTickCount() - m_shellHistoryStarted > 30000) {
        KillTimer(m_hWnd, kTimerShellHistory); m_shellHistoryPending = false;
        if (complete) { target.push_back(std::move(source.back())); source.pop_back(); }
        RefreshAfterHistory();
        UpdateStatus(complete ? _T("Windows 文件操作已完成") : _T("Windows 文件操作未完成，请检查提示后重试"));
    }
}
// ---- Keyboard helpers --------------------------------------------------

void CMainWnd::FocusSearchBox()
{
    if (!m_pSearchEdit) return;
    SetSearchPlaceholder(false);
    m_pSearchEdit->SetFocus();
    m_pSearchEdit->SetSelAll();
}

void CMainWnd::FocusFileView()
{
    CControlUI* control = IsTileViewMode() ? static_cast<CControlUI*>(m_pIconTiles)
                                         : static_cast<CControlUI*>(m_pFileList);
    if (control) m_PaintManager.SetFocus(control);
    if (m_shellBrowser && m_shellBrowser->Focus()) return;
    ::SetFocus(m_hWnd);
}

void CMainWnd::ShowAddressHistory()
{
    EnterAddressEditMode();
    if (!m_pAddressEdit) return;
    std::vector<std::wstring> paths;
    auto add = [&](const std::wstring& path) {
        if (path.empty() || paths.size() >= 20) return;
        for (const auto& existing : paths) if (_wcsicmp(existing.c_str(), path.c_str()) == 0) return;
        paths.push_back(path);
    };
    add(m_currentPath);
    if (m_activeTab >= 0 && m_activeTab < int(m_tabs.size())) {
        const auto& tab = m_tabs[m_activeTab];
        for (auto i = tab.backStack.rbegin(); i != tab.backStack.rend(); ++i) add(*i);
        for (auto i = tab.forwardStack.rbegin(); i != tab.forwardStack.rend(); ++i) add(*i);
    }
    for (const auto& tab : m_tabs) add(tab.path);
    HMENU menu = CreatePopupMenu(); if (!menu) return;
    for (size_t i = 0; i < paths.size(); ++i)
        AppendMenuW(menu, MF_STRING, i + 1, IsThisPcPath(paths[i]) ? L"此电脑" : paths[i].c_str());
    RECT bounds = m_pAddressEdit->GetPos(); POINT point{bounds.left, bounds.bottom};
    ClientToScreen(m_hWnd, &point);
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        point.x, point.y, m_hWnd, nullptr);
    DestroyMenu(menu);
    if (command > 0 && command <= paths.size()) {
        ExitAddressEditMode(false); NavigateTo(paths[command - 1], true); FocusFileView();
    }
}

void CMainWnd::CycleKeyboardPane(bool reverse)
{
    int pane = 2;
    const HWND focus = ::GetFocus();
    if (m_pAddressEdit && focus == m_pAddressEdit->GetNativeEditHWND()) pane = 0;
    else if (m_pSearchEdit && focus == m_pSearchEdit->GetNativeEditHWND()) pane = 1;
    else if (m_PaintManager.GetFocus() == m_pDirTree) pane = 3;
    pane = (pane + (reverse ? 3 : 1)) % 4;
    if (m_addressEditMode && pane != 0) ExitAddressEditMode(false);
    switch (pane) {
    case 0: EnterAddressEditMode(); break;
    case 1: FocusSearchBox(); break;
    case 2: FocusFileView(); break;
    case 3: if (m_pDirTree) m_pDirTree->SetFocus(); break;
    }
}

void CMainWnd::ShowPropertiesForSelection()
{
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);

    const std::wstring target = items.empty() ? m_currentPath : items.front().path;
    if (target.empty()) {
        UpdateStatus(_T("没有可显示属性的对象"));
        return;
    }

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"properties";
    sei.lpFile = target.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!::ShellExecuteExW(&sei)) {
        CDuiString tip;
        tip.Format(_T("无法显示属性 (错误 %lu)"), ::GetLastError());
        UpdateStatus(tip.GetData());
    }
}
