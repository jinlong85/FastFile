// FastFile — lightweight file manager (P0 scaffold)
// Tech: DuiLib (XML skin) + C++ / Win32
// Do NOT reverse or ship any 360 binaries; fresh open-source scaffold only.

#include "MainWnd.h"

#include <ObjBase.h>
#include <shellapi.h> // CommandLineToArgvW
#include <shlobj.h>   // SHGetFolderPathW / CSIDL_LOCAL_APPDATA for the crash report
#include <shlwapi.h>  // PathFindFileNameW (crash-report stack frames)

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((DPI_AWARENESS_CONTEXT)-3)
#endif

namespace {
// Must match CMainWnd::kMsgReactivate / GetWindowClassName()
constexpr UINT kMsgReactivate = WM_USER + 100;
constexpr wchar_t kMainWndClass[] = L"FastFile_MainWnd";
constexpr ULONG_PTR kOpenPathsCopyData = 0x46464F50; // "FFOP"; must match CMainWnd
// Filled in by wWinMain so a crash report can tell UI crashes from worker-thread ones.
DWORD g_mainThreadId = 0;

// Writes %LOCALAPPDATA%\FastFile\last_crash.txt with the exception code and the faulting
// address *relative to its module*. FastFile.map (next to the exe, produced by the linker's
// /MAP) then resolves that RVA to a function without needing a debugger on this machine.
LONG WINAPI FastFileCrashHandler(EXCEPTION_POINTERS* info)
{
    if (!info || !info->ExceptionRecord)
        return EXCEPTION_EXECUTE_HANDLER;
    const EXCEPTION_RECORD* rec = info->ExceptionRecord;

    wchar_t dir[MAX_PATH] = {};
    if (::SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, dir) == S_OK
        && dir[0] != L'\0') {
        std::wstring path = std::wstring(dir) + L"\\FastFile";
        ::CreateDirectoryW(path.c_str(), nullptr);
        path += L"\\last_crash.txt";
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"w") == 0 && f) {
            fwprintf(f, L"exception=0x%08X\n", static_cast<unsigned>(rec->ExceptionCode));
            fwprintf(f, L"address=0x%016llX\n",
                static_cast<unsigned long long>(reinterpret_cast<ULONGLONG>(rec->ExceptionAddress)));
            HMODULE mod = nullptr;
            if (::GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(rec->ExceptionAddress), &mod) && mod) {
                wchar_t modPath[MAX_PATH] = {};
                ::GetModuleFileNameW(mod, modPath, MAX_PATH);
                const unsigned long long rva =
                    reinterpret_cast<ULONGLONG>(rec->ExceptionAddress) - reinterpret_cast<ULONGLONG>(mod);
                fwprintf(f, L"module=%s\nrva=0x%llX\n", modPath, rva);
            } else {
                fwprintf(f, L"module=<unknown>\n");
            }
            fwprintf(f, L"tid=%lu\n", ::GetCurrentThreadId());
            fwprintf(f, L"main_tid=%lu\n", g_mainThreadId);

            // Walk the faulting thread's stack so FastFile.map can resolve the call chain
            // (release builds may drop frames, but the immediate caller is what matters).
            void* frames[40] = {};
            const USHORT n = ::RtlCaptureStackBackTrace(0, 40, frames, nullptr);
            for (USHORT i = 0; i < n; ++i) {
                HMODULE frameMod = nullptr;
                if (!::GetModuleHandleExW(
                        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        reinterpret_cast<LPCWSTR>(frames[i]), &frameMod) || !frameMod)
                    continue;
                wchar_t framePath[MAX_PATH] = {};
                ::GetModuleFileNameW(frameMod, framePath, MAX_PATH);
                const unsigned long long frameRva =
                    reinterpret_cast<ULONGLONG>(frames[i]) - reinterpret_cast<ULONGLONG>(frameMod);
                fwprintf(f, L"stack%02u=%s+0x%llX\n", static_cast<unsigned>(i),
                    ::PathFindFileNameW(framePath), frameRva);
            }
            fclose(f);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

std::vector<std::wstring> ParseOpenPaths()
{
    std::vector<std::wstring> paths;
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv)
        return paths;

    bool expectPath = false;
    bool literalArgs = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg(argv[i] ? argv[i] : L"");
        if (arg == L"--") {
            literalArgs = true;
            expectPath = true;
            continue;
        }
        if (!literalArgs && ::_wcsicmp(arg.c_str(), L"--open") == 0) {
            expectPath = true;
            continue;
        }
        // Unknown flags are reserved for future invocations.  A normal bare path is still
        // accepted, so a shortcut can target FastFile.exe and pass a folder directly.
        if (!literalArgs && arg.size() > 2 && arg[0] == L'-' && arg[1] == L'-') {
            expectPath = false;
            continue;
        }
        if (expectPath || !arg.empty())
            paths.push_back(arg);
        expectPath = false;
    }
    ::LocalFree(argv);
    return paths;
}

