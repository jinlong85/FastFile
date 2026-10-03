#pragma once
// Registers a FastFile main window in the Shell's window list (IShellWindows).
//
// When another application asks Windows to "show this file in its folder"
// (SHOpenFolderAndSelectItems, used by browsers, chat clients and download tools),
// Windows can run the folder's default verb and then wait for a *registered shell window* that displays that folder
// so it can select the file.  An unregistered window is never found: the caller blocks
// for about 20 seconds, gets E_ABORT, and many callers then open Explorer themselves,
// so the user sees FastFile and an Explorer window.
//
// The registration follows the documented RegisterPending / Register / OnNavigate /
// Revoke sequence with an SWC_BROWSER window, an IWebBrowserApp object whose document
// provides an IShellView service, and a SelectItem that is routed back to FastFile.

#include <Windows.h>
#include <shlobj.h>

#include <functional>
#include <memory>

struct IShellWindows;

class ShellWindowRegistration final {
public:
    // Absolute item to select and the SVSI_* flags requested by the Shell.
    using SelectHandler = std::function<HRESULT(PCIDLIST_ABSOLUTE item, UINT flags)>;
    using NavigateHandler = std::function<HRESULT(PCIDLIST_ABSOLUTE folder)>;

    ShellWindowRegistration() = default;
    ~ShellWindowRegistration();
    ShellWindowRegistration(const ShellWindowRegistration&) = delete;
    ShellWindowRegistration& operator=(const ShellWindowRegistration&) = delete;

    // Must run on the window's UI thread (RegisterPending is keyed by thread id).
    HRESULT Register(HWND window, PCIDLIST_ABSOLUTE location, SelectHandler onSelect,
        NavigateHandler onNavigate = {});
    // Reports the folder now shown by the window.
    HRESULT Navigate(PCIDLIST_ABSOLUTE location);
    void Revoke();
    bool IsRegistered() const { return m_registered; }
    long Cookie() const { return m_cookie; }
    long PendingCookie() const { return m_pendingCookie; }

    struct State;

private:
    IShellWindows* m_windows = nullptr;
    long m_cookie = 0;
    long m_pendingCookie = 0;
    bool m_registered = false;
    std::shared_ptr<State> m_state;
};
