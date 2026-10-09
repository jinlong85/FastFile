// FastFile - Windows-native file operations (see ShellFileOperation.h)

#include "ShellFileOperation.h"

#include <shellapi.h>
#include <shlobj.h>
#include <sherrors.h>

namespace ShellFileOps {
namespace {

bool Exists(const std::wstring& path)
{
    return !path.empty() && ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool Missing(const std::wstring& path)
{
    if (::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    const DWORD error = ::GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

std::wstring Normalize(const std::wstring& path)
{
    if (path.empty()) return path;
    wchar_t buffer[32768];
    const DWORD length = ::GetFullPathNameW(path.c_str(), _countof(buffer), buffer, nullptr);
    std::wstring result = (length > 0 && length < _countof(buffer)) ? std::wstring(buffer, length) : path;
    for (auto& ch : result) if (ch == L'/') ch = L'\\';
    while (result.size() > 3 && result.back() == L'\\') result.pop_back();
    return result;
}

std::wstring Join(const std::wstring& folder, const std::wstring& leaf)
{
    std::wstring result = folder;
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/') result.push_back(L'\\');
    return result + leaf;
}

std::wstring ItemPath(IShellItem* item)
{
    std::wstring result;
    PWSTR path = nullptr;
    if (item && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        result = path;
        ::CoTaskMemFree(path);
    }
    return result;
}

// Collects the engine's per-item results. Cancellation is honoured in every Pre* callback:
// a failing Pre* callback cancels that item and every operation still pending.
class ProgressSink final : public IFileOperationProgressSink
{
public:
    explicit ProgressSink(const std::atomic<bool>* cancel) : m_cancel(cancel) {}

    std::vector<std::pair<std::wstring, std::wstring>> transferred;
    std::vector<std::wstring> deleted;

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        if (!object) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IFileOperationProgressSink)) {
            *object = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(::InterlockedIncrement(&m_ref)); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        const LONG ref = ::InterlockedDecrement(&m_ref);
        if (ref == 0) delete this;
        return static_cast<ULONG>(ref);
    }

    IFACEMETHODIMP StartOperations() override { return S_OK; }
    IFACEMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    IFACEMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return Gate(); }
    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return Gate(); }
    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT hr,
        IShellItem* created) override { Record(item, hr, created); return S_OK; }
    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return Gate(); }
    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT hr,
        IShellItem* created) override { Record(item, hr, created); return S_OK; }
    IFACEMETHODIMP PreDeleteItem(DWORD, IShellItem*) override { return Gate(); }
    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem* item, HRESULT hr, IShellItem*) override
    {
        if (SUCCEEDED(hr) && hr != COPYENGINE_S_USER_IGNORED) {
            auto path = ItemPath(item);
            if (!path.empty()) deleted.push_back(std::move(path));
        }
        return S_OK;
    }
    IFACEMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return Gate(); }
    IFACEMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
    IFACEMETHODIMP ResetTimer() override { return S_OK; }
    IFACEMETHODIMP PauseTimer() override { return S_OK; }
    IFACEMETHODIMP ResumeTimer() override { return S_OK; }

private:
    HRESULT Gate() const
    {
        return m_cancel && m_cancel->load() ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK;
    }
    void Record(IShellItem* item, HRESULT hr, IShellItem* created)
    {
        if (FAILED(hr) || hr == COPYENGINE_S_USER_IGNORED || !created) return;
        auto source = ItemPath(item);
        auto result = ItemPath(created);
        if (!source.empty() && !result.empty())
            transferred.emplace_back(std::move(source), std::move(result));
    }

    LONG m_ref = 1;
    const std::atomic<bool>* m_cancel = nullptr;
};

struct Snapshot {
    std::vector<std::wstring> sources;   // normalized
    std::vector<std::wstring> targets;   // destination\leaf for Copy / Move
    std::vector<bool> existedBefore;     // source (delete) or target (copy / move)
};

Snapshot TakeSnapshot(const Request& request)
{
    Snapshot snapshot;
    const bool transfer = request.kind == Kind::Copy || request.kind == Kind::Move;
    for (const auto& source : request.sources) {
        snapshot.sources.push_back(Normalize(source));
        const auto target = transfer ? Join(Normalize(request.destination), LeafName(snapshot.sources.back()))
                                     : std::wstring();
        snapshot.targets.push_back(target);
        snapshot.existedBefore.push_back(Exists(transfer ? target : snapshot.sources.back()));
    }
    return snapshot;
}

