#pragma once
// FastFile - Windows-native file operations (IFileOperation).
//
// Copy, move, recycle and permanent delete all go through the Shell copy engine so the user
// gets Explorer's own progress dialog (pause / cancel / time remaining), the native
// replace-or-skip conflict dialog and elevation prompts. Each operation runs on its own STA
// worker thread (see CMainWnd::StartFileOperation) so the FastFile window keeps painting and
// navigating while the engine works; the main window is passed as owner of the Shell UI.
//
// The completed (source, result) pairs are taken from IFileOperationProgressSink
// (PostCopyItem / PostMoveItem / PostDeleteItem) and verified against the file system, so
// FastFile's own Ctrl+Z history only records items that were really created, moved or
// recycled. Items the user skipped, merged into an existing folder or replaced are excluded.

#include <Windows.h>
#include <shobjidl.h>

#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace ShellFileOps {

enum class Kind { Copy, Move, Recycle, Delete };
enum class Engine { None, FileOperation, LegacyFileOp };   // LegacyFileOp = SHFileOperationW fallback

struct Request {
    Kind kind = Kind::Copy;
    std::vector<std::wstring> sources;
    std::wstring destination;          // target folder for Copy / Move
    HWND owner = nullptr;              // owner of the Shell progress / conflict UI
    // Production always uses the interactive Windows UI. false is reserved for automated
    // tests on isolated temporary folders: no progress, confirmation or error UI at all.
    bool interactive = true;
    bool useLegacyEngine = false;      // tests only: exercise the SHFileOperation fallback
};

struct Result {
    Kind kind = Kind::Copy;
    Engine engine = Engine::None;
    DWORD flags = 0;                   // operation flags actually handed to the engine
    HRESULT hr = E_FAIL;
    bool aborted = false;              // user cancelled or skipped at least one item
    size_t requested = 0;
    std::wstring destination;
    // Copy / Move: (source, created or moved path). Recycle / Delete: (path, path).
    std::vector<std::pair<std::wstring, std::wstring>> completed;
    // Items that succeeded but cannot be reverted safely (merged / replaced destination).
    size_t notUndoable = 0;
};

// Flags used for a kind of operation. Interactive flags never contain FOF_SILENT,
// FOF_NOERRORUI, FOFX_NOMINIMIZEBOX or a blanket FOF_NOCONFIRMATION for copy / move, so the
// native progress dialog, conflict dialog and error UI stay available.
DWORD OperationFlags(Kind kind, bool interactive);

// Performs the operation synchronously on the calling thread, which must be a COM STA.
// cancel (optional) aborts the remaining items at the next engine callback.
Result Perform(const Request& request, const std::atomic<bool>* cancel = nullptr);

// Engine-independent helpers shared with the FastFile window and its tests.
bool PathsEqual(const std::wstring& a, const std::wstring& b);
std::wstring LeafName(const std::wstring& path);

} // namespace ShellFileOps
