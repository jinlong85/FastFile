// FastFile — lightweight file manager (P0 scaffold)
// Tech: DuiLib (XML skin) + C++ / Win32
// Do NOT reverse or ship any 360 binaries; fresh open-source scaffold only.

#include "MainWnd.h"

#include <ObjBase.h>

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

bool ActivateExistingInstance()
{
    HWND existing = ::FindWindowW(kMainWndClass, nullptr);
    if (!existing)
        return false;
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
    EnablePerMonitorDpiAwareness();

    // Single-instance: tray / second launch should restore the existing main HWND
    if (ActivateExistingInstance())
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

    CMainWnd mainWnd;
    HWND hWnd = mainWnd.Create(nullptr, _T("FastFile"), UI_WNDSTYLE_FRAME, WS_EX_WINDOWEDGE);
    if (hWnd == nullptr)
    {
        ::MessageBoxW(nullptr,
            L"Create window failed (skin XML may be missing under <exe>\\skin\\main.xml).",
            L"FastFile", MB_OK | MB_ICONERROR);
        ::OleUninitialize();
        return 1;
    }
    mainWnd.CenterWindow();
    mainWnd.ShowWindow(true);
    mainWnd.EnsureDpiLayout();
    mainWnd.CenterWindow();

    CPaintManagerUI::MessageLoop();
    CPaintManagerUI::Term();

    ::OleUninitialize();
    return 0;
}