void Verify(const Request& request, const Snapshot& snapshot, const ProgressSink* sink, Result& result)
{
    const bool transfer = request.kind == Kind::Copy || request.kind == Kind::Move;
    std::vector<bool> settled(snapshot.sources.size(), false);
    auto indexOf = [&](const std::wstring& path) -> size_t {
        for (size_t i = 0; i < snapshot.sources.size(); ++i)
            if (!settled[i] && PathsEqual(snapshot.sources[i], path)) return i;
        return snapshot.sources.size();
    };
    auto accept = [&](size_t i, const std::wstring& created) {
        settled[i] = true;
        if (transfer && PathsEqual(created, snapshot.targets[i]) && snapshot.existedBefore[i]) {
            ++result.notUndoable; // merged into / replaced an existing item
            return;
        }
        result.completed.emplace_back(request.sources[i], created);
    };
    // 1) Results reported by the engine for the requested top-level items. Callbacks for
    //    nested children of a copied folder do not match a requested source and are ignored.
    if (sink && transfer) {
        for (const auto& pair : sink->transferred) {
            const size_t i = indexOf(pair.first);
            if (i == snapshot.sources.size()) continue;
            const bool present = Exists(pair.second);
            const bool sourceGone = request.kind != Kind::Move || Missing(snapshot.sources[i]);
            if (present && sourceGone) accept(i, Normalize(pair.second));
        }
    } else if (sink) {
        for (const auto& path : sink->deleted) {
            const size_t i = indexOf(path);
            if (i != snapshot.sources.size() && Missing(snapshot.sources[i])) accept(i, request.sources[i]);
        }
    }
    // 2) File-system check for anything the engine did not report (legacy engine, or a
    //    provider that skips the sink): only unambiguous outcomes are recorded.
    for (size_t i = 0; i < snapshot.sources.size(); ++i) {
        if (settled[i]) continue;
        if (!transfer) {
            if (snapshot.existedBefore[i] && Missing(snapshot.sources[i])) accept(i, request.sources[i]);
        } else if (!snapshot.existedBefore[i] && Exists(snapshot.targets[i])
            && (request.kind != Kind::Move || Missing(snapshot.sources[i]))) {
            accept(i, snapshot.targets[i]);
        }
    }
}

Result PerformLegacy(const Request& request, Result result, const Snapshot& snapshot)
{
    result.engine = Engine::LegacyFileOp;
    std::wstring from;
    for (const auto& source : request.sources) { from += source; from.push_back(L'\0'); }
    from.push_back(L'\0');
    std::wstring to = request.destination;
    to.push_back(L'\0'); to.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.hwnd = request.owner;
    operation.wFunc = request.kind == Kind::Copy ? FO_COPY : request.kind == Kind::Move ? FO_MOVE : FO_DELETE;
    operation.pFrom = from.c_str();
    operation.pTo = (request.kind == Kind::Copy || request.kind == Kind::Move) ? to.c_str() : nullptr;
    operation.fFlags = static_cast<FILEOP_FLAGS>(result.flags & 0xFFFF);
    const int code = ::SHFileOperationW(&operation);
    result.hr = code == 0 ? S_OK : code == ERROR_CANCELLED ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : E_FAIL;
    result.aborted = operation.fAnyOperationsAborted != FALSE;
    Verify(request, snapshot, nullptr, result);
    return result;
}

} // namespace

