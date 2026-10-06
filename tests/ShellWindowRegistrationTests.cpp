// Verifies that a window registered through ShellWindowRegistration is found by the
// real Shell, which is what SHOpenFolderAndSelectItems ("show in folder", used by
// browsers, chat clients and download tools) looks for. An unregistered window is never
// found: the call waits ~20 s and returns E_ABORT, and callers may then open Explorer.
//
// The Shell ignores windows on other desktops, so unlike the MainWnd regression
// harness this test runs on the normal desktop with a hidden, off-screen 1x1 window.
// The show-in-folder call runs in a separate --show process, as a browser would. It is
// only made after the parent has confirmed the Shell can find the window, so a broken
// registration fails the test without the Shell opening a folder window anywhere.
// Keep COM registration intact: overriding HKCR with an empty key also removes
// the proxy/stub registrations needed to communicate with the registered window.
//
//   (no args)        expected behaviour (registration, as FastFile does)
//   --unregistered   control run without registration (the behaviour before the fix)
#include <Windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>
#include <propvarutil.h>
#include <shlguid.h>

#include <fstream>
#include <iostream>
#include <string>

#include "ShellWindowRegistration.h"

namespace {

int g_failures = 0;
void Check(bool value, const char* name)
{
    if (!value) { ++g_failures; std::cerr << "FAIL " << name << "\n"; }
}

std::wstring ModulePath()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

bool SamePath(const std::wstring& a, const std::wstring& b)
{
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}

PIDLIST_ABSOLUTE Parse(const std::wstring& path)
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr);
    return pidl;
}

void Pump()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

// Observe only real Explorer windows at the disposable test folder. In particular,
// do not count other user folders or FastFile's registered browser proxy.
int ExplorerWindowsAt(const std::wstring& folder)
{
    IShellWindows* windows = nullptr;
    PIDLIST_ABSOLUTE target = Parse(folder);
    int found = -1;
    if (target && SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) {
        long count = 0;
        if (FAILED(windows->get_Count(&count))) { windows->Release(); CoTaskMemFree(target); return -1; }
        found = 0;
        for (long i = 0; i < count; ++i) {
            VARIANT index{}; index.vt = VT_I4; index.lVal = i;
            IDispatch* item = nullptr;
            if (FAILED(windows->Item(index, &item)) || !item) continue;
            IServiceProvider* provider = nullptr;
            IShellBrowser* browser = nullptr;
            IShellView* view = nullptr;
            IFolderView* folderView = nullptr;
            IPersistFolder2* persist = nullptr;
            PIDLIST_ABSOLUTE current = nullptr;
            if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&provider)))
                && SUCCEEDED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))
                && SUCCEEDED(browser->QueryActiveShellView(&view))
                && SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&folderView)))
                && SUCCEEDED(folderView->GetFolder(IID_PPV_ARGS(&persist)))
                && SUCCEEDED(persist->GetCurFolder(&current)) && ILIsEqual(target, current)) {
                HWND host = nullptr;
                browser->GetWindow(&host);
                wchar_t className[128]{};
                GetClassNameW(GetAncestor(host, GA_ROOT), className, _countof(className));
                if (wcscmp(className, L"CabinetWClass") == 0 || wcscmp(className, L"ExploreWClass") == 0)
                    ++found;
                static HWND lastHost = nullptr;
                if (lastHost != host) {
                    lastHost = host;
                    DWORD pid = 0; GetWindowThreadProcessId(host, &pid);
                    std::wcerr << L"  target window class=" << className << L" pid=" << pid << L"\n";
                }
            }
            CoTaskMemFree(current);
            if (persist) persist->Release();
            if (folderView) folderView->Release();
            if (view) view->Release();
            if (browser) browser->Release();
            if (provider) provider->Release();
            item->Release();
        }
        windows->Release();
    }
    CoTaskMemFree(target);
    return found;
}

