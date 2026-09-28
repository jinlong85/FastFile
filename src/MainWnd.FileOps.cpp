// FastFile - file operations and the background copy engine
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

namespace {

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

    if (!m_pFileList) return;

    const int n = m_pFileList->GetCount();
    for (int i = 0; i < n; ++i) {
        CControlUI* p = m_pFileList->GetItemAt(i);
        if (!p) continue;
        IListItemUI* pListItem = static_cast<IListItemUI*>(p->GetInterface(DUI_CTR_ILISTITEM));
        if (!pListItem || !pListItem->IsSelected()) continue;
        CDuiString ud = p->GetUserData();
        if (ud.IsEmpty()) continue;
        ClipboardItem item;
        item.path = ud.GetData();
        item.isDir = (p->GetTag() != 0);
        out.push_back(std::move(item));
    }

    if (out.empty()) {
        const int cur = m_pFileList->GetCurSel();
        if (cur >= 0) {
            CControlUI* p = m_pFileList->GetItemAt(cur);
            if (p) {
                CDuiString ud = p->GetUserData();
                if (!ud.IsEmpty()) {
                    ClipboardItem item;
                    item.path = ud.GetData();
                    item.isDir = (p->GetTag() != 0);
                    out.push_back(std::move(item));
                }
            }
        }
    }
}

void CMainWnd::OnCopyClicked()
{
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.empty()) {
        UpdateStatus(_T("请先选中要复制的文件或文件夹（支持 Ctrl/Shift 多选）"));
        return;
    }
    m_clipboard = std::move(items);
    m_clipboardIsCut = false;
    CDuiString tip;
    tip.Format(_T("已复制 %d 项 — 切换到目标面板后点「粘贴」（可跨左右面板）"),
        static_cast<int>(m_clipboard.size()));
    UpdateStatus(tip.GetData());
    ApplyCopyUiState();
}

void CMainWnd::OnPasteClicked()
{
    if (m_copyRunning.load()) {
        UpdateStatus(_T("已有复制任务在进行，请等待或取消"));
        return;
    }
    if (m_clipboard.empty()) {
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
        std::vector<std::wstring> paths;
        paths.reserve(m_clipboard.size());
        for (const auto& it : m_clipboard)
            paths.push_back(it.path);
        if (TransferWithShell(paths, m_currentPath, true)) {
            m_clipboard.clear();
            m_clipboardIsCut = false;
            ApplyCopyUiState();
        }
        return;
    }

    m_lastCopyDest = m_currentPath;
    StartCopyJob(m_clipboard, m_currentPath);
}

void CMainWnd::OnCancelCopyClicked()
{
    if (!m_copyRunning.load()) return;
    m_copyCancel.store(true);
    UpdateStatus(_T("正在取消复制…"));
}

void CMainWnd::OnDeleteClicked()
{
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.empty()) {
        UpdateStatus(_T("请先选中要删除的项目"));
        return;
    }

    CDuiString msg;
    if (items.size() == 1) {
        msg.Format(_T("确定将「%s」删除到回收站吗？"), GetLeafName(items[0].path).c_str());
    } else {
        msg.Format(_T("确定将选中的 %d 项删除到回收站吗？"), static_cast<int>(items.size()));
    }
    int ret = ::MessageBoxW(m_hWnd, msg.GetData(), L"FastFile - 确认删除",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (ret != IDYES) {
        UpdateStatus(_T("已取消删除"));
        return;
    }

    if (DeleteItems(items)) {
        CDuiString tip;
        tip.Format(_T("已删除 %d 项到回收站"), static_cast<int>(items.size()));
        UpdateStatus(tip.GetData());
        RefreshListing();
    }
}

bool CMainWnd::DeleteItems(const std::vector<ClipboardItem>& items)
{
    // Build double-null-terminated path list for SHFileOperation
    std::wstring from;
    for (const auto& it : items) {
        from += it.path;
        from.push_back(L'\0');
    }
    from.push_back(L'\0');

    SHFILEOPSTRUCTW op = {};
    op.hwnd = m_hWnd;
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    int r = ::SHFileOperationW(&op);
    if (r != 0 || op.fAnyOperationsAborted) {
        CDuiString tip;
        tip.Format(_T("删除失败或已中止 (代码 %d)"), r);
        UpdateStatus(tip.GetData());
        return false;
    }
    return true;
}

void CMainWnd::OnRenameClicked()
{
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

    if (!InvokeShellRename(items[0].path))
        UpdateStatus(_T("Windows Shell 未能启动重命名"));
}