DWORD OperationFlags(Kind kind, bool interactive)
{
    DWORD flags = 0;
    switch (kind) {
    case Kind::Copy:
    case Kind::Move:
        // Native progress, conflict (replace / skip / keep both) and error dialogs stay on.
        // The undo record puts drag-and-drop transfers into the Windows undo history, the
        // only history FastFile has (Ctrl+Z replays it natively, as in Explorer).
        flags = FOF_NOCONFIRMMKDIR | FOFX_SHOWELEVATIONPROMPT | FOF_ALLOWUNDO | FOFX_ADDUNDORECORD;
        break;
    case Kind::Recycle:
        // Routine "move to Recycle Bin?" is not asked (Windows 10/11 default); the warning
        // for items that cannot be recycled still is. The Explorer undo record backs Ctrl+Z.
        flags = FOF_NOCONFIRMATION | FOFX_SHOWELEVATIONPROMPT | FOF_ALLOWUNDO |
            FOFX_ADDUNDORECORD | FOFX_RECYCLEONDELETE | FOF_WANTNUKEWARNING;
        break;
    case Kind::Delete:
        // Engine-level permanent delete (no FastFile command issues it any more; the
        // Delete key and 删除 button run Windows' own delete verb with its confirmation).
        flags = FOF_NOCONFIRMATION | FOFX_SHOWELEVATIONPROMPT;
        break;
    }
    if (!interactive) {
        flags &= ~static_cast<DWORD>(FOFX_SHOWELEVATIONPROMPT | FOF_WANTNUKEWARNING);
        flags |= FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR;
        if (kind == Kind::Copy || kind == Kind::Move) flags |= FOF_RENAMEONCOLLISION;
    }
    return flags;
}

bool PathsEqual(const std::wstring& a, const std::wstring& b)
{
    const auto left = Normalize(a), right = Normalize(b);
    return !left.empty() && ::CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
        right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

std::wstring LeafName(const std::wstring& path)
{
    std::wstring trimmed = path;
    while (trimmed.size() > 3 && (trimmed.back() == L'\\' || trimmed.back() == L'/')) trimmed.pop_back();
    const size_t slash = trimmed.find_last_of(L"\\/");
    return slash == std::wstring::npos ? trimmed : trimmed.substr(slash + 1);
}

Result Perform(const Request& request, const std::atomic<bool>* cancel)
{
    Result result;
    result.kind = request.kind;
    result.requested = request.sources.size();
    result.destination = request.destination;
    result.flags = OperationFlags(request.kind, request.interactive);
    const bool transfer = request.kind == Kind::Copy || request.kind == Kind::Move;
    if (request.sources.empty() || (transfer && request.destination.empty())) {
        result.hr = E_INVALIDARG;
        return result;
    }
    const Snapshot snapshot = TakeSnapshot(request);

    IFileOperation* operation = nullptr;
    HRESULT hr = request.useLegacyEngine ? E_NOINTERFACE
        : ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    // SHFileOperation fallback: same flags (lower word), same Windows progress dialog.
    if (FAILED(hr) || !operation) return PerformLegacy(request, result, snapshot);
    result.engine = Engine::FileOperation;

    if (request.owner) operation->SetOwnerWindow(request.owner);
    hr = operation->SetOperationFlags(result.flags);
    auto* sink = new ProgressSink(cancel);
    DWORD cookie = 0;
    if (SUCCEEDED(hr)) hr = operation->Advise(sink, &cookie);

    IShellItem* folder = nullptr;
    if (SUCCEEDED(hr) && transfer)
        hr = ::SHCreateItemFromParsingName(request.destination.c_str(), nullptr, IID_PPV_ARGS(&folder));

    size_t queued = 0;
    for (size_t i = 0; SUCCEEDED(hr) && i < request.sources.size(); ++i) {
        IShellItem* item = nullptr;
        // A source that vanished since it was selected is simply not part of the batch.
        if (FAILED(::SHCreateItemFromParsingName(request.sources[i].c_str(), nullptr, IID_PPV_ARGS(&item))))
            continue;
        HRESULT queuedHr = E_FAIL;
        switch (request.kind) {
        case Kind::Copy: queuedHr = operation->CopyItem(item, folder, nullptr, nullptr); break;
        case Kind::Move: queuedHr = operation->MoveItem(item, folder, nullptr, nullptr); break;
        case Kind::Recycle:
        case Kind::Delete: queuedHr = operation->DeleteItem(item, nullptr); break;
        }
        item->Release();
        if (SUCCEEDED(queuedHr)) ++queued;
        else hr = queuedHr;
    }
    if (SUCCEEDED(hr) && queued == 0) hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if (SUCCEEDED(hr)) {
        hr = operation->PerformOperations();
        BOOL aborted = FALSE;
        if (SUCCEEDED(operation->GetAnyOperationsAborted(&aborted))) result.aborted = aborted != FALSE;
    }
    if (cookie) operation->Unadvise(cookie);
    if (folder) folder->Release();
    operation->Release();
    result.hr = hr;
    Verify(request, snapshot, sink, result);
    sink->Release();
    return result;
}

} // namespace ShellFileOps