// What SHOpenFolderAndSelectItems looks up first: a SWC_BROWSER window at the folder.
HWND FoundWindow(const std::wstring& folder)
{
    IShellWindows* windows = nullptr;
    HWND found = nullptr;
    PIDLIST_ABSOLUTE pidl = Parse(folder);
    if (pidl && SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) {
        VARIANT location{}, empty{};
        VariantInit(&empty);
        if (SUCCEEDED(InitVariantFromBuffer(pidl, ILGetSize(pidl), &location))) {
            long hwnd = 0;
            IDispatch* dispatch = nullptr;
            if (windows->FindWindowSW(&location, &empty, SWC_BROWSER, &hwnd, SWFO_NEEDDISPATCH, &dispatch) == S_OK)
                found = reinterpret_cast<HWND>(static_cast<LONG_PTR>(hwnd));
            if (dispatch) dispatch->Release();
            VariantClear(&location);
        }
        windows->Release();
    }
    CoTaskMemFree(pidl);
    return found;
}

IShellView* ActiveShellView(const std::wstring& folder, IWebBrowser2** navigation = nullptr)
{
    IShellView* view = nullptr;
    IShellWindows* windows = nullptr;
    PIDLIST_ABSOLUTE pidl = Parse(folder);
    if (pidl && SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows)))) {
        VARIANT location{}, empty{};
        VariantInit(&empty);
        if (SUCCEEDED(InitVariantFromBuffer(pidl, ILGetSize(pidl), &location))) {
            long hwnd = 0;
            IDispatch* dispatch = nullptr;
            if (windows->FindWindowSW(&location, &empty, SWC_BROWSER, &hwnd, SWFO_NEEDDISPATCH, &dispatch) == S_OK && dispatch) {
                if(navigation)dispatch->QueryInterface(IID_PPV_ARGS(navigation));
                IServiceProvider* provider = nullptr;
                IShellBrowser* browser = nullptr;
                if (SUCCEEDED(dispatch->QueryInterface(IID_PPV_ARGS(&provider)))) {
                    if (SUCCEEDED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)))) {
                        browser->QueryActiveShellView(&view);
                        browser->Release();
                    }
                    provider->Release();
                }
            }
            if (dispatch) dispatch->Release();
            VariantClear(&location);
        }
        windows->Release();
    }
    CoTaskMemFree(pidl);
    return view;
}