bool ShellOpenRequested()
{
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool requested = false;
    for (int i = 1; argv && i < argc; ++i) {
        if (wcscmp(argv[i], L"--") == 0) break;
        if (_wcsicmp(argv[i], L"--open") == 0) { requested = true; break; }
    }
    LocalFree(argv);
    return requested;
}

// "--new-window" (used when a tab is dragged out of the window) must NOT be folded into an
// existing instance: the caller wants a separate HWND.
bool ForceNewWindowRequested()
{
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv)
        return false;
    bool force = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && ::_wcsicmp(argv[i], L"--new-window") == 0) {
            force = true;
            break;
        }
    }
    ::LocalFree(argv);
    return force;
}

// "--geometry=x,y,w,h" - a tab dragged out of the window is handed to the clone process so it
// opens at the drop point with the *source* window's size instead of the default design size.
bool StartupGeometryRequested(RECT& out)
{
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv)
        return false;
    bool found = false;
    for (int i = 1; i < argc && !found; ++i) {
        if (!argv[i] || ::_wcsnicmp(argv[i], L"--geometry=", 11) != 0)
            continue;
        int x = 0, y = 0, w = 0, h = 0;
        if (::swscanf_s(argv[i] + 11, L"%d,%d,%d,%d", &x, &y, &w, &h) == 4 && w > 0 && h > 0) {
            out.left = x;
            out.top = y;
            out.right = x + w;
            out.bottom = y + h;
            found = true;
        }
    }
    ::LocalFree(argv);
    return found;
}

bool ForwardOpenPaths(HWND existing, const std::vector<std::wstring>& paths)
{
    if (!existing || paths.empty())
        return false;
    std::wstring payload;
    std::vector<std::wstring> valid;
    for (const std::wstring& path : paths) {
        if (path.empty())
            continue;
        if (!payload.empty())
            payload.push_back(L'\n');
        payload += path;
        valid.push_back(path);
    }
    if (payload.empty())
        return false;
    payload.push_back(L'\0');
    // This process was just started by the Shell and may set the foreground window;
    // hand that right to the running FastFile before asking it to come forward.
    DWORD pid = 0;
    ::GetWindowThreadProcessId(existing, &pid);
    if (pid)
        ::AllowSetForegroundWindow(pid);
    COPYDATASTRUCT cds = {};
    cds.dwData = kOpenPathsCopyData;
    cds.cbData = static_cast<DWORD>(payload.size() * sizeof(wchar_t));
    cds.lpData = const_cast<wchar_t*>(payload.c_str());
    DWORD_PTR accepted = 0;
    if (::SendMessageTimeoutW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
            SMTO_BLOCK, 1500, &accepted) != 0 && accepted != 0)
        return true;
    // The window exists but is busy (for example its UI thread is waiting for a sleeping
    // disk). Do not drop the request and never fall back to Explorer: queue it and post a
    // notification that the window handles as soon as it is free.
    const std::wstring queued = CMainWnd::QueueExternalOpen(valid);
    if (queued.empty())
        return false;
    if (::PostMessageW(existing, CMainWnd::kMsgDrainOpenQueue, 0, 0))
        return true;
    ::DeleteFileW(queued.c_str()); // the window is gone; this process opens the folder itself
    return false;
}

bool ActivateExistingInstance(const std::vector<std::wstring>& paths)
{
    HWND existing = ::FindWindowW(kMainWndClass, nullptr);
    if (!existing)
        return false;
    if (!paths.empty())
        return ForwardOpenPaths(existing, paths); // false: open a window of our own
    DWORD pid = 0;
    ::GetWindowThreadProcessId(existing, &pid);
    if (pid)
        ::AllowSetForegroundWindow(pid);
    ::PostMessageW(existing, kMsgReactivate, 0, 0);
    // Also nudge taskbar-style restore in case PostMessage is delayed
    if (::IsIconic(existing))
        ::ShowWindow(existing, SW_RESTORE);
    ::SetForegroundWindow(existing);
    return true;
}

