// FastFile - file commands (Windows-native verbs), rename helpers and the IFileOperation
// engine used by drag-and-drop transfers. FastFile keeps no undo history of its own: every
// operation lands in the Windows undo history, which Ctrl+Z / Ctrl+Y replay natively.
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Declarations live in MainWnd.h; the copy engine itself is ShellFileOperation.cpp.

#include "MainWndInternal.h"
#include "FastFileCore.h"

#include <memory>
#include <sherrors.h>

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

// Command-bar buttons and shortcuts: the same canonical Windows verbs as Explorer.
void CMainWnd::OnCopyClicked() { RunFileCommand(FileCommand::Copy); }

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

void CMainWnd::OnPasteClicked() { RunFileCommand(FileCommand::Paste); }

void CMainWnd::OnCancelCopyClicked()
{
    if (!m_copyRunning.load()) return;
    m_copyCancel.store(true);
    UpdateStatus(_T("正在取消剩余的文件操作…"));
}

void CMainWnd::OnDeleteClicked(bool permanent)
{
    // Windows' own delete: recycle (Shift = permanent) with Windows' confirmation rules.
    RunFileCommand(permanent ? FileCommand::DeletePermanent : FileCommand::Delete);
}

void CMainWnd::OnRenameClicked()
{
    // The hosted Windows view renames in place with its own edit (IFolderView2::DoRename),
    // exactly like F2 in Explorer, including multi-item rename.
    if (!IsTreeKeyboardFocus() && m_shellBrowser && m_shellBrowser->IsCreated() && m_shellBrowser->IsVisible()) {
        if (m_shellBrowser->SelectedCount() <= 0) UpdateStatus(_T("请先选中要重命名的项目"));
        else if (!m_shellBrowser->BeginRename()) UpdateStatus(_T("无法启动 Windows 原生重命名"));
        return;
    }
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
    if (RenameItem(item, entered)) RefreshListing();
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

// The tree and FastFile's search list have no Shell view for the native in-place edit, so
// they collect the name in FastFile's edit and let the Windows engine rename the item
// (IFileOperation::RenameItem) with an undo record in the system history, so Ctrl+Z (here or
// from the folder menu's 撤销) restores it like an Explorer rename.
bool CMainWnd::RenameItem(const ClipboardItem& item, const std::wstring& newName)
{
    std::wstring parent = ParentPath(item.path);
    if (parent.empty()) parent = item.path; // shouldn't happen
    const std::wstring dest = JoinPath(parent, newName);
    if (::GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES && !PathEquals(dest, item.path)) {
        UpdateStatus(_T("目标名称已存在"));
        return false;
    }
    IShellItem* shellItem = nullptr;
    IFileOperation* operation = nullptr;
    HRESULT hr = ::SHCreateItemFromParsingName(item.path.c_str(), nullptr, IID_PPV_ARGS(&shellItem));
    if (SUCCEEDED(hr)) hr = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    if (SUCCEEDED(hr)) {
        DWORD flags = FOF_ALLOWUNDO | FOFX_ADDUNDORECORD | FOFX_SHOWELEVATIONPROMPT | FOF_NOCONFIRMMKDIR;
        if (!m_fileOpsInteractive) flags = FOF_ALLOWUNDO | FOFX_ADDUNDORECORD | FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION;
        hr = operation->SetOperationFlags(flags);
    }
    if (SUCCEEDED(hr)) hr = operation->SetOwnerWindow(m_hWnd);
    if (SUCCEEDED(hr)) hr = operation->RenameItem(shellItem, newName.c_str(), nullptr);
    if (SUCCEEDED(hr)) hr = operation->PerformOperations();
    BOOL aborted = FALSE;
    if (SUCCEEDED(hr) && operation) operation->GetAnyOperationsAborted(&aborted);
    if (operation) operation->Release();
    if (shellItem) shellItem->Release();
    if (FAILED(hr) || aborted || ::GetFileAttributesW(dest.c_str()) == INVALID_FILE_ATTRIBUTES) {
        CDuiString tip;
        tip.Format(_T("重命名未完成 (0x%08X)"), static_cast<unsigned>(hr));
        UpdateStatus(aborted ? _T("已取消重命名") : tip.GetData());
        return false;
    }
    PushUndoRename(item.path, dest);
    UpdateStatus(_T("重命名完成（Ctrl+Z 可撤销）"));
    return true;
}

void CMainWnd::OnNewFolderClicked() { RunFileCommand(FileCommand::NewFolder); }

void CMainWnd::OnCutClicked() { RunFileCommand(FileCommand::Cut); }

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

// ---- Windows file operations (IFileOperation on STA worker threads) ----------

void CMainWnd::ApplyCopyUiState()
{
    // Progress, pause and cancel live in the Windows progress dialog; the old status-bar
    // cancel button stays hidden.
    if (m_pBtnCancelCopy) {
        m_pBtnCancelCopy->SetVisible(false);
        m_pBtnCancelCopy->SetEnabled(false);
    }
    // The command bar buttons (cut/copy/paste/rename/share/delete) follow the selection and
    // clipboard state instead of being hard-wired here.
    UpdateCommandBarState();
}

void CMainWnd::StopCopyThread(bool wait)
{
    m_copyCancel.store(true);
    for (auto& job : m_fileOpJobs) {
        if (!job || !job->thread.joinable()) continue;
        if (wait) job->thread.join();
        else job->thread.detach();
    }
    m_fileOpJobs.clear();
    m_copyRunning.store(false);
}

bool CMainWnd::StartFileOperation(ShellFileOps::Kind kind, std::vector<std::wstring> sources,
    std::wstring destination)
{
    if (sources.empty() || !m_hWnd) return false;
    if (m_fileOpJobs.empty() && !m_closeAfterFileOps) m_copyCancel.store(false);
    ShellFileOps::Request request;
    request.kind = kind;
    request.sources = std::move(sources);
    request.destination = std::move(destination);
    request.owner = m_hWnd;               // Explorer's progress / conflict UI is owned by FastFile
    request.interactive = m_fileOpsInteractive;
    m_lastFileOpRequestFlags = ShellFileOps::OperationFlags(kind, request.interactive);

    auto job = std::make_unique<FileOperationJob>();
    job->id = m_nextFileOpId++;
    const unsigned id = job->id;
    const HWND notify = m_hWnd;
    // The worker must live on the window's desktop so the owned Shell dialogs can appear.
    const HDESK desktop = ::GetThreadDesktop(::GetCurrentThreadId());
    std::atomic<bool>* cancel = &m_copyCancel;
    try {
        job->thread = std::thread([request = std::move(request), id, notify, desktop, cancel]() {
            if (desktop) ::SetThreadDesktop(desktop);
            const HRESULT init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            auto* result = new (std::nothrow) ShellFileOps::Result(ShellFileOps::Perform(request, cancel));
            if (SUCCEEDED(init)) ::CoUninitialize();
            if (result && !::PostMessageW(notify, kMsgFileOpFinished, id, reinterpret_cast<LPARAM>(result)))
                delete result;
        });
    } catch (...) {
        UpdateStatus(_T("无法启动 Windows 文件操作"));
        return false;
    }
    m_fileOpJobs.push_back(std::move(job));
    m_copyRunning.store(true);
    ApplyCopyUiState();
    return true;
}

void CMainWnd::OnFileOperationFinished(WPARAM id, LPARAM resultPointer)
{
    std::unique_ptr<ShellFileOps::Result> result(reinterpret_cast<ShellFileOps::Result*>(resultPointer));
    for (auto i = m_fileOpJobs.begin(); i != m_fileOpJobs.end(); ++i) {
        if (!*i || (*i)->id != static_cast<unsigned>(id)) continue;
        if ((*i)->thread.joinable()) (*i)->thread.join();
        m_fileOpJobs.erase(i);
        break;
    }
    m_copyRunning.store(!m_fileOpJobs.empty());
    if (result) {
        m_lastFileOperation = *result;
        ApplyFileOperationResult(*result);
    }
    ApplyCopyUiState();
    if (m_closeAfterFileOps && m_fileOpJobs.empty() && m_hWnd)
        ::PostMessageW(m_hWnd, WM_CLOSE, 0, 0);
}

void CMainWnd::ApplyFileOperationResult(const ShellFileOps::Result& result)
{
    using Kind = ShellFileOps::Kind;
    if (result.kind == Kind::Copy && !result.completed.empty()) {
        PushUndoCopy(result.completed);
    }
    // The operation carried FOFX_ADDUNDORECORD, so it is already in the Windows undo history.
    UpdateStatus(DescribeFileOperation(result).c_str());
    // ExplorerBrowser follows change notifications itself; refresh FastFile's own state
    // (details pane, tree, a deleted current folder) for the affected folders.
    if (result.kind == Kind::Recycle || result.kind == Kind::Delete) RefreshAfterFileChange();
    else if (result.kind == Kind::Move || PathEquals(m_currentPath, result.destination)) RefreshListing();
}

std::wstring CMainWnd::DescribeFileOperation(const ShellFileOps::Result& result)
{
    using Kind = ShellFileOps::Kind;
    const int done = static_cast<int>(result.completed.size() + result.notUndoable);
    const int total = static_cast<int>(result.requested);
    const wchar_t* verb = result.kind == Kind::Copy ? L"复制" : result.kind == Kind::Move ? L"移动"
        : result.kind == Kind::Recycle ? L"删除" : L"永久删除";
    wchar_t text[1024]{};
    const bool cancelled = result.hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || result.hr == E_ABORT
        || result.hr == COPYENGINE_E_USER_CANCELLED || result.aborted;
    if (SUCCEEDED(result.hr) && !result.aborted && done == total) {
        switch (result.kind) {
        case Kind::Copy: swprintf_s(text, L"已复制 %d 项 → %s", done, result.destination.c_str()); break;
        case Kind::Move: swprintf_s(text, L"已移动 %d 项 → %s", done, result.destination.c_str()); break;
        case Kind::Recycle: swprintf_s(text, L"已删除到回收站 %d 项", done); break;
        case Kind::Delete: swprintf_s(text, L"已永久删除 %d 项", done); break;
        }
    } else if (cancelled) {
        swprintf_s(text, L"%s已取消或部分项目已跳过，已处理 %d / %d 项", verb, done, total);
    } else {
        wchar_t* message = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, DWORD(result.hr), 0,
            reinterpret_cast<wchar_t*>(&message), 0, nullptr);
        std::wstring reason = message ? message : L"Windows 未完成该操作";
        if (message) LocalFree(message);
        while (!reason.empty() && (reason.back() == L'\n' || reason.back() == L'\r')) reason.pop_back();
        swprintf_s(text, L"%s未全部完成，已处理 %d / %d 项：%s (0x%08X)", verb, done, total,
            reason.c_str(), DWORD(result.hr));
    }
    std::wstring status = text;
    if (!result.completed.empty() && result.kind != Kind::Delete) status += L"（Ctrl+Z 可撤销）";
    if (result.notUndoable) status += L"；" + std::to_wstring(result.notUndoable) + L" 项已合并或替换，不能撤销";
    return status;
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

void CMainWnd::RefreshAfterFileChange()
{
    // A removed directory can be the one currently being viewed. Return to its nearest
    // existing parent instead of retaining an invalid tab path.
    if (!IsThisPcPath(m_currentPath) && GetFileAttributesW(m_currentPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring parent = ParentPath(m_currentPath);
        while (!parent.empty() && !IsThisPcPath(parent) && GetFileAttributesW(parent.c_str()) == INVALID_FILE_ATTRIBUTES) {
            const auto next = ParentPath(parent); if (next == parent) break; parent = next;
        }
        NavigateToNow(parent.empty() ? kThisPcPath : parent, true);
    } else RefreshListing();
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
    // Windows' own 属性 verb: the selected items' sheet, or the folder's when nothing is
    // selected (Alt+Enter on the background in Explorer).
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    const bool viewSelection = !IsTreeKeyboardFocus() && m_shellBrowser && m_shellBrowser->IsCreated()
        && m_shellBrowser->IsVisible() && m_shellBrowser->SelectedCount() > 0;
    const bool selection = viewSelection || (!items.empty()
        && !(m_shellBrowser && m_shellBrowser->IsCreated() && m_shellBrowser->IsVisible() && !IsTreeKeyboardFocus()));
    if (!InvokeNativeVerb(L"properties", selection ? NativeScope::Selection : NativeScope::Background))
        UpdateStatus(_T("没有可显示属性的对象"));
}

void CMainWnd::PushUndoRename(const std::wstring& oldPath, const std::wstring& newPath)
{
    m_undoStack.push_back({ UndoKind::Rename, oldPath, newPath, {} });
    m_redoStack.clear();
}

void CMainWnd::PushUndoCopy(const std::vector<std::pair<std::wstring, std::wstring>>& completedCopies)
{
    std::vector<std::wstring> dests;
    dests.reserve(completedCopies.size());
    for (const auto& pair : completedCopies) {
        if (!pair.second.empty()) dests.push_back(pair.second);
    }
    if (!dests.empty()) {
        m_undoStack.push_back({ UndoKind::CopyFiles, L"", L"", std::move(dests) });
        m_redoStack.clear();
    }
}

bool CMainWnd::ApplyUndoEntry(const UndoEntry& entry, bool isRedo)
{
    if (entry.kind == UndoKind::Rename) {
        const std::wstring current = isRedo ? entry.oldPath : entry.newPath;
        const std::wstring target = isRedo ? entry.newPath : entry.oldPath;
        const std::wstring targetLeaf = GetLeafName(target);
        if (::GetFileAttributesW(current.c_str()) == INVALID_FILE_ATTRIBUTES) {
            UpdateStatus(isRedo ? _T("重做失败：原文件不存在") : _T("撤销失败：当前文件不存在"));
            return false;
        }
        IShellItem* shellItem = nullptr;
        IFileOperation* operation = nullptr;
        HRESULT hr = ::SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(&shellItem));
        if (SUCCEEDED(hr)) hr = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
        if (SUCCEEDED(hr)) {
            DWORD flags = FOF_ALLOWUNDO | FOFX_ADDUNDORECORD | FOFX_SHOWELEVATIONPROMPT | FOF_NOCONFIRMMKDIR;
            if (!m_fileOpsInteractive) flags = FOF_ALLOWUNDO | FOFX_ADDUNDORECORD | FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION;
            hr = operation->SetOperationFlags(flags);
        }
        if (SUCCEEDED(hr)) hr = operation->SetOwnerWindow(m_hWnd);
        if (SUCCEEDED(hr)) hr = operation->RenameItem(shellItem, targetLeaf.c_str(), nullptr);
        if (SUCCEEDED(hr)) hr = operation->PerformOperations();
        BOOL aborted = FALSE;
        if (SUCCEEDED(hr) && operation) operation->GetAnyOperationsAborted(&aborted);
        if (operation) operation->Release();
        if (shellItem) shellItem->Release();
        if (FAILED(hr) || aborted) {
            UpdateStatus(isRedo ? _T("重做重命名失败") : _T("撤销重命名失败"));
            return false;
        }
        RefreshListing();
        UpdateStatus(isRedo ? _T("已重做重命名") : _T("已撤销重命名"));
        return true;
    }
    if (entry.kind == UndoKind::CopyFiles) {
        if (isRedo) {
            UpdateStatus(_T("复制操作暂不支持重做"));
            return false;
        }
        IFileOperation* operation = nullptr;
        HRESULT hr = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
        if (FAILED(hr) || !operation) {
            UpdateStatus(_T("撤销复制失败：无法创建文件操作组件"));
            return false;
        }
        DWORD flags = FOFX_RECYCLEONDELETE | FOF_ALLOWUNDO | FOFX_ADDUNDORECORD | FOFX_SHOWELEVATIONPROMPT | FOF_NOCONFIRMATION;
        if (!m_fileOpsInteractive) flags = FOF_ALLOWUNDO | FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION;
        operation->SetOperationFlags(flags);
        operation->SetOwnerWindow(m_hWnd);
        int queued = 0;
        for (const auto& path : entry.copiedDests) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(::SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
                if (SUCCEEDED(operation->DeleteItem(item, nullptr))) {
                    ++queued;
                }
                item->Release();
            }
        }
        if (queued == 0) {
            operation->Release();
            UpdateStatus(_T("撤销复制失败：副本已不存在"));
            return false;
        }
        hr = operation->PerformOperations();
        BOOL aborted = FALSE;
        operation->GetAnyOperationsAborted(&aborted);
        operation->Release();
        if (FAILED(hr) || aborted) {
            UpdateStatus(_T("撤销复制失败"));
            return false;
        }
        RefreshListing();
        UpdateStatus(_T("已撤销复制（已移除副本）"));
        return true;
    }
    return false;
}