bool CMainWnd::InvokeShellRename(const std::wstring& path)
{
    const std::wstring parent = ParentPath(path);
    const std::wstring leaf = GetLeafName(path);
    if (parent.empty() || leaf.empty()) return false;
    PIDLIST_ABSOLUTE pidlFolder = nullptr;
    SFGAOF attrs = 0;
    if (FAILED(::SHParseDisplayName(parent.c_str(), nullptr, &pidlFolder, 0, &attrs)) || !pidlFolder)
        return false;
    IShellFolder* folder = nullptr;
    HRESULT hr = ::SHBindToObject(nullptr, pidlFolder, nullptr, IID_IShellFolder,
        reinterpret_cast<void**>(&folder));
    ::CoTaskMemFree(pidlFolder);
    if (FAILED(hr) || !folder) return false;
    PIDLIST_RELATIVE child = nullptr;
    DWORD childAttrs = 0;
    hr = folder->ParseDisplayName(m_hWnd, nullptr, const_cast<LPWSTR>(leaf.c_str()),
        nullptr, &child, &childAttrs);
    IContextMenu* menu = nullptr;
    if (SUCCEEDED(hr) && child) {
        LPCITEMIDLIST item = child;
        hr = folder->GetUIObjectOf(m_hWnd, 1, &item, IID_IContextMenu, nullptr,
            reinterpret_cast<void**>(&menu));
    }
    if (child) ::CoTaskMemFree(child);
    folder->Release();
    if (FAILED(hr) || !menu) return false;
    CMINVOKECOMMANDINFOEX info = {};
    info.cbSize = sizeof(info);
    info.fMask = CMIC_MASK_UNICODE;
    info.hwnd = m_hWnd;
    info.lpVerb = "rename";
    info.lpVerbW = L"rename";
    info.nShow = SW_SHOWNORMAL;
    hr = menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
    menu->Release();
    if (SUCCEEDED(hr)) {
        UpdateStatus(_T("已交由 Windows Shell 重命名"));
        RefreshListing();
        return true;
    }
    return false;
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
    return true;
}

void CMainWnd::OnNewFolderClicked()
{
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
    if (entered.find_first_of(L"\\/") != std::wstring::npos || entered.empty()
        || entered == L"." || entered == L"..") {
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
    tip.Format(_T("已创建: %s"), GetLeafName(dest).c_str());
    UpdateStatus(tip.GetData());
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
    if (m_pBtnPaste)
        m_pBtnPaste->SetEnabled(!running && !m_clipboard.empty());
    if (m_pBtnCopy)
        m_pBtnCopy->SetEnabled(true);
    if (m_pBtnCancelCopy) {
        m_pBtnCancelCopy->SetVisible(running);
        m_pBtnCancelCopy->SetEnabled(running);
    }
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

void CMainWnd::StartCopyJob(std::vector<ClipboardItem> items, std::wstring destDir)
{
    StopCopyThread(true);

    m_copyCancel.store(false);
    m_copyRunning.store(true);
    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress = CopyProgressSnapshot{};
        m_progress.state = CopyProgressSnapshot::State::Running;
        m_progress.filesTotal = static_cast<int>(items.size());
    }
    m_workerBytesBase = 0;
    m_workerFileSize = 0;

    ApplyCopyUiState();
    UpdateStatus(_T("正在准备复制…（可继续浏览目录）"));

    m_copyThread = std::thread([this, items = std::move(items), destDir = std::move(destDir)]() mutable {
        CopyWorkerMain(this, std::move(items), std::move(destDir));
    });
}

void CMainWnd::OnCopyProgressMessage()
{
    CopyProgressSnapshot snap;
    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        snap = m_progress;
    }

    wchar_t buf[512] = {};
    const wchar_t* name = snap.current[0] ? snap.current : L"…";
    if (snap.bytesTotal > 0) {
        const double pct = (100.0 * static_cast<double>(snap.bytesDone))
            / static_cast<double>(snap.bytesTotal);
        swprintf_s(buf,
            L"复制中 %d/%d  ·  %s / %s (%.0f%%)  ·  %s  ·  可继续浏览",
            snap.filesDone, snap.filesTotal,
            FormatFileSize(snap.bytesDone).c_str(),
            FormatFileSize(snap.bytesTotal).c_str(),
            pct, name);
    } else {
        swprintf_s(buf, L"复制中 %d/%d  ·  %s  ·  可继续浏览",
            snap.filesDone, snap.filesTotal, name);
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

    CDuiString tip;
    if (resultCode == 2)
        tip.Format(_T("复制已取消（完成 %d/%d）"), snap.filesDone, snap.filesTotal);
    else if (resultCode == 1)
        tip.Format(_T("复制失败 (错误 %lu)，已完成 %d/%d"),
            snap.lastError, snap.filesDone, snap.filesTotal);
    else
        tip.Format(_T("复制完成：%d 个文件 → %s"),
            snap.filesDone, m_lastCopyDest.c_str());
    UpdateStatus(tip.GetData());

    if (_wcsicmp(m_currentPath.c_str(), m_lastCopyDest.c_str()) == 0)
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
    std::wstring destDir)
{
    WPARAM result = 0;

    const int fileTotal = CountFiles(items, self->m_copyCancel);
    const ULONGLONG bytesTotal = CalcTotalBytes(items, self->m_copyCancel);
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

    for (const auto& it : items) {
        if (self->m_copyCancel.load()) { result = 2; break; }

        size_t slash = it.path.find_last_of(L"\\/");
        std::wstring leaf = (slash == std::wstring::npos) ? it.path : it.path.substr(slash + 1);
        std::wstring dest = UniqueDestPath(JoinPath(destDir, leaf));

        bool ok = it.isDir
            ? CopyDirectoryRecursive(self, it.path, dest)
            : CopyOneFile(self, it.path, dest);
        if (!ok) {
            result = self->m_copyCancel.load() ? 2 : 1;
            break;
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