// Enable Per-Monitor DPI awareness v2 before any HWND / GDI work.
// Manifest embeds the same; this is a belt-and-suspenders runtime call.
void EnablePerMonitorDpiAwareness()
{
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetCtxFn = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setCtx = reinterpret_cast<SetCtxFn>(
            ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setCtx) {
            if (setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
                return;
            if (setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
                return;
        }
    }
    HMODULE shcore = ::LoadLibraryW(L"Shcore.dll");
    if (shcore) {
        using SetAwareFn = HRESULT (WINAPI*)(int);
        auto setAware = reinterpret_cast<SetAwareFn>(
            ::GetProcAddress(shcore, "SetProcessDpiAwareness"));
        // PROCESS_PER_MONITOR_DPI_AWARE = 2
        if (setAware)
            setAware(2);
        ::FreeLibrary(shcore);
    }
}
} // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrev*/, LPWSTR /*lpCmdLine*/, int /*nShow*/)
{
    g_mainThreadId = ::GetCurrentThreadId();
    EnablePerMonitorDpiAwareness();
    ::SetUnhandledExceptionFilter(FastFileCrashHandler);

    int restoreArgc=0;auto restoreArgv=CommandLineToArgvW(GetCommandLineW(),&restoreArgc);
    bool restoreIntegration=false;
    bool repairIntegration=false;
    for(int i=1;restoreArgv && i<restoreArgc;++i)if(wcscmp(restoreArgv[i],L"--restore-integration")==0)restoreIntegration=true;
    for(int i=1;restoreArgv && i<restoreArgc;++i)if(wcscmp(restoreArgv[i],L"--repair-integration")==0)repairIntegration=true;
    LocalFree(restoreArgv);
    if(restoreIntegration) {
        FastFileSettings disabled;
        return CMainWnd::ApplySystemIntegration(disabled) && CMainWnd::RestoreNativeFolderHandlers() ? 0 : 2;
    }
    if(repairIntegration) {
        FastFileSettings current;CMainWnd::ReadSystemIntegration(current);
        return CMainWnd::ApplySystemIntegration(current)?0:2;
    }
    CMainWnd::RestoreNativeFolderHandlers();
    CMainWnd::RepairOwnedSystemIntegration();

    // Single-instance: tray / second launch should restore the existing main HWND
    const std::vector<std::wstring> startupPaths = ParseOpenPaths();
    // Existing Explorer views can keep a cached folder verb after association changes.
    // Honor an explicit disable even for those stale --open invocations, before IPC.
    if (ShellOpenRequested() && CMainWnd::RedirectDisabledShellOpen(startupPaths))
        return 0;
    // ... unless the caller explicitly asked for a second window (tab dragged out).
    if (!ForceNewWindowRequested() && (startupPaths.empty() || !FastFileSettings::Load(FastFileSettings::FilePath()).externalNewWindow) && ActivateExistingInstance(startupPaths))
        return 0;

    HRESULT hr = ::OleInitialize(nullptr);
    if (FAILED(hr))
        return -1;

    CPaintManagerUI::SetInstance(hInstance);
    // Do NOT SetResourcePath(GetInstancePath()) here.
    // WindowImplBase::OnCreate only appends GetSkinFolder() ("skin") when the
    // resource path is still empty. Setting it to the exe dir made DuiLib look
    // for <exe_dir>\main.xml instead of <exe_dir>\skin\main.xml, then
    // MessageBox + ExitProcess(1).
    // Leave path empty so OnCreate builds: GetInstancePath() + GetSkinFolder().

    // Heap-allocated on purpose and never freed: the thumbnail/copy workers keep a
    // back-pointer to this object, and joining them on shutdown can take many seconds
    // (Shell video thumbnails). Letting the object live until the process exits keeps
    // those detached workers from touching freed memory and keeps the close instant.
    CMainWnd* mainWnd = new CMainWnd();
    mainWnd->SetStartupOpenPaths(startupPaths);
    HWND hWnd = mainWnd->Create(nullptr, _T("FastFile"), UI_WNDSTYLE_FRAME, WS_EX_WINDOWEDGE);
    if (hWnd == nullptr)
    {
        ::MessageBoxW(nullptr,
            L"Create window failed (skin XML may be missing under <exe>\\skin\\main.xml).",
            L"FastFile", MB_OK | MB_ICONERROR);
        ::OleUninitialize();
        return 1;
    }
    mainWnd->CenterWindow();
    mainWnd->ShowWindow(true);
    mainWnd->EnsureDpiLayout();
    RECT startupGeom = {};
    if (StartupGeometryRequested(startupGeom)) {
        // Drag-out clone: keep the source window's size at the drop point.
        ::SetWindowPos(hWnd, nullptr, startupGeom.left, startupGeom.top,
            startupGeom.right - startupGeom.left, startupGeom.bottom - startupGeom.top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        mainWnd->CenterWindow();
    }

    CPaintManagerUI::MessageLoop();
    CPaintManagerUI::Term();

    ::OleUninitialize();
    return 0;
}