int ShowInFolder(const wchar_t* folderPath, const wchar_t* filePath, const wchar_t* resultPath,
    bool singleAbsolute = false, bool multithreaded = false)
{
    const HRESULT com = CoInitializeEx(nullptr, multithreaded ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 2;
    PIDLIST_ABSOLUTE folder = Parse(folderPath);
    PIDLIST_ABSOLUTE file = Parse(filePath);
    PCUITEMID_CHILD child = file ? ILFindLastID(file) : nullptr;
    const DWORD start = GetTickCount();
    const HRESULT hr = folder && child ? (singleAbsolute ? SHOpenFolderAndSelectItems(file, 0, nullptr, 0)
        : SHOpenFolderAndSelectItems(folder, 1, &child, 0)) : E_FAIL;
    const DWORD ms = GetTickCount() - start;
    CoTaskMemFree(file);
    CoTaskMemFree(folder);
    CoUninitialize();
    std::wofstream out(resultPath);
    out << static_cast<unsigned long>(hr) << L" " << ms << L"\n";
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc >= 3 && wcscmp(argv[1], L"--find") == 0) {
        OleInitialize(nullptr);
        HWND found = FoundWindow(argv[2]);
        wchar_t name[128]{};DWORD pid = 0;
        if (found) {GetClassNameW(found,name,_countof(name));GetWindowThreadProcessId(found,&pid);}
        std::wcout << L"Found window=" << found << L" class=" << name << L" pid=" << pid << L"\n";
        OleUninitialize();return found?0:1;
    }
    // Explicit runtime diagnostic against the real default handler. Kept out of CTest:
    // this opens the supplied disposable fixture in the user's running FastFile.
    if (argc >= 5 && wcscmp(argv[1], L"--live-show") == 0) {
        const int result = ShowInFolder(argv[2], argv[3], argv[4], true);
        OleInitialize(nullptr);
        Check(result == 0, "live caller completes");
        HRESULT hr = E_PENDING; DWORD ms = 0; unsigned long value = 0;
        std::wifstream input(argv[4]);
        if (input >> value >> ms) hr = static_cast<HRESULT>(value);
        Check(hr == S_OK && ms < 5000, "live default handler acknowledges the request promptly");
        const DWORD start = GetTickCount();
        int explorers = 0;
        do {
            const int observed = ExplorerWindowsAt(argv[2]);
            Check(observed >= 0, "live Explorer window observation is available");
            if (observed < 0) break;
            explorers += observed; Pump(); Sleep(250);
        }
        while (GetTickCount() - start < 25000);
        Check(explorers == 0, "no immediate or delayed Explorer window at the requested folder");
        OleUninitialize();
        std::cout << "Live show-in-folder hr=0x" << std::hex << hr << std::dec
            << " ms=" << ms << " explorerObservations=" << explorers << "\n";
        return g_failures ? 1 : 0;
    }
    if (argc >= 5 && (wcscmp(argv[1], L"--show") == 0 || wcscmp(argv[1], L"--show-absolute") == 0
        || wcscmp(argv[1], L"--show-mta") == 0))
        return ShowInFolder(argv[2], argv[3], argv[4], wcscmp(argv[1], L"--show-absolute") == 0,
            wcscmp(argv[1], L"--show-mta") == 0);
    const bool registerWindow = !(argc >= 2 && wcscmp(argv[1], L"--unregistered") == 0);

    OleInitialize(nullptr);
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    const std::wstring root = std::wstring(tempPath) + L"FastFileShellWindow_" + std::to_wstring(GetCurrentProcessId());
    const std::wstring first = root + L"\\First", second = root + L"\\Second", pick = second + L"\\pick.txt";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW(first.c_str(), nullptr);
    CreateDirectoryW(second.c_str(), nullptr);
    { std::ofstream(pick) << "x"; }

    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FastFileShellWindowTest";
    RegisterClassW(&wc);
    // Never shown: off-screen 1x1 tool window on the user's desktop.
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"FastFileShellWindowTest",
        WS_POPUP, -32000, -32000, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    Check(window != nullptr, "test window");

    std::wstring selected;
    UINT selectFlags = 0;
    ShellWindowRegistration registration;
    // What FastFile does when it shows a folder after an external open: a fresh
    // RegisterPending/Register pair at that folder.
    auto registerAt = [&](const std::wstring& folder) {
        if (!registerWindow) return;
        PIDLIST_ABSOLUTE location = Parse(folder);
        const HRESULT hr = registration.Register(window, location, [&](PCIDLIST_ABSOLUTE item, UINT flags) {
            wchar_t path[MAX_PATH]{};
            SHGetPathFromIDListW(item, path);
            selected = path;
            selectFlags = flags;
            return S_OK;
        }, [&](PCIDLIST_ABSOLUTE folder) { return registration.Navigate(folder); });
        CoTaskMemFree(location);
        Check(SUCCEEDED(hr), "window registers in the Shell window list");
        Check(registration.Cookie() == registration.PendingCookie(), "Register completes the pending entry");
    };
    auto showInFolder = [&](const char* label, const std::wstring& folder, const wchar_t* mode = L"--show",
        const std::wstring& navigateFrom = std::wstring()) {
        const auto& discoverAt=navigateFrom.empty()?folder:navigateFrom;
        if (FoundWindow(discoverAt) != window) {
            Check(false, "the Shell finds the registered window showing the folder");
            std::cerr << "  " << label << ": window not discoverable; show-in-folder call skipped\n";
            return;
        }
        IWebBrowser2* navigation=nullptr;
        IShellView* active = ActiveShellView(discoverAt,&navigation);
        Check(active != nullptr, "registered browser provides its active Shell view service");
        Check(navigation != nullptr,"registered browser exposes IWebBrowser2 for cold-folder navigation");
        if(navigation)navigation->Release();
        else { if(active)active->Release();return; }
        if (!active) return; // a broken service must not launch a fallback Explorer
        active->Release();
        selected.clear();
        selectFlags = 0;
        const std::wstring resultFile = root + L"\\show-result.txt";
        DeleteFileW(resultFile.c_str());
        std::wstring command = L"\"" + ModulePath() + L"\" " + mode + L" \"" + folder + L"\" \""
            + pick + L"\" \"" + resultFile + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const bool started = CreateProcessW(nullptr, &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process) != FALSE;
        Check(started, "show-in-folder caller process starts");
        if (!started) return;
        CloseHandle(process.hThread);
        const DWORD deadline = GetTickCount() + 25000;
        bool forwarded=navigateFrom.empty();
        while (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT && GetTickCount() < deadline) {
            Pump();
            // Emulate the actual default-verb activation arriving while the caller
            // waits, rather than incorrectly assuming SHOpen itself navigates a fake
            // test browser that has no association launch command.
            if(!forwarded) { registerAt(folder);forwarded=true; }
            Sleep(5);
        }
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
            Check(false, "show-in-folder caller completes within its deadline");
            TerminateProcess(process.hProcess, 3);
            WaitForSingleObject(process.hProcess, 5000);
        }
        DWORD exitCode = 0;
        GetExitCodeProcess(process.hProcess, &exitCode);
        Check(exitCode == 0, "show-in-folder caller exits successfully");
        CloseHandle(process.hProcess);
        HRESULT hr = E_PENDING;
        DWORD ms = 0;
        {
            std::wifstream in(resultFile);
            unsigned long value = 0;
            if (in >> value >> ms) hr = static_cast<HRESULT>(value);
        }
        DeleteFileW(resultFile.c_str());
        // SHOpenFolderAndSelectItems may acknowledge before its cross-process
        // SelectItem callback is dispatched, especially on a hosted desktop.
        // Wait for the real selection, retaining the caller's separate 5s bound.
        const DWORD selectionDeadline = GetTickCount() + 5000;
        while (selected.empty() && GetTickCount() < selectionDeadline) { Pump(); Sleep(25); }
        std::cerr << "  " << label << ": hr=0x" << std::hex << hr << std::dec << " ms=" << ms
                  << " selectFlags=0x" << std::hex << selectFlags << std::dec << "\n";
        Check(hr == S_OK, "show-in-folder succeeds instead of timing out (E_ABORT)");
        Check(ms < 5000, "show-in-folder completes quickly");
        Check(SamePath(selected, pick), "the Shell asks the FastFile window to select the file");
    };

    // 1. Window shows the folder: another process's show-in-folder request is handed to
    //    it (SelectItem) instead of timing out.
    registerAt(second);
    showInFolder("window at folder", second);
    showInFolder("single absolute item", second, L"--show-absolute");
    showInFolder("MTA caller", second, L"--show-mta");
    registerAt(first);
    IWebBrowser2* navigation=nullptr;
    IShellView* navigationView=ActiveShellView(first,&navigation);
    if(navigationView)navigationView->Release();
    Check(navigation!=nullptr,"navigation interface is available before the destination is open");
    if(navigation) {
        VARIANT invalid{};invalid.vt=VT_I4;invalid.lVal=42;
        Check(navigation->Navigate2(&invalid,nullptr,nullptr,nullptr,nullptr)==E_INVALIDARG
            && FoundWindow(first)==window,"invalid navigation leaves the current location intact");
        VARIANT destination{};PIDLIST_ABSOLUTE target=Parse(second);
        if(target && SUCCEEDED(InitVariantFromBuffer(target,ILGetSize(target),&destination))) {
            Check(navigation->Navigate2(&destination,nullptr,nullptr,nullptr,nullptr)==S_OK
                && FoundWindow(second)==window,"binary PIDL navigation routes to the registered FastFile window");
            VARIANT text{};text.vt=VT_BSTR;text.bstrVal=SysAllocString(first.c_str());
            Check(navigation->Navigate2(&text,nullptr,nullptr,nullptr,nullptr)==S_OK
                && FoundWindow(first)==window,"string navigation routes to the registered FastFile window");
            VariantClear(&text);
            VARIANT indirect{};indirect.vt=VT_VARIANT|VT_BYREF;indirect.pvarVal=&destination;
            Check(navigation->Navigate2(&indirect,nullptr,nullptr,nullptr,nullptr)==S_OK
                && FoundWindow(second)==window,"by-reference navigation preserves the absolute target PIDL");
            const BYTE invalidPidl[]={0,0,0};VARIANT malformed{};
            if(SUCCEEDED(InitVariantFromBuffer(invalidPidl,sizeof(invalidPidl),&malformed))) {
                Check(navigation->Navigate2(&malformed,nullptr,nullptr,nullptr,nullptr)==E_INVALIDARG
                    && FoundWindow(second)==window,"malformed PIDL cannot change the registered location");
                VariantClear(&malformed);
            } else Check(false,"malformed navigation fixture is created");
            VariantClear(&destination);
        } else Check(false,"navigation PIDL fixture is created");
        CoTaskMemFree(target);navigation->Release();
    }
    registerAt(first);
    IShellView* pendingView=ActiveShellView(first);
    registerAt(second);
    if(pendingView) {
        HWND retainedWindow=nullptr;PIDLIST_ABSOLUTE item=Parse(pick);
        Check(pendingView->GetWindow(&retainedWindow)==S_OK && retainedWindow==window,
            "navigation reannouncement keeps an in-flight Shell view alive");
        selected.clear();
        Check(item && pendingView->SelectItem(ILFindLastID(item),SVSI_SELECT)==S_OK && SamePath(selected,pick),
            "in-flight selection survives replacing the Shell registration cookie");
        CoTaskMemFree(item);pendingView->Release();
    } else Check(false,"in-flight selection view is available");
    // Start the request before the window has opened the destination: the previous
    // tests only exercised a folder that was already discoverable in IShellWindows.
    registerAt(first);
    showInFolder("folder forwarding as caller starts", second, L"--show-absolute", first);
    // 2. Window at another folder, then a forwarded external open of the requested folder
    //    arrives: the fresh registration must make it discoverable there at once.
    registerAt(first);
    Check(FoundWindow(first) == window, "registered window is found at its folder");
    Check(FoundWindow(second) == nullptr, "window is not reported for a folder it does not show");
    registerAt(second);
    Check(FoundWindow(second) == window, "window is found at the forwarded folder right away");
    showInFolder("after forwarded open", second);
    // Windows/callers can time out around 20 seconds and open Explorer later. Observe
    // beyond that interval while keeping the registered window and message pump alive.
    if (registerWindow && FoundWindow(second) == window) {
        const DWORD start = GetTickCount();
        int explorers = 0;
        do {
            const int observed = ExplorerWindowsAt(second);
            Check(observed >= 0, "Explorer window observation is available");
            if (observed < 0) break;
            explorers += observed; Pump(); Sleep(250);
        }
        while (GetTickCount() - start < 25000);
        Check(explorers == 0, "no delayed duplicate Explorer window after a successful show-in-folder request");
    }
    // 3. Ordinary navigation inside the window moves the location (OnNavigate).
    {
        PIDLIST_ABSOLUTE location = Parse(first);
        if (registerWindow) registration.Navigate(location);
        CoTaskMemFree(location);
    }
    Check(FoundWindow(first) == window, "navigation moves the registered location");
    Check(FoundWindow(second) == nullptr, "the previous folder is no longer reported");
    // 4. Revoke (window closing) removes it.
    IWebBrowser2* outstandingBrowser=nullptr;
    IShellView* outstanding = ActiveShellView(first,&outstandingBrowser);
    Check(outstanding != nullptr, "view reference can outlive window registration");
    registration.Revoke();
    Check(FoundWindow(first) == nullptr, "revoked window is no longer reported");
    if (outstanding) {
        const std::wstring before = selected;
        PIDLIST_ABSOLUTE item = Parse(pick);
        Check(item && FAILED(outstanding->SelectItem(ILFindLastID(item), SVSI_SELECT)),
            "revoked view cannot call back into a closing window");
        Check(selected == before, "revoked view preserves previous selection");
        HWND gone = nullptr;
        Check(FAILED(outstanding->GetWindow(&gone)) && gone == nullptr,
            "revoked view no longer exposes a window handle");
        CoTaskMemFree(item);
        outstanding->Release();
    }
    if(outstandingBrowser) {
        VARIANT destination{};PIDLIST_ABSOLUTE folder=Parse(second);
        if(folder && SUCCEEDED(InitVariantFromBuffer(folder,ILGetSize(folder),&destination))) {
            Check(FAILED(outstandingBrowser->Navigate2(&destination,nullptr,nullptr,nullptr,nullptr)),
                "revoked browser cannot navigate a closed FastFile window");
            VariantClear(&destination);
        }
        CoTaskMemFree(folder);outstandingBrowser->Release();
    }

    DestroyWindow(window);
    DeleteFileW(pick.c_str());
    RemoveDirectoryW(first.c_str());
    RemoveDirectoryW(second.c_str());
    RemoveDirectoryW(root.c_str());
    OleUninitialize();
    if (g_failures) std::cerr << "ShellWindowRegistrationTests: " << g_failures << " failures\n";
    else std::cout << "ShellWindowRegistrationTests: all passed\n";
    return g_failures ? 1 : 0;
}
