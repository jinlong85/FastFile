#include "ShellBrowserHost.h"
#include "ShellPresentation.h"
#include "FileTagManager.h"
#include <algorithm>

#include <KnownFolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <commoncontrols.h>
#include <new>
#include <utility>
#include <vector>
#include <shldisp.h>
#include <shdispid.h>
#include <ocidl.h>
#include <servprov.h>
#include <shlguid.h>

namespace {

constexpr wchar_t kThisPcPath[] = L"::ThisPC";

void EnsureBufferedList(HWND list)
{
    // Refresh/sort invalidates several rows together. Commit the complete native
    // custom-draw frame instead of exposing its erase/icon/text intermediate steps.
    if(list && !(ListView_GetExtendedListViewStyle(list)&LVS_EX_DOUBLEBUFFER))
        ListView_SetExtendedListViewStyleEx(list,LVS_EX_DOUBLEBUFFER,LVS_EX_DOUBLEBUFFER);
}


std::wstring TrimFolderTerminator(std::wstring path)
{
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    return path;
}

class NameFolderFilter final : public IFolderFilter
{
public:
    NameFolderFilter(std::wstring text, bool showHidden) : m_text(std::move(text)), m_showHidden(showHidden) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IFolderFilter) {
            *object = static_cast<IFolderFilter*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refs = --m_refs;
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE ShouldShow(IShellFolder* folder, PCIDLIST_ABSOLUTE,
        PCUITEMID_CHILD item) override
    {
        if (!m_showHidden && folder) {
            SFGAOF attributes = SFGAO_HIDDEN;
            if (SUCCEEDED(folder->GetAttributesOf(1, &item, &attributes)) && (attributes & SFGAO_HIDDEN))
                return S_FALSE;
        }
        if (m_text.empty()) return S_OK;
        STRRET name = {};
        if (!folder || FAILED(folder->GetDisplayNameOf(item, SHGDN_INFOLDER | SHGDN_NORMAL, &name)))
            return S_FALSE;
        wchar_t buffer[MAX_PATH] = {};
        if (FAILED(::StrRetToBufW(&name, item, buffer, _countof(buffer)))
            || buffer[0] == L'\0')
            return S_FALSE;
        return ::StrStrIW(buffer, m_text.c_str()) ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE GetEnumFlags(IShellFolder*, PCIDLIST_ABSOLUTE, HWND* hwnd,
        DWORD* flags) override
    {
        if (hwnd) *hwnd = nullptr;
        if (flags) {
            *flags |= SHCONTF_FOLDERS | SHCONTF_NONFOLDERS;
            const DWORD hidden = SHCONTF_INCLUDEHIDDEN | SHCONTF_INCLUDESUPERHIDDEN;
            if (m_showHidden) *flags |= hidden;
            else *flags &= ~hidden;
        }
        return S_OK;
    }

private:
    volatile LONG m_refs = 1;
    std::wstring m_text;
    bool m_showHidden;
};

std::wstring ShellPathFromPidl(PCIDLIST_ABSOLUTE pidl)
{
    if (!pidl)
        return {};
    PWSTR value = nullptr;
    if (SUCCEEDED(::SHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, &value)) && value) {
        std::wstring path = TrimFolderTerminator(value);
        ::CoTaskMemFree(value);
        return path;
    }
    if (SUCCEEDED(::SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, &value)) && value) {
        std::wstring path(value);
        ::CoTaskMemFree(value);
        if (_wcsicmp(path.c_str(), L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}") == 0)
            return kThisPcPath;
        return path;
    }
    return {};
}

} // namespace

class ShellBrowserHost::EventSink final : public IExplorerBrowserEvents, public IDispatch,
    public IServiceProvider, public ICommDlgBrowser2
{
public:
    EventSink(ShellBrowserHost* owner, HWND parent, UINT navigationMessage, UINT selectionMessage)
        : m_owner(owner), m_parent(parent), m_navigationMessage(navigationMessage),
          m_selectionMessage(selectionMessage) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IExplorerBrowserEvents) {
            *object = static_cast<IExplorerBrowserEvents*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IDispatch || riid == DIID_DShellFolderViewEvents) {
            *object = static_cast<IDispatch*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IServiceProvider)
            *object = static_cast<IServiceProvider*>(this);
        else if (riid == IID_ICommDlgBrowser || riid == IID_ICommDlgBrowser2)
            *object = static_cast<ICommDlgBrowser2*>(this);
        if (*object) { AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE QueryService(REFGUID service, REFIID iid, void** object) override
    {
        if (service == SID_SExplorerBrowserFrame) return QueryInterface(iid, object);
        if (object) *object = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultCommand(IShellView* view) override {
        return m_owner ? m_owner->DefaultCommand(view) : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE OnStateChange(IShellView*, ULONG) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE IncludeObject(IShellView* shellView, PCUITEMID_CHILD item) override
    {
        if (!m_owner || !shellView) return S_OK;
        IFolderView* view = nullptr;
        IShellFolder* folder = nullptr;
        if (SUCCEEDED(shellView->QueryInterface(IID_PPV_ARGS(&view)))) {
            view->GetFolder(IID_PPV_ARGS(&folder));
            view->Release();
        }
        if (!folder) return S_OK;
        NameFolderFilter filter(m_owner->m_filterText, m_owner->m_showHidden);
        const HRESULT result = filter.ShouldShow(folder, nullptr, item);
        folder->Release();
        return result;
    }
    HRESULT STDMETHODCALLTYPE Notify(IShellView*, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDefaultMenuText(IShellView*, LPWSTR, int) override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE GetViewFlags(DWORD* flags) override
    {
        if (!flags) return E_POINTER;
        // Enumerate hidden objects independently of Explorer's global setting;
        // IncludeObject applies FastFile's own menu preference using NameFolderFilter.
        // CDB2GVF_NOSELECTVERB must stay: hosting ICommDlgBrowser puts DefView in
        // common-dialog mode, where it would otherwise *add* a "选择" default verb to
        // every item menu. With the flag the item menus stay identical to Explorer's.
        *flags = CDB2GVF_SHOWALLFILES | CDB2GVF_NOSELECTVERB;
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refs = --m_refs;
        if (refs == 0)
            delete this;
        return refs;
    }

    HRESULT STDMETHODCALLTYPE OnNavigationPending(PCIDLIST_ABSOLUTE folder) override
    {
        const auto path=ShellPathFromPidl(folder);
        if(m_owner && !path.empty()) {
            if(!m_owner->m_pendingNavigation.empty()
                && _wcsicmp(path.c_str(),m_owner->m_pendingNavigation.c_str())!=0) {
                m_owner->TraceNavigation(L"stale-pending");
                return S_OK;
            }
            if(!m_owner->m_navigationQueued)m_owner->m_lastNavigation=path;
            m_owner->m_pendingNavigation=path;
            m_owner->m_navigationFailed=false;
            if(!m_owner->m_navigationDeadline)m_owner->m_navigationDeadline=GetTickCount64()+15000;
            m_owner->TraceNavigation(L"pending");
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnViewCreated(IShellView* view) override
    {
        if (m_owner) {
            m_owner->CancelThumbRequests();
            m_owner->AttachViewFilter(view);
            IFolderView2* folderView=nullptr;
            if(SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&folderView)))) {
                m_owner->ApplyViewMode(folderView);
                folderView->Release();
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnNavigationComplete(PCIDLIST_ABSOLUTE folder) override
    {
        std::wstring shellPath = ShellPathFromPidl(folder);
        if (!m_owner || !m_owner->FinishNavigation(shellPath,false))return S_OK;
        if (m_owner) m_owner->SetVisible(m_owner->m_visible);
        auto* path = new (std::nothrow) std::wstring(std::move(shellPath));
        if (!path || !::PostMessageW(m_parent, m_navigationMessage, 0,
                reinterpret_cast<LPARAM>(path))) {
            delete path;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnNavigationFailed(PCIDLIST_ABSOLUTE folder) override
    {
        const auto path=ShellPathFromPidl(folder);
        if(!m_owner || !m_owner->FinishNavigation(path,true))return S_OK;
        auto* failed=new (std::nothrow) std::wstring(path);
        if(!failed || !PostMessageW(m_parent,m_navigationMessage,1,reinterpret_cast<LPARAM>(failed)))delete failed;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override { if (count) *count = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override {
        if (id == DISPID_SELECTIONCHANGED) SelectionChanged();
        return S_OK;
    }

    void SelectionChanged()
    {
        ::PostMessageW(m_parent, m_selectionMessage, 0, 0);
    }

private:
    volatile LONG m_refs = 1;
    ShellBrowserHost* m_owner = nullptr;
    HWND m_parent = nullptr;
    UINT m_navigationMessage = 0;
    UINT m_selectionMessage = 0;
};

ShellBrowserHost::ShellBrowserHost() = default;
ShellBrowserHost::~ShellBrowserHost() { Destroy(); }

bool ShellBrowserHost::Create(HWND parent, const RECT& bounds,
    UINT navigationMessage, UINT selectionMessage, UINT folderOpenMessage)
{
    if (m_browser || !parent)
        return false;
    m_parent = parent;
    m_uiThread = GetCurrentThreadId();
    m_navigationMessage = navigationMessage;
    m_selectionMessage = selectionMessage;
    m_folderOpenMessage = folderOpenMessage;

    HRESULT hr = ::CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_browser));
    if (FAILED(hr) || !m_browser) {
        m_browser = nullptr;
        return false;
    }

    FOLDERSETTINGS settings = {};
    settings.ViewMode = FVM_DETAILS;
    settings.fFlags = FWF_AUTOARRANGE | FWF_NOWEBVIEW;
    // Keep the Shell view/verbs, but use its documented Win32 list-view renderer
    // so icon cells can be styled without replacing browsing or file operations.
    IFolderViewOptions* options = nullptr;
    if (SUCCEEDED(m_browser->QueryInterface(IID_PPV_ARGS(&options)))) {
        options->SetFolderViewOptions(FVO_VISTALAYOUT, FVO_VISTALAYOUT);
        options->Release();
    }
    m_events = new (std::nothrow) EventSink(this, parent, navigationMessage, selectionMessage);
    if (!m_events) { Destroy(); return false; }
    IObjectWithSite* site = nullptr;
    if (SUCCEEDED(m_browser->QueryInterface(IID_PPV_ARGS(&site)))) {
        site->SetSite(static_cast<IServiceProvider*>(m_events));
        site->Release();
    }
    hr = m_browser->Initialize(parent, &bounds, &settings);
    if (FAILED(hr)) {
        Destroy();
        return false;
    }
    // DuiLib owns the flat panel dividers; suppress the Shell host's inset frame.
    m_browser->SetOptions(EBO_NOBORDER);
    if (!m_events || FAILED(m_browser->Advise(m_events, &m_eventCookie))) {
        Destroy();
        return false;
    }
    return true;
}

void ShellBrowserHost::Destroy()
{
    RestoreListSpacing();
    if (m_systemSmallImages) {
        reinterpret_cast<IImageList*>(m_systemSmallImages)->Release();
        m_systemSmallImages = nullptr;
    }
    StopThumbWorker();
    ClearItemImages();
    if (m_listWindow) RemoveWindowSubclass(m_listWindow, ListSubclass, reinterpret_cast<UINT_PTR>(this));
    if (m_viewWindow) RemoveWindowSubclass(m_viewWindow, ViewSubclass, reinterpret_cast<UINT_PTR>(this));
    m_listWindow = m_viewWindow = nullptr;
    m_hotItem = -1;
    DetachSelectionEvents();
    if (m_filterSite) {
        m_filterSite->SetFilter(nullptr);
        m_filterSite->Release();
        m_filterSite = nullptr;
    }
    if (m_browser) {
        if (m_eventCookie)
            m_browser->Unadvise(m_eventCookie);
        m_eventCookie = 0;
        IObjectWithSite* site = nullptr;
        if (SUCCEEDED(m_browser->QueryInterface(IID_PPV_ARGS(&site)))) {
            site->SetSite(nullptr);
            site->Release();
        }
        m_browser->Destroy();
        m_browser->Release();
        m_browser = nullptr;
    }
    if (m_events) {
        m_events->Release();
        m_events = nullptr;
    }
    if (m_filter) {
        m_filter->Release();
        m_filter = nullptr;
    }
    m_parent = nullptr;
    m_lastNavigation.clear();
    m_pendingNavigation.clear();
    m_navigationFailed=false;
    m_navigationQueued=false;m_navigationDeadline=0;
    m_observedList=nullptr;m_observedItems=-2;m_observedVisible=m_observedRedraw=-1;
    m_filterText.clear();
    m_visible = true;
}

void ShellBrowserHost::SetBounds(const RECT& bounds)
{
    if (m_browser)
        m_browser->SetRect(nullptr, bounds);
    // SetRect can be a no-op when the outer rectangle is unchanged. A newly
    // created/cached DefView may still be zero-sized despite populated items.
    // Ask its existing container to run its normal layout once in that state.
    if(m_browser && bounds.right>bounds.left && bounds.bottom>bounds.top) {
        IShellView* view=nullptr;HWND root=nullptr;
        if(SUCCEEDED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))) && view) {
            view->GetWindow(&root);view->Release();
        }
        RECT inner{},outer{};const HWND container=root?GetParent(root):nullptr;
        if(root && container && IsChild(m_parent,root) && GetClientRect(root,&inner)
            && (inner.right<=0 || inner.bottom<=0) && GetClientRect(container,&outer)
            && outer.right>0 && outer.bottom>0) {
            SendMessageW(container,WM_SIZE,SIZE_RESTORED,MAKELPARAM(outer.right,outer.bottom));
            TraceNavigation(L"zero-size-relayout");
        }
    }
    ObserveViewState();
}

bool ShellBrowserHost::Navigate(const std::wstring& path, bool retryPending)
{
    if (!m_browser || path.empty())
        return false;
    const std::wstring target = _wcsicmp(path.c_str(), kThisPcPath) == 0
        ? std::wstring(kThisPcPath) : TrimFolderTerminator(path);
    if (IsAtPath(target))
        return true;
    if(!retryPending && !m_navigationFailed && !m_navigationQueued && _wcsicmp(target.c_str(),m_pendingNavigation.c_str())==0)
        return true; // coalesce an accepted request; this is not proof of completion
    m_lastNavigation=target;
    if(!m_pendingNavigation.empty()) {
        m_navigationQueued=true;
        if(!m_navigationDeadline)m_navigationDeadline=GetTickCount64()+15000;
        TraceNavigation(L"queued");
        return true;
    }
    // Drop queued thumbnails of the old folder; cached ones stay (keyed by item + size).
    CancelThumbRequests();

    // OnViewCreated may run inside BrowseToObject. Its grouping snapshot and
    // details-column setup must describe the destination, not the old folder.
    m_lastNavigation=target;
    m_pendingNavigation=target;
    m_navigationFailed=false;
    m_navigationQueued=false;
    if(!m_navigationDeadline)m_navigationDeadline=GetTickCount64()+15000;
    TraceNavigation(L"request");
    HRESULT hr = E_FAIL;
    if (_wcsicmp(target.c_str(), kThisPcPath) == 0) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        hr = ::SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &pidl);
        if (SUCCEEDED(hr) && pidl) {
            hr = m_browser->BrowseToIDList(pidl, SBSP_ABSOLUTE);
            ::CoTaskMemFree(pidl);
        }
    } else {
        IShellItem* item = nullptr;
        hr = ::SHCreateItemFromParsingName(target.c_str(), nullptr, IID_PPV_ARGS(&item));
        if (SUCCEEDED(hr) && item) {
            hr = m_browser->BrowseToObject(item, SBSP_ABSOLUTE);
            item->Release();
        }
    }
    if(hr==HRESULT_FROM_WIN32(ERROR_BUSY) || hr==E_PENDING) {
        m_pendingNavigation.clear();m_navigationQueued=true;
        TraceNavigation(L"busy-retry",hr);
        return true; // accepted into our queue; not a successful Shell navigation
    }
    if (FAILED(hr))FinishNavigation(target,true);
    TraceNavigation(L"submitted",hr);
    return SUCCEEDED(hr);
}

void ShellBrowserHost::PollNavigation()
{
    if(!m_browser || (!m_navigationQueued && m_pendingNavigation.empty()))return;
    if(m_navigationDeadline && GetTickCount64()>=m_navigationDeadline) {
        m_pendingNavigation.clear();m_navigationQueued=false;m_navigationDeadline=0;
        m_navigationFailed=true;
        TraceNavigation(L"timeout",HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        auto* path=new (std::nothrow) std::wstring(m_lastNavigation);
        if(!path || !PostMessageW(m_parent,m_navigationMessage,1,reinterpret_cast<LPARAM>(path)))delete path;
        return;
    }
    if(m_navigationQueued && m_pendingNavigation.empty())Navigate(m_lastNavigation,true);
}

void ShellBrowserHost::Refresh()
{
    ++m_counters.refreshes;
    if (!m_browser)
        return;
    if(!m_lastNavigation.empty() && !IsAtPath(m_lastNavigation)) {
        TraceNavigation(L"refresh-retry");
        Navigate(m_lastNavigation,true);
        return;
    }
    IShellView* view = nullptr;
    if (SUCCEEDED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))) && view) {
        EnsureBufferedList(m_listWindow);
        const HRESULT result=view->Refresh();
        view->Release();
        TraceNavigation(L"refresh",result);
    }
}

HRESULT ShellBrowserHost::DefaultCommand(IShellView* shellView,
    BOOL (WINAPI *execute)(SHELLEXECUTEINFOW*))
{
    if(!shellView)return S_FALSE;
    IFolderView2* view=nullptr;
    if(FAILED(shellView->QueryInterface(IID_PPV_ARGS(&view))))return S_FALSE;
    IShellItemArray* selection=nullptr;
    const HRESULT result=view->Items(SVGIO_SELECTION,IID_PPV_ARGS(&selection));
    view->Release();
    if(FAILED(result) || !selection)return S_FALSE;
    DWORD count=0;selection->GetCount(&count);
    std::vector<std::wstring> files,folders;
    for(DWORD i=0;i<count;++i) {
        IShellItem* item=nullptr;SFGAOF attributes=0;PWSTR path=nullptr;
        bool supported=false;
        if(SUCCEEDED(selection->GetItemAt(i,&item))) {
            supported=SUCCEEDED(item->GetAttributes(SFGAO_FOLDER,&attributes))
                && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path));
            if(supported && (attributes&SFGAO_FOLDER)) {
                const DWORD filesystemAttributes=GetFileAttributesW(path);
                // Archives can be Shell folders while remaining ordinary files.
                // Let the native view browse those virtual contents as before.
                if(filesystemAttributes!=INVALID_FILE_ATTRIBUTES && !(filesystemAttributes&FILE_ATTRIBUTE_DIRECTORY))supported=false;
            }
            if(supported) {
                if(attributes&SFGAO_FOLDER)folders.emplace_back(path);
                else files.emplace_back(path);
            }
            CoTaskMemFree(path);item->Release();
        }
        // Unsupported virtual namespaces retain their native Shell semantics.
        if(!supported) {selection->Release();return S_FALSE;}
    }
    selection->Release();
    if(files.empty() && folders.empty())return S_FALSE;
    if(!folders.empty() && !m_folderOpenMessage)return S_FALSE;
    // Queue navigation outside the Shell activation callback. Never invoke the
    // registered folder verb here: it may launch Explorer or another manager.
    for(size_t i=0;i<folders.size();++i) {
        auto* target=new(std::nothrow) std::wstring(folders[i]);
        if(target && !PostMessageW(m_parent,m_folderOpenMessage,i!=0,reinterpret_cast<LPARAM>(target)))delete target;
    }
    for(const auto& path:files)ShellPresentation::OpenDefaultFile(m_parent,path,execute);
    // Even a cancelled/error launch is handled; returning S_FALSE would make
    // the embedded view invoke a second command and show another chooser.
    return S_OK;
}

bool ShellBrowserHost::BeginRename()
{
    if (!m_browser)
        return false;
    IFolderView2* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))) || !view)
        return false;
    const HRESULT hr = view->DoRename();
    view->Release();
    return SUCCEEDED(hr);
}

HRESULT ShellBrowserHost::CreateBackgroundContextMenu(IContextMenu** menu) const
{
    if (!menu) return E_POINTER;
    *menu = nullptr;
    if (!m_browser) return E_UNEXPECTED;
    IShellView* view = nullptr;
    HRESULT hr = m_browser->GetCurrentView(IID_PPV_ARGS(&view));
    if (FAILED(hr) || !view) return FAILED(hr) ? hr : E_FAIL;
    hr = view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(menu));
    view->Release();
    return hr;
}

HRESULT ShellBrowserHost::CreateSelectionContextMenu(IContextMenu** menu) const
{
    if (!menu) return E_POINTER;
    *menu = nullptr;
    if (!m_browser) return E_UNEXPECTED;
    IShellView* view = nullptr;
    HRESULT hr = m_browser->GetCurrentView(IID_PPV_ARGS(&view));
    if (FAILED(hr) || !view) return FAILED(hr) ? hr : E_FAIL;
    hr = view->GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(menu));
    view->Release();
    return hr;
}

int ShellBrowserHost::SelectedCount() const
{
    if (!m_browser) return -1;
    IFolderView* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))) || !view) return -1;
    int count = 0;
    const HRESULT hr = view->ItemCount(SVGIO_SELECTION, &count);
    view->Release();
    return SUCCEEDED(hr) ? count : -1;
}

bool ShellBrowserHost::SiteContextMenu(IUnknown* menu) const
{
    if (!menu || !m_browser) return false;
    IServiceProvider* services = nullptr;
    IShellBrowser* browser = nullptr;
    // Only site the menu when the frame really answers for the Shell browser service.
    if (FAILED(m_browser->QueryInterface(IID_PPV_ARGS(&services))) || !services) return false;
    const HRESULT hr = services->QueryService(SID_SShellBrowser, IID_PPV_ARGS(&browser));
    if (browser) browser->Release();
    bool sited = false;
    if (SUCCEEDED(hr)) sited = SUCCEEDED(IUnknown_SetSite(menu, services));
    services->Release();
    return sited;
}

bool ShellBrowserHost::SelectAll()
{
    if (!m_browser) return false;
    IFolderView2* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view)))) return false;
    int count = 0;
    HRESULT result = view->ItemCount(SVGIO_ALLVIEW, &count);
    for (int i = 0; SUCCEEDED(result) && i < count; ++i)
        result = view->SelectItem(i, SVSI_SELECT | (i == 0 ? SVSI_DESELECTOTHERS : 0));
    view->Release();
    return SUCCEEDED(result);
}

bool ShellBrowserHost::ClearSelection()
{
    if (!m_browser) return false;
    IShellView* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view)))) return false;
    const HRESULT result = view->SelectItem(nullptr, SVSI_DESELECTOTHERS);
    view->Release();
    return SUCCEEDED(result);
}

bool ShellBrowserHost::Focus()
{
    if (!m_browser || !m_visible) return false;
    IShellView* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view)))) return false;
    view->UIActivate(SVUIA_ACTIVATE_FOCUS);
    HWND window = nullptr; view->GetWindow(&window); view->Release();
    if (window && !OwnsWindow(GetFocus())) SetFocus(window);
    return window != nullptr;
}

bool ShellBrowserHost::OwnsWindow(HWND window) const
{
    if (!window || !m_browser || !m_visible) return false;
    IShellView* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view)))) return false;
    HWND viewWindow = nullptr;
    view->GetWindow(&viewWindow);
    view->Release();
    return viewWindow && (window == viewWindow || IsChild(viewWindow, window));
}

HRESULT ShellBrowserHost::TranslateAccelerator(MSG* message)
{
    if (!message || !m_browser || !m_visible) return S_FALSE;
    IShellView* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view)))) return S_FALSE;
    HWND window = nullptr;
    view->GetWindow(&window);
    HRESULT result = S_FALSE;
    if (window && (message->hwnd == window || IsChild(window, message->hwnd))) {
        if(message->message==WM_KEYDOWN && message->wParam==VK_RETURN
            && !(GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_MENU)&0x8000)
            && !(GetKeyState(VK_SHIFT)&0x8000)) {
            wchar_t name[64]{};GetClassNameW(message->hwnd,name,_countof(name));
            if(_wcsicmp(name,L"Edit")!=0 && DefaultCommand(view)==S_OK) {
                view->Release();return S_OK;
            }
        }
        IInputObject* input = nullptr;
        if (SUCCEEDED(m_browser->QueryInterface(IID_PPV_ARGS(&input)))) {
            result = input->TranslateAcceleratorIO(message);
            input->Release();
        }
        if (result != S_OK) result = view->TranslateAccelerator(message);
    }
    view->Release();
    return result;
}

bool ShellBrowserHost::IsAtPath(const std::wstring& path) const
{
    return !m_navigationFailed && !m_navigationQueued && m_pendingNavigation.empty() && !path.empty()
        && _wcsicmp(TrimFolderTerminator(path).c_str(),m_lastNavigation.c_str())==0
        && _wcsicmp(TrimFolderTerminator(path).c_str(),ActualViewPath().c_str())==0;
}
bool ShellBrowserHost::IsNavigationCompleteAt(const std::wstring& path) const
{
    return IsAtPath(path);
}

std::wstring ShellBrowserHost::ActualViewPath() const
{
    if(!m_browser)return {};
    IFolderView* view=nullptr;IPersistFolder2* folder=nullptr;PIDLIST_ABSOLUTE location=nullptr;
    std::wstring actual;
    if(SUCCEEDED(m_browser->GetCurrentView(IID_PPV_ARGS(&view)))
        && SUCCEEDED(view->GetFolder(IID_PPV_ARGS(&folder)))
        && SUCCEEDED(folder->GetCurFolder(&location)))
        actual=ShellPathFromPidl(location);
    if(location)CoTaskMemFree(location);if(folder)folder->Release();if(view)view->Release();
    return actual;
}

bool ShellBrowserHost::FinishNavigation(const std::wstring& path,bool failed)
{
    // A slow callback for A must never commit/cancel the newer request for B.
    const auto& expected=m_pendingNavigation.empty()?m_lastNavigation:m_pendingNavigation;
    if(path.empty() || (!expected.empty() && _wcsicmp(path.c_str(),expected.c_str())!=0)) {
        TraceNavigation(failed?L"stale-failure":L"stale-complete");
        return false;
    }
    if(m_navigationQueued || (!m_lastNavigation.empty() && _wcsicmp(path.c_str(),m_lastNavigation.c_str())!=0)) {
        // This finishes the in-flight request, not the newer target. Submit the
        // latest target on the next timer tick, outside the Shell callback.
        m_pendingNavigation.clear();m_navigationQueued=true;m_navigationFailed=false;
        if(!m_navigationDeadline)m_navigationDeadline=GetTickCount64()+15000;
        TraceNavigation(failed?L"superseded-failure":L"superseded-complete");
        return false;
    }
    m_lastNavigation=path;
    m_pendingNavigation.clear();
    m_navigationFailed=failed;
    m_navigationQueued=false;m_navigationDeadline=0;
    TraceNavigation(failed?L"failed":L"complete",failed?E_FAIL:S_OK);
    return true;
}

void ShellBrowserHost::TraceNavigation(const wchar_t* event,HRESULT result) const
{
    // Bounded per-user log, also isolated by the tests' APPDATA. No file-content reads.
    wchar_t root[32768]{};
    std::wstring file;
    DWORD length=GetEnvironmentVariableW(L"FASTFILE_NAV_LOG",root,_countof(root));
    if(length && length<_countof(root))file=root;
    else {
        length=GetEnvironmentVariableW(L"APPDATA",root,_countof(root));
        if(!length || length>=_countof(root))return;
        file=std::wstring(root)+L"\\FastFile";CreateDirectoryW(file.c_str(),nullptr);
        file+=L"\\navigation.log";
    }
    int count=-1;IFolderView* view=nullptr;
    if(m_browser && SUCCEEDED(m_browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
        view->ItemCount(SVGIO_ALLVIEW,&count);view->Release();
    }
    RECT bounds{};if(m_listWindow)GetWindowRect(m_listWindow,&bounds);
    wchar_t values[512]{};SYSTEMTIME now{};GetLocalTime(&now);
    swprintf_s(values,L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tick=%llu event=%s hr=%08lX count=%d listCount=%d visible=%d redrawOff=%d rect=%ld,%ld,%ld,%ld ",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,
        GetCurrentProcessId(),GetTickCount64(),event,static_cast<DWORD>(result),count,
        m_listWindow?ListView_GetItemCount(m_listWindow):-1,
        m_listWindow?IsWindowVisible(m_listWindow):0,m_listWindow && GetPropW(m_listWindow,L"SysSetRedraw")!=nullptr,
        bounds.left,bounds.top,bounds.right,bounds.bottom);
    const std::wstring line=std::wstring(values)+L"target="+m_lastNavigation+L" pending="+m_pendingNavigation
        +L" actual="+ActualViewPath()+L"\r\n";
    const int bytes=WideCharToMultiByte(CP_UTF8,0,line.data(),int(line.size()),nullptr,0,nullptr,nullptr);
    if(bytes<=0)return;
    std::string utf8(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,line.data(),int(line.size()),utf8.data(),bytes,nullptr,nullptr);
    HANDLE log=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(log==INVALID_HANDLE_VALUE)return;
    LARGE_INTEGER size{},zero{};GetFileSizeEx(log,&size);
    if(size.QuadPart>2*1024*1024) {SetFilePointerEx(log,zero,nullptr,FILE_BEGIN);SetEndOfFile(log);}
    SetFilePointerEx(log,zero,nullptr,FILE_END);DWORD written=0;
    WriteFile(log,utf8.data(),DWORD(utf8.size()),&written,nullptr);CloseHandle(log);
}

void ShellBrowserHost::ObserveViewState()
{
    const int count=m_listWindow?ListView_GetItemCount(m_listWindow):-1;
    const int visible=m_listWindow?IsWindowVisible(m_listWindow):0;
    const int redraw=m_listWindow && GetPropW(m_listWindow,L"SysSetRedraw")!=nullptr;
    if(m_observedList==m_listWindow && m_observedItems==count && m_observedVisible==visible && m_observedRedraw==redraw)return;
    m_observedList=m_listWindow;m_observedItems=count;m_observedVisible=visible;m_observedRedraw=redraw;
    TraceNavigation(L"view-state");
}

bool ShellBrowserHost::SetVisible(bool visible)
{
    if (!m_browser)
        return false;
    IShellView* view = nullptr;
    HWND viewWindow = nullptr;
    if (SUCCEEDED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))) && view) {
        view->GetWindow(&viewWindow);
        view->Release();
    }
    if (viewWindow && IsChild(m_parent, viewWindow)) {
        // GetCurrentView supports IShellView, not every IOleWindow IID. Hide the
        // enclosing ExplorerBrowser child too: its HWND otherwise covers our
        // DuiLib search results even when the inner Shell view is hidden.
        HWND container = viewWindow;
        while (GetParent(container) && GetParent(container) != m_parent)
            container = GetParent(container);
        ::ShowWindow(container, visible ? SW_SHOWNA : SW_HIDE);
        if (visible && container != viewWindow) ::ShowWindow(viewWindow, SW_SHOWNA);
    }
    m_visible = visible;
    return true;
}

bool ShellBrowserHost::SetFilter(const std::wstring& text)
{
    m_filterText = text;
    if (!m_filterSite)
        return false;

    auto* next = new (std::nothrow) NameFolderFilter(text, m_showHidden);
    if (!next)
        return false;
    ++m_counters.filterSets;
    const HRESULT hr = m_filterSite->SetFilter(next);
    if (FAILED(hr)) {
        next->Release();
        return false;
    }
    if (m_filter)
        m_filter->Release();
    m_filter = next;
    return true;
}

bool ShellBrowserHost::SetShowHidden(bool show)
{
    if (m_showHidden == show && m_filter) return true;
    m_showHidden = show;
    return SetFilter(m_filterText);
}

void ShellBrowserHost::AttachViewFilter(IShellView* view)
{
    DetachSelectionEvents();
    IDispatch* background = nullptr;
    if (view && SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background)))) {
        IConnectionPointContainer* source = nullptr;
        if (SUCCEEDED(background->QueryInterface(IID_PPV_ARGS(&source)))) {
            if (SUCCEEDED(source->FindConnectionPoint(DIID_DShellFolderViewEvents, &m_selectionEvents)))
                m_selectionEvents->Advise(static_cast<IDispatch*>(m_events), &m_selectionCookie);
            source->Release();
        }
        background->Release();
    }
    if (m_filterSite) {
        m_filterSite->SetFilter(nullptr);
        m_filterSite->Release();
        m_filterSite = nullptr;
    }
    // ExplorerBrowser owns IFolderFilterSite; asking only the current Shell view
    // silently missed it, so the hidden-items menu never affected enumeration.
    if (m_browser)
        m_browser->QueryInterface(IID_PPV_ARGS(&m_filterSite));
    if (!m_filterSite && view) view->QueryInterface(IID_PPV_ARGS(&m_filterSite));
    if (m_filterSite)
        SetFilter(m_filterText);
}

void ShellBrowserHost::DetachSelectionEvents()
{
    if (m_selectionEvents) {
        if (m_selectionCookie) m_selectionEvents->Unadvise(m_selectionCookie);
        m_selectionEvents->Release();
        m_selectionEvents = nullptr;
    }
    m_selectionCookie = 0;
}

bool ShellBrowserHost::SetViewMode(FOLDERVIEWMODE mode, int iconSize)
{
    m_requestedMode=mode;
    m_requestedIconSize=iconSize;
    if (!m_browser)
        return false;
    IFolderView2* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))) || !view)
        return false;
    const bool applied=ApplyViewMode(view);
    view->Release();
    return applied;
}

bool ShellBrowserHost::ApplyViewMode(IFolderView2* view)
{
    ++m_counters.modeApplies;
    const auto mode=m_requestedMode;
    const int iconSize=m_requestedIconSize;
    FOLDERVIEWMODE currentMode=FVM_AUTO;int currentSize=0;
    view->GetViewModeAndIconSize(&currentMode,&currentSize);
    UINT dpi=GetDpiForWindow(m_parent);if(!dpi)dpi=96;
    const bool dpiChanged=dpi!=m_dpi;
    const int targetSize = (iconSize > 0) ? iconSize :
        (mode == FVM_TILE ? 48 :
         mode == FVM_CONTENT ? 32 :
         mode == FVM_SMALLICON ? 16 :
         (mode == FVM_DETAILS || mode == FVM_LIST) ? 16 : -1);
    const bool sizeChanged = (targetSize > 0 && currentSize != targetSize)
        || (iconSize > 0 && currentSize != iconSize);
    const bool changed=currentMode!=mode || sizeChanged;
    // A real change (mode, size, DPI) is batched: the list does not paint the
    // half-switched layout (old rows under new items) and repaints once at the end.
    HWND batch=nullptr;
    if ((changed || dpiChanged) && m_listWindow && IsWindow(m_listWindow) && IsWindowVisible(m_listWindow)) {
        IShellView* shell=nullptr;HWND root=nullptr;
        if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&shell)))) {shell->GetWindow(&root);shell->Release();}
        if (root && (root==m_listWindow || IsChild(root,m_listWindow))) {
            batch=m_listWindow;
            SendMessageW(batch,WM_SETREDRAW,FALSE,0);
            m_redrawBatch=batch;
            ++m_counters.redrawBatches;
        }
    }
    // List and Details share the 26-px spacer: keep it across List <-> Details and
    // only give the Shell its own small image list back for the other modes.
    const bool spacerMode=mode==FVM_LIST || mode==FVM_DETAILS;
    if(dpiChanged || (changed && !spacerMode))RestoreListSpacing();
    // Shell folder templates may retain their header in icon/tile modes. Let the
    // native view remove its own header and reclaim its top inset on each switch.
    const DWORD headerMask = FWF_NOCOLUMNHEADER | FWF_NOHEADERINALLVIEWS;
    const DWORD wantFlags = mode == FVM_DETAILS ? 0 : headerMask;
    DWORD currentFlags = 0;
    HRESULT flags = S_OK;
    if (changed || FAILED(view->GetCurrentFolderFlags(&currentFlags)) || (currentFlags & headerMask) != wantFlags)
        flags = view->SetCurrentFolderFlags(headerMask, wantFlags);
    if ((changed || dpiChanged) && m_customTileHeight && m_listWindow) {
        auto info=m_originalTileInfo;
        info.dwFlags&=LVTVIF_FIXEDSIZE;
        info.sizeTile.cx=MulDiv(info.sizeTile.cx,96,m_dpi);
        info.sizeTile.cy=MulDiv(info.sizeTile.cy,96,m_dpi);
        ListView_SetTileViewInfo(m_listWindow,&info);
        m_customTileHeight=false;
    }
    // Entering List / Details from another mode: give the list its 26-px spacer
    // before the switch, so the new layout is computed once with the final row height
    // (installing it afterwards re-measured every item a second time).
    if (changed && spacerMode && !dpiChanged && batch && !m_listSpacer) InstallListSpacer(batch);
    if (changed) ++m_counters.modeSets;
    const int applySize = (iconSize > 0) ? iconSize : ((targetSize > 0 && currentSize != targetSize) ? targetSize : iconSize);
    const HRESULT hr = changed ? view->SetViewModeAndIconSize(mode, applySize) : S_OK;
    m_dpi = GetDpiForWindow(m_parent);
    if (!m_dpi) m_dpi = 96;
    const int previousSlot = m_iconSlot;
    m_iconSlot = mode == FVM_ICON && (iconSize == MulDiv(128, m_dpi, 96)
        || iconSize == MulDiv(160, m_dpi, 96)) ? iconSize : 0;
    // A new slot only drops queued requests: thumbnails of other sizes stay cached,
    // so returning to a size draws from memory without any extraction.
    if (m_iconSlot != previousSlot) CancelThumbRequests();
    if (dpiChanged) m_badges.clear();
    if (dpiChanged) m_spacingList = nullptr;
    StyleNativeView(view);
    if (mode == FVM_DETAILS && _wcsicmp(m_lastNavigation.c_str(), kThisPcPath) != 0) {
        IColumnManager* columns = nullptr;
        if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&columns)))) {
            PROPERTYKEY keys[] = { PKEY_ItemNameDisplay, PKEY_DateModified,
                PKEY_ItemTypeText, PKEY_Size };
            PROPERTYKEY current[_countof(keys)] = {};
            UINT count = 0;
            const bool same = SUCCEEDED(columns->GetColumnCount(CM_ENUM_VISIBLE, &count))
                && count == _countof(keys)
                && SUCCEEDED(columns->GetColumns(CM_ENUM_VISIBLE, current, count))
                && std::equal(keys, keys + _countof(keys), current, [](const PROPERTYKEY& a, const PROPERTYKEY& b) {
                    return a.pid == b.pid && IsEqualGUID(a.fmtid, b.fmtid); });
            if (!same) {
                ++m_counters.columnSets;
                columns->SetColumns(keys, _countof(keys));
            }
            columns->Release();
        }
    }
    if (batch) {
        m_redrawBatch=nullptr;
        if (IsWindow(batch)) {
            SendMessageW(batch,WM_SETREDRAW,TRUE,0);
            if (mode == FVM_LIST) {
                SendMessageW(batch, LVM_SETCOLUMNWIDTH, (WPARAM)-1, MAKELPARAM(LVSCW_AUTOSIZE, 0));
            }
            ListView_RedrawItems(batch, 0, ListView_GetItemCount(batch) - 1);
            // Measured: an extra RDW_UPDATENOW here only moved the paint into the switch
            // call (longer sync, same busy time), so the repaint stays asynchronous.
            RedrawWindow(batch,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);
        }
        if (m_listWindow && m_listWindow!=batch) {
            if (mode == FVM_LIST) {
                SendMessageW(m_listWindow, LVM_SETCOLUMNWIDTH, (WPARAM)-1, MAKELPARAM(LVSCW_AUTOSIZE, 0));
            }
            InvalidateRect(m_listWindow,nullptr,TRUE);
        }
    } else if (m_listWindow && IsWindow(m_listWindow)) {
        if (mode == FVM_LIST) {
            SendMessageW(m_listWindow, LVM_SETCOLUMNWIDTH, (WPARAM)-1, MAKELPARAM(LVSCW_AUTOSIZE, 0));
        }
    }
    return SUCCEEDED(hr) && SUCCEEDED(flags);
}

void ShellBrowserHost::StyleNativeView(IFolderView2* view)
{
    ApplyGrouping(view,false);
    IShellView* shell = nullptr;
    HWND root = nullptr;
    if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&shell)))) {
        shell->GetWindow(&root); shell->Release();
    }
    HWND list = nullptr;
    if (root) EnumChildWindows(root, [](HWND child, LPARAM param) -> BOOL {
        wchar_t name[64]{}; GetClassNameW(child, name, _countof(name));
        if (wcscmp(name, WC_LISTVIEWW) != 0) return TRUE;
        *reinterpret_cast<HWND*>(param) = child; return FALSE;
    }, reinterpret_cast<LPARAM>(&list));
    for (HWND frame = list ? list : root; frame && frame != m_parent; frame = GetParent(frame)) {
        const LONG_PTR style = GetWindowLongPtrW(frame, GWL_STYLE);
        const LONG_PTR exStyle = GetWindowLongPtrW(frame, GWL_EXSTYLE);
        if (!(style & WS_BORDER) && !(exStyle & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE))) continue;
        SetWindowLongPtrW(frame, GWL_STYLE, style & ~WS_BORDER);
        SetWindowLongPtrW(frame, GWL_EXSTYLE, exStyle & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));
        SetWindowPos(frame, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    if (list != m_listWindow) {
        RestoreListSpacing();
        m_customTileHeight=false;
        if (m_listWindow) RemoveWindowSubclass(m_listWindow, ListSubclass, reinterpret_cast<UINT_PTR>(this));
        if (m_viewWindow) RemoveWindowSubclass(m_viewWindow, ViewSubclass, reinterpret_cast<UINT_PTR>(this));
        m_listWindow = list;
        m_viewWindow = list ? GetParent(list) : nullptr;
        if (list) {
            SetWindowSubclass(list, ListSubclass, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this));
            SetWindowSubclass(m_viewWindow, ViewSubclass, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this));
        }
    }
    EnsureBufferedList(list);
    if (list && m_iconSlot) {
        // The Vista Shell list scales LVM_SETICONSPACING internally. Its input
        // is a 96-DPI value; thumbnail requests and painting remain physical.
        // Re-applying an unchanged spacing still re-arranges every item, so skip it
        // while the list reports the spacing we last set (the Shell resets it on its
        // own mode switches, which this comparison detects).
        const int slot = MulDiv(m_iconSlot, 96, m_dpi);
        const DWORD current = static_cast<DWORD>(ListView_GetItemSpacing(list, FALSE));
        if (list != m_spacingList || slot != m_spacingSlot || current != m_appliedSpacing) {
            ++m_counters.iconSpacingSets;
            ListView_SetIconSpacing(list, slot + 16, slot + 36);
            m_spacingList = list; m_spacingSlot = slot;
            m_appliedSpacing = static_cast<DWORD>(ListView_GetItemSpacing(list, FALSE));
            if (m_redrawBatch != list) InvalidateRect(list, nullptr, FALSE);
        }
    }
    UINT mode=0;
    m_dpi=GetDpiForWindow(m_parent);if(!m_dpi)m_dpi=96;
    if (list && SUCCEEDED(view->GetCurrentViewMode(&mode)) && (mode==FVM_LIST || mode==FVM_DETAILS))
        InstallListSpacer(list);
    if (list && SUCCEEDED(view->GetCurrentViewMode(&mode)) && mode==FVM_TILE
        && _wcsicmp(m_lastNavigation.c_str(),kThisPcPath)==0 && !m_customTileHeight) {
        m_originalTileInfo={};m_originalTileInfo.cbSize=sizeof(LVTILEVIEWINFO);
        m_originalTileInfo.dwMask=LVTVIM_TILESIZE;
        if (ListView_GetTileViewInfo(list,&m_originalTileInfo)) {
            auto info=m_originalTileInfo;
            RECT bounds{};
            if(ListView_GetItemRect(list,0,&bounds,LVIR_BOUNDS) && bounds.right>bounds.left) {
                info.sizeTile.cx=bounds.right-bounds.left;
                m_originalTileInfo.sizeTile.cx=info.sizeTile.cx;
            }
            // Shell's list wrapper scales tile inputs from 96-DPI units too.
            info.sizeTile.cx=MulDiv(info.sizeTile.cx,96,m_dpi);
            info.dwFlags=LVTVIF_FIXEDSIZE;
            info.sizeTile.cy=64;
            m_customTileHeight=ListView_SetTileViewInfo(list,&info)!=FALSE;
        }
    }
}

HIMAGELIST ShellBrowserHost::GetSystemSmallImageList()
{
    if (!m_systemSmallImages) {
        SHFILEINFOW sfi{};
        m_systemSmallImages = reinterpret_cast<HIMAGELIST>(
            SHGetFileInfoW(L"", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
    }
    return m_systemSmallImages;
}

HIMAGELIST ShellBrowserHost::GetSmallImageList()
{
    HIMAGELIST sys = GetSystemSmallImageList();
    if (m_shellSmallImages && m_shellSmallImages != sys && ImageList_GetImageCount(m_shellSmallImages) > 0)
        return m_shellSmallImages;
    return sys;
}

int ShellBrowserHost::ResolveItemIcon(int index, HIMAGELIST* outIml)
{
    if (outIml) *outIml = nullptr;
    HIMAGELIST sys = GetSystemSmallImageList();
    HIMAGELIST shell = (m_shellSmallImages && m_shellSmallImages != sys && ImageList_GetImageCount(m_shellSmallImages) > 0) ? m_shellSmallImages : nullptr;
    HIMAGELIST iml = shell ? shell : sys;

    LVITEMW item{}; item.mask = LVIF_IMAGE; item.iItem = index;
    if (ListView_GetItem(m_listWindow, &item) && item.iImage >= 0) {
        if (iml && item.iImage < ImageList_GetImageCount(iml)) {
            if (outIml) *outIml = iml;
            return item.iImage;
        }
    }
    if (m_viewWindow) {
        NMLVDISPINFO di{};
        di.hdr.hwndFrom = m_listWindow;
        di.hdr.idFrom = GetDlgCtrlID(m_listWindow);
        di.hdr.code = LVN_GETDISPINFO;
        di.item.mask = LVIF_IMAGE;
        di.item.iItem = index;
        di.item.iSubItem = 0;
        SendMessageW(m_viewWindow, WM_NOTIFY, di.hdr.idFrom, reinterpret_cast<LPARAM>(&di));
        if (iml && di.item.iImage >= 0 && di.item.iImage < ImageList_GetImageCount(iml)) {
            if (outIml) *outIml = iml;
            return di.item.iImage;
        }
    }
    if (m_browser) {
        IFolderView2* fv = nullptr;
        if (SUCCEEDED(m_browser->GetCurrentView(IID_PPV_ARGS(&fv))) && fv) {
            IShellItem* si = nullptr;
            if (SUCCEEDED(fv->GetItem(index, IID_PPV_ARGS(&si))) && si) {
                int resolved = -1;
                PIDLIST_ABSOLUTE pidl = nullptr;
                if (SUCCEEDED(SHGetIDListFromObject(si, &pidl)) && pidl) {
                    SHFILEINFO sfi{};
                    if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &sfi, sizeof(sfi),
                        SHGFI_PIDL | SHGFI_SYSICONINDEX | SHGFI_SMALLICON)) {
                        resolved = sfi.iIcon;
                    }
                    CoTaskMemFree(pidl);
                }
                if (resolved < 0) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(si->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                        SHFILEINFO sfi{};
                        if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi),
                            SHGFI_SYSICONINDEX | SHGFI_SMALLICON)) {
                            resolved = sfi.iIcon;
                        }
                        CoTaskMemFree(path);
                    }
                }
                if (resolved < 0) {
                    SFGAOF attrs = 0;
                    si->GetAttributes(SFGAO_FOLDER, &attrs);
                    SHFILEINFO sfi{};
                    if (attrs & SFGAO_FOLDER) {
                        if (SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
                            SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES | SHGFI_SMALLICON))
                            resolved = sfi.iIcon;
                    } else {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(si->GetDisplayName(SIGDN_NORMALDISPLAY, &path)) && path) {
                            if (SHGetFileInfoW(path, FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                                SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES | SHGFI_SMALLICON))
                                resolved = sfi.iIcon;
                            CoTaskMemFree(path);
                        }
                    }
                }
                si->Release();
                fv->Release();
                if (resolved >= 0 && sys) {
                    if (outIml) *outIml = sys;
                    return resolved;
                }
            } else {
                fv->Release();
            }
        }
    }
    return -1;
}

bool ShellBrowserHost::InstallListSpacer(HWND list)
{
    if (m_listSpacer) return true;
    HIMAGELIST images = ListView_GetImageList(list, LVSIL_SMALL);
    int w = 0, h = 0;
    HIMAGELIST sys = GetSystemSmallImageList();
    if (images && images != sys && images != m_listSpacer && ImageList_GetIconSize(images, &w, &h) && ImageList_GetImageCount(images) > 0) {
        m_shellSmallImages = images;
    } else {
        m_shellSmallImages = nullptr;
        w = 16; h = 16;
    }
    m_listSpacer=ImageList_Create(MulDiv(16,m_dpi,96),26,ILC_COLOR32,1,1);
    if(!m_listSpacer) return false;
    ++m_counters.spacerSwaps;
    ImageList_SetImageCount(m_listSpacer,1);
    SetWindowLongPtrW(list,GWL_STYLE,GetWindowLongPtrW(list,GWL_STYLE)|LVS_SHAREIMAGELISTS);
    ListView_SetImageList(list,m_listSpacer,LVSIL_SMALL);
    return true;
}

void ShellBrowserHost::RestoreListSpacing()
{
    if(!m_listSpacer)return;
    ++m_counters.spacerSwaps;
    const HIMAGELIST spacer=m_listSpacer;m_listSpacer=nullptr;
    HIMAGELIST restoreIml = m_shellSmallImages ? m_shellSmallImages : GetSmallImageList();
    if(m_listWindow && IsWindow(m_listWindow) && restoreIml)
        ListView_SetImageList(m_listWindow,restoreIml,LVSIL_SMALL);
    ImageList_Destroy(spacer);m_shellSmallImages=nullptr;
}

bool ShellBrowserHost::GetItemPath(int itemIndex, std::wstring& outPath) const
{
    outPath.clear();
    if (!m_browser) return false;
    IFolderView2* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))) || !view) return false;
    IShellItem* shellItem = nullptr;
    HRESULT hr = view->GetItem(itemIndex, IID_PPV_ARGS(&shellItem));
    view->Release();
    if (FAILED(hr) || !shellItem) return false;
    PWSTR path = nullptr;
    if (SUCCEEDED(shellItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &path)) && path) {
        outPath = path;
        CoTaskMemFree(path);
    }
    shellItem->Release();
    return !outPath.empty();
}

LRESULT ShellBrowserHost::DrawListIcon(NMLVCUSTOMDRAW* draw)
{
    if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
    if(draw->dwItemType!=LVCDI_ITEM)return CDRF_DODEFAULT;
    const int itemIndex = int(draw->nmcd.dwItemSpec);
    const bool isDetails = (m_requestedMode == FVM_DETAILS)
        || ((GetWindowLongPtrW(m_listWindow, GWL_STYLE) & LVS_TYPEMASK) == LVS_REPORT);

    if (draw->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT) {
        if (!isDetails) return CDRF_DODEFAULT;
        LVITEMW item{}; item.mask = LVIF_IMAGE | LVIF_STATE; item.iItem = itemIndex;
        item.stateMask = LVIS_OVERLAYMASK | LVIS_CUT;
        RECT icon{}, row{}; int w = 0, h = 0;
        HIMAGELIST iml = nullptr;
        const int img = ResolveItemIcon(itemIndex, &iml);
        if (img >= 0 && iml
            && ListView_GetItem(m_listWindow, &item)
            && ListView_GetItemRect(m_listWindow, itemIndex, &icon, LVIR_ICON)
            && ListView_GetItemRect(m_listWindow, itemIndex, &row, LVIR_BOUNDS)
            && ImageList_GetIconSize(iml, &w, &h)) {
            const int iconY = row.top + (row.bottom - row.top - h) / 2;
            ImageList_DrawEx(iml, img, draw->nmcd.hdc, icon.left,
                iconY, w, h, CLR_NONE, CLR_NONE,
                ILD_TRANSPARENT | (item.state & LVIS_OVERLAYMASK) | ((item.state & LVIS_CUT) ? ILD_BLEND50 : 0));
            std::wstring itemPath;
            if (GetItemPath(itemIndex, itemPath)) {
                FileTagInfo tag = FileTagManager::Instance().GetTag(itemPath);
                if (tag.color != FileTagColor::None) {
                    const int radius = MulDiv(3, m_dpi, 96) + 1;
                    FileTagManager::DrawTagDot(draw->nmcd.hdc, icon.left + w - radius, iconY + h - radius, radius, FileTagManager::GetColorRef(tag.color));
                }
                if (tag.starred) {
                    const int starR = MulDiv(4, m_dpi, 96) + 1;
                    FileTagManager::DrawStar(draw->nmcd.hdc, icon.left + starR, iconY + starR, starR);
                }
            }
        }
        return CDRF_DODEFAULT;
    }
    if (draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    // Details keeps native text, columns, focus and selection painting.
    // Draw only its real icon in CDDS_ITEMPOSTPAINT.
    if (isDetails)
        return CDRF_NOTIFYPOSTPAINT;

    // List mode: custom draw item row to accommodate custom 26-px row height.
    LVITEMW item{}; item.mask = LVIF_IMAGE | LVIF_STATE; item.iItem = itemIndex;
    item.stateMask = LVIS_OVERLAYMASK | LVIS_CUT | LVIS_SELECTED | LVIS_FOCUSED;
    RECT icon{}, row{}; int w = 0, h = 0;
    HIMAGELIST iml = nullptr;
    const int img = ResolveItemIcon(itemIndex, &iml);
    if (ListView_GetItem(m_listWindow, &item)
        && ListView_GetItemRect(m_listWindow, itemIndex, &icon, LVIR_ICON)
        && ListView_GetItemRect(m_listWindow, itemIndex, &row, LVIR_BOUNDS)) {
        const bool selected = (item.state & LVIS_SELECTED) != 0;
        HBRUSH fill = CreateSolidBrush(selected ? RGB(0xE5, 0xF1, 0xFB)
            : (draw->nmcd.uItemState & CDIS_HOT) ? RGB(0xF5, 0xF5, 0xF5) : RGB(255, 255, 255));
        FillRect(draw->nmcd.hdc, &row, fill); DeleteObject(fill);
        int iconLeft = icon.left;
        if (icon.right <= icon.left || iconLeft < row.left) {
            iconLeft = row.left + MulDiv(4, m_dpi, 96);
        }
        if (img >= 0 && iml && ImageList_GetIconSize(iml, &w, &h)) {
            const int iconY = row.top + (row.bottom - row.top - h) / 2;
            ImageList_DrawEx(iml, img, draw->nmcd.hdc, iconLeft,
                iconY, w, h, CLR_NONE, CLR_NONE,
                ILD_TRANSPARENT | (item.state & LVIS_OVERLAYMASK) | ((item.state & LVIS_CUT) ? ILD_BLEND50 : 0));
            std::wstring itemPath;
            if (GetItemPath(itemIndex, itemPath)) {
                FileTagInfo tag = FileTagManager::Instance().GetTag(itemPath);
                if (tag.color != FileTagColor::None) {
                    const int radius = MulDiv(3, m_dpi, 96) + 1;
                    FileTagManager::DrawTagDot(draw->nmcd.hdc, iconLeft + w - radius, iconY + h - radius, radius, FileTagManager::GetColorRef(tag.color));
                }
                if (tag.starred) {
                    const int starR = MulDiv(4, m_dpi, 96) + 1;
                    FileTagManager::DrawStar(draw->nmcd.hdc, iconLeft + starR, iconY + starR, starR);
                }
            }
        }
        wchar_t title[32768]{}; ListView_GetItemText(m_listWindow, itemIndex, 0, title, _countof(title));
        const int saved = SaveDC(draw->nmcd.hdc);
        SelectObject(draw->nmcd.hdc, reinterpret_cast<HFONT>(SendMessageW(m_listWindow, WM_GETFONT, 0, 0)));
        SetBkMode(draw->nmcd.hdc, TRANSPARENT); SetTextColor(draw->nmcd.hdc, RGB(0x1A, 0x1A, 0x1A));
        const int iconRight = (w > 0) ? (iconLeft + w) : (iconLeft + MulDiv(16, m_dpi, 96));
        RECT text{iconRight + MulDiv(4, m_dpi, 96), row.top, row.right - MulDiv(4, m_dpi, 96), row.bottom};
        DrawTextW(draw->nmcd.hdc, title, -1, &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        if ((item.state & LVIS_FOCUSED) && OwnsWindow(GetFocus())) DrawFocusRect(draw->nmcd.hdc, &row);
        RestoreDC(draw->nmcd.hdc, saved);
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}

HBITMAP ShellBrowserHost::NormalizeImageAlpha(HBITMAP bitmap)
{
    BITMAP source{};
    if (!bitmap || !GetObjectW(bitmap,sizeof(source),&source) || source.bmBitsPixel!=32) return bitmap;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=source.bmWidth;info.bmiHeader.biHeight=-source.bmHeight;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    std::vector<DWORD> pixels(size_t(source.bmWidth)*source.bmHeight);
    HDC dc=GetDC(nullptr);
    if (GetDIBits(dc,bitmap,0,source.bmHeight,pixels.data(),&info,DIB_RGB_COLORS)!=source.bmHeight) {
        ReleaseDC(nullptr,dc);return bitmap;
    }
    bool straight=false;
    for(DWORD p:pixels) {
        const DWORD a=p>>24;
        if(a && ((p&255)>a || ((p>>8)&255)>a || ((p>>16)&255)>a)) { straight=true;break; }
    }
    for(DWORD& p:pixels) {
        const DWORD a=p>>24;
        if(!a) p=0;
        else if(straight && a<255) p=(a<<24)
            | (((((p>>16)&255)*a+127)/255)<<16)
            | (((((p>>8)&255)*a+127)/255)<<8) | (((p&255)*a+127)/255);
    }
    void* bits=nullptr;
    HBITMAP normalized=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    ReleaseDC(nullptr,dc);
    if (!normalized) return bitmap;
    memcpy(bits,pixels.data(),pixels.size()*sizeof(DWORD));
    DeleteObject(bitmap);return normalized;
}

void ShellBrowserHost::ClearItemImages()
{
    CancelThumbRequests();
    for(const auto& icon:m_associatedIcons)if(icon.second)DestroyIcon(icon.second);
    m_associatedIcons.clear();
    m_badges.clear();
    for (const auto& icon : m_placeholderIcons) if (icon.second) DestroyIcon(icon.second);
    m_placeholderIcons.clear();
    for (const auto& entry : m_thumbLru) if (entry.bitmap) DeleteObject(entry.bitmap);
    m_thumbLru.clear();
    m_thumbIndex.clear();
    m_thumbBytes = 0;
}

void ShellBrowserHost::SetThumbnailCacheLimits(size_t maxBytes, size_t maxEntries)
{
    m_thumbMaxBytes = maxBytes;
    m_thumbMaxEntries = maxEntries ? maxEntries : 1;
    TrimThumbs();
}

bool ShellBrowserHost::LookupThumb(const std::wstring& key, HBITMAP& bitmap)
{
    const auto found = m_thumbIndex.find(key);
    if (found == m_thumbIndex.end()) return false;
    if (found->second != m_thumbLru.begin()) m_thumbLru.splice(m_thumbLru.begin(), m_thumbLru, found->second);
    bitmap = found->second->bitmap;
    return true;
}

void ShellBrowserHost::StoreThumb(const std::wstring& key, HBITMAP bitmap)
{
    size_t bytes = 64;
    BITMAP info{};
    if (bitmap && GetObjectW(bitmap, sizeof(info), &info)) bytes += size_t(info.bmWidth) * size_t(info.bmHeight) * 4;
    const auto found = m_thumbIndex.find(key);
    if (found != m_thumbIndex.end()) {
        if (found->second->bitmap && found->second->bitmap != bitmap) DeleteObject(found->second->bitmap);
        m_thumbBytes -= found->second->bytes;
        m_thumbLru.erase(found->second);
        m_thumbIndex.erase(found);
    }
    m_thumbLru.push_front({key, bitmap, bytes});
    m_thumbIndex[key] = m_thumbLru.begin();
    m_thumbBytes += bytes;
    TrimThumbs();
}

void ShellBrowserHost::TrimThumbs()
{
    // Least recently drawn first; the newest entry always stays.
    while (m_thumbLru.size() > 1 && (m_thumbBytes > m_thumbMaxBytes || m_thumbLru.size() > m_thumbMaxEntries)) {
        const ThumbEntry& last = m_thumbLru.back();
        if (last.bitmap) DeleteObject(last.bitmap);
        m_thumbBytes -= last.bytes;
        m_thumbIndex.erase(last.key);
        m_thumbLru.pop_back();
        ++m_counters.thumbEvictions;
    }
}

std::wstring ShellBrowserHost::ThumbKey(IShellItem* item, const std::wstring& path, int size)
{
    // Identity + requested size + the size / modified time the view enumerated, so an
    // edited file (re-enumerated by the view) gets a fresh thumbnail. Fast properties
    // come from the item id itself: no file or property-handler access on paint.
    ULONGLONG bytes = 0, written = 0;
    IShellItem2* item2 = nullptr;
    if (item && SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&item2)))) {
        IPropertyStore* store = nullptr;
        if (SUCCEEDED(item2->GetPropertyStore(GPS_FASTPROPERTIESONLY, IID_PPV_ARGS(&store)))) {
            PROPVARIANT value; PropVariantInit(&value);
            if (SUCCEEDED(store->GetValue(PKEY_Size, &value)) && value.vt == VT_UI8) bytes = value.uhVal.QuadPart;
            PropVariantClear(&value);
            if (SUCCEEDED(store->GetValue(PKEY_DateModified, &value)) && value.vt == VT_FILETIME)
                written = (ULONGLONG(value.filetime.dwHighDateTime) << 32) | value.filetime.dwLowDateTime;
            PropVariantClear(&value);
            store->Release();
        }
        item2->Release();
    }
    wchar_t tail[80]{};
    swprintf_s(tail, L"|%d|%llx|%llx", size, bytes, written);
    return path + tail;
}

HBITMAP ShellBrowserHost::ExtractThumb(PCIDLIST_ABSOLUTE pidl, int size)
{
    if (GetCurrentThreadId() == m_uiThread) ++m_counters.syncExtractions;
    IShellItemImageFactory* factory = nullptr;
    HBITMAP bitmap = nullptr;
    if (pidl && SUCCEEDED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&factory)))) {
        const SIZE request{size, size};
        SFGAOF attrs = 0;
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&item))) && item) {
            item->GetAttributes(SFGAO_FOLDER, &attrs);
            item->Release();
        }
        if (attrs & SFGAO_FOLDER) {
            // Folders use clean icons to prevent Shell composite thumbnail black borders
            if (FAILED(factory->GetImage(request, SIIGBF_ICONONLY, &bitmap)))
                factory->GetImage(request, SIIGBF_THUMBNAILONLY, &bitmap);
        } else {
            // Ask Shell for the physical-size thumbnail, never a stretched 16px icon.
            if (FAILED(factory->GetImage(request, SIIGBF_THUMBNAILONLY, &bitmap)))
                factory->GetImage(request, SIIGBF_ICONONLY, &bitmap);
        }
        factory->Release();
    }
    return NormalizeImageAlpha(bitmap);
}

namespace {
constexpr UINT kThumbReadyMessage = WM_APP + 0x51;
constexpr wchar_t kThumbWindowClass[] = L"FastFileThumbnailSink";
}

LRESULT CALLBACK ShellBrowserHost::ThumbWindowProc(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == kThumbReadyMessage) {
        if (auto* host = reinterpret_cast<ShellBrowserHost*>(GetWindowLongPtrW(window, GWLP_USERDATA))) host->OnThumbsReady();
        return 0;
    }
    return DefWindowProcW(window, msg, wp, lp);
}

void ShellBrowserHost::StartThumbWorker()
{
    if (m_thumbThread.joinable()) return;
    if (!m_thumbWindow) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = ThumbWindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kThumbWindowClass;
        RegisterClassExW(&wc); // a second registration fails harmlessly
        m_thumbWindow = CreateWindowExW(0, kThumbWindowClass, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (!m_thumbWindow) return;
        SetWindowLongPtrW(m_thumbWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }
    if (!m_uiThread) m_uiThread = GetCurrentThreadId();
    {
        std::lock_guard<std::mutex> lock(m_thumbMutex);
        m_thumbStop = false;
    }
    m_thumbThread = std::thread(ThumbWorkerMain, this);
}

void ShellBrowserHost::StopThumbWorker()
{
    {
        std::lock_guard<std::mutex> lock(m_thumbMutex);
        m_thumbStop = true;
    }
    m_thumbCv.notify_all();
    if (m_thumbThread.joinable()) m_thumbThread.join();
    std::lock_guard<std::mutex> lock(m_thumbMutex);
    for (auto& request : m_thumbQueue) CoTaskMemFree(request.pidl);
    m_thumbQueue.clear();
    for (auto& result : m_thumbResults) if (result.bitmap) DeleteObject(result.bitmap);
    m_thumbResults.clear();
    m_thumbPosted = false;
    m_thumbPending.clear();
    if (m_thumbWindow) { DestroyWindow(m_thumbWindow); m_thumbWindow = nullptr; }
}

void ShellBrowserHost::CancelThumbRequests()
{
    // A new folder or slot: queued requests and in-flight results of the old one are
    // dropped (the memory cache itself is kept; its keys include item and size).
    ++m_thumbGeneration;
    m_thumbPending.clear();
    std::lock_guard<std::mutex> lock(m_thumbMutex);
    for (auto& request : m_thumbQueue) CoTaskMemFree(request.pidl);
    m_counters.staleDropped += int(m_thumbQueue.size());
    m_thumbQueue.clear();
}

void ShellBrowserHost::RequestThumb(IShellItem* item, const std::wstring& key, const std::wstring& path, int index)
{
    if (m_thumbPending.count(key)) {
        // Already queued: just mark it as painted (visible) again.
        ++m_counters.coalesced;
        std::lock_guard<std::mutex> lock(m_thumbMutex);
        for (auto& request : m_thumbQueue) if (request.key == key) { request.paintSeq = m_paintSeq; request.item = index; break; }
        return;
    }
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetIDListFromObject(item, &pidl)) || !pidl) return;
    StartThumbWorker();
    if (!m_thumbThread.joinable()) { CoTaskMemFree(pidl); return; }
    m_thumbPending.insert(key);
    ++m_counters.thumbRequests;
    {
        std::lock_guard<std::mutex> lock(m_thumbMutex);
        ThumbRequest request;
        request.key = key; request.path = path; request.pidl = pidl;
        request.size = m_iconSlot; request.item = index;
        request.generation = m_thumbGeneration.load(); request.paintSeq = m_paintSeq;
        m_thumbQueue.push_back(std::move(request));
    }
    m_thumbCv.notify_one();
}

void ShellBrowserHost::ThumbWorkerMain(ShellBrowserHost* self)
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        ThumbRequest request;
        {
            std::unique_lock<std::mutex> lock(self->m_thumbMutex);
            self->m_thumbCv.wait(lock, [self] { return self->m_thumbStop || !self->m_thumbQueue.empty(); });
            if (self->m_thumbStop) break;
            const UINT generation = self->m_thumbGeneration.load();
            auto best = self->m_thumbQueue.end();
            for (auto it = self->m_thumbQueue.begin(); it != self->m_thumbQueue.end();) {
                if (it->generation != generation) {
                    CoTaskMemFree(it->pidl); ++self->m_thumbDroppedQueued;
                    it = self->m_thumbQueue.erase(it);
                    best = self->m_thumbQueue.end();
                    continue;
                }
                ++it;
            }
            // Most recently painted first (what is on screen now); FIFO among equals.
            for (auto it = self->m_thumbQueue.begin(); it != self->m_thumbQueue.end(); ++it)
                if (best == self->m_thumbQueue.end() || it->paintSeq > best->paintSeq) best = it;
            if (best == self->m_thumbQueue.end()) continue;
            request = std::move(*best);
            self->m_thumbQueue.erase(best);
        }
        HBITMAP bitmap = self->ExtractThumb(request.pidl, request.size);
        CoTaskMemFree(request.pidl);
        bool post = false;
        {
            std::lock_guard<std::mutex> lock(self->m_thumbMutex);
            if (self->m_thumbStop) { if (bitmap) DeleteObject(bitmap); break; }
            self->m_thumbResults.push_back({request.key, request.path, bitmap, request.item, request.generation});
            if (!self->m_thumbPosted) self->m_thumbPosted = post = true;
        }
        if (post && !PostMessageW(self->m_thumbWindow, kThumbReadyMessage, 0, 0)) {
            std::lock_guard<std::mutex> lock(self->m_thumbMutex);
            self->m_thumbPosted = false;
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
}

void ShellBrowserHost::OnThumbsReady()
{
    std::vector<ThumbResult> results;
    {
        std::lock_guard<std::mutex> lock(m_thumbMutex);
        results.swap(m_thumbResults);
        m_thumbPosted = false;
        m_counters.staleDropped += m_thumbDroppedQueued;
        m_thumbDroppedQueued = 0;
    }
    const UINT generation = m_thumbGeneration.load();
    IFolderView2* view = nullptr;
    bool wholeList = false;
    for (auto& result : results) {
        ++m_counters.thumbExtractions;
        if (result.generation != generation) {
            ++m_counters.staleDropped;
            if (result.bitmap) DeleteObject(result.bitmap);
            continue;
        }
        m_thumbPending.erase(result.key);
        StoreThumb(result.key, result.bitmap);
        if (!m_listWindow || !m_iconSlot || wholeList) continue;
        // Repaint only that cell; the item may have moved (sort / refresh) meanwhile.
        bool same = false;
        if (!view && m_browser) m_browser->GetCurrentView(IID_PPV_ARGS(&view));
        IShellItem* item = nullptr;
        if (view && result.item >= 0 && SUCCEEDED(view->GetItem(result.item, IID_PPV_ARGS(&item)))) {
            PWSTR name = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &name))) {
                same = result.path == name; CoTaskMemFree(name);
            }
            item->Release();
        }
        if (same) { InvalidateIconCell(result.item); ++m_counters.itemInvalidations; }
        else { InvalidateRect(m_listWindow, nullptr, FALSE); wholeList = true; }
    }
    if (view) view->Release();
}

bool ShellBrowserHost::IconCell(int index, RECT& cell) const
{
    POINT point{};
    if (!m_listWindow || !m_iconSlot || !ListView_GetItemPosition(m_listWindow, index, &point)) return false;
    const int pad = MulDiv(8, m_dpi, 96);
    cell = {point.x, point.y, point.x + m_iconSlot + 2 * pad, point.y + m_iconSlot + MulDiv(36, m_dpi, 96)};
    return true;
}

void ShellBrowserHost::InvalidateIconCell(int index)
{
    RECT cell{};
    if (IconCell(index, cell)) InvalidateRect(m_listWindow, &cell, FALSE);
}

HICON ShellBrowserHost::PlaceholderIcon(int systemIndex)
{
    if (systemIndex < 0) return nullptr;
    const auto found = m_placeholderIcons.find(systemIndex);
    if (found != m_placeholderIcons.end()) return found->second;
    HICON icon = nullptr;
    IImageList* jumbo = nullptr;
    if (SUCCEEDED(SHGetImageList(SHIL_JUMBO, IID_PPV_ARGS(&jumbo)))) {
        jumbo->GetIcon(systemIndex, ILD_TRANSPARENT, &icon);
        jumbo->Release();
    }
    m_placeholderIcons[systemIndex] = icon;
    return icon;
}

void ShellBrowserHost::DrawPlaceholder(HDC dc, const std::wstring& path, const RECT& cell, bool folder)
{
    // While the thumbnail loads: the generic Shell icon of the item's type (folder / file
    // extension, resolved from the registry only, never from the file), 256-px source.
    int index = -1;
    if (folder) {
        if (m_folderIcon < 0) {
            SHSTOCKICONINFO stock{sizeof(stock)};
            if (SUCCEEDED(SHGetStockIconInfo(SIID_FOLDER, SHGSI_SYSICONINDEX, &stock))) m_folderIcon = stock.iSysImageIndex;
        }
        index = m_folderIcon;
    } else {
        index = Badge(path).systemIcon;
    }
    HICON icon = PlaceholderIcon(index);
    if (!icon) return;
    const int pad = MulDiv(8, m_dpi, 96);
    DrawIconEx(dc, cell.left + pad, cell.top + pad, icon, m_iconSlot, m_iconSlot, 0, nullptr, DI_NORMAL);

    FileTagInfo tag = FileTagManager::Instance().GetTag(path);
    if (tag.color != FileTagColor::None) {
        const int radius = MulDiv(6, m_dpi, 96);
        FileTagManager::DrawTagDot(dc, cell.left + pad + m_iconSlot - radius, cell.top + pad + radius, radius, FileTagManager::GetColorRef(tag.color));
    }
    if (tag.starred) {
        const int starR = MulDiv(7, m_dpi, 96);
        FileTagManager::DrawStar(dc, cell.left + pad + starR, cell.top + pad + starR, starR);
    }
}

HICON ShellBrowserHost::AssociatedAppIcon(const std::wstring& path)
{
    const std::wstring extension=PathFindExtensionW(path.c_str());
    const auto found=m_associatedIcons.find(extension);
    if(found!=m_associatedIcons.end())return found->second;
    HICON icon=nullptr;
    HKEY association=nullptr;
    if(SUCCEEDED(AssocQueryKeyW(ASSOCF_NONE,ASSOCKEY_CLASS,extension.c_str(),nullptr,&association))) {
        wchar_t overlay[2048]{};DWORD bytes=sizeof(overlay);
        const LSTATUS result=RegGetValueW(association,nullptr,L"TypeOverlay",RRF_RT_REG_SZ,nullptr,overlay,&bytes);
        RegCloseKey(association);
        if(result==ERROR_SUCCESS) {
            if(overlay[0]) {
                wchar_t expanded[2048]{};ExpandEnvironmentStringsW(overlay,expanded,_countof(expanded));
                const int index=PathParseIconLocationW(expanded);
                SHDefExtractIconW(expanded,index,0,&icon,nullptr,32);
            }
            m_associatedIcons[extension]=icon;return icon;
        }
    }
    wchar_t executable[2048]{};DWORD characters=_countof(executable);
    if(SUCCEEDED(AssocQueryStringW(ASSOCF_VERIFY,ASSOCSTR_EXECUTABLE,extension.c_str(),L"open",executable,&characters))) {
        SHFILEINFOW info{};
        if(SHGetFileInfoW(executable,0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON))icon=info.hIcon;
    }
    m_associatedIcons[extension]=icon;return icon;
}

const ShellBrowserHost::BadgeInfo& ShellBrowserHost::Badge(const std::wstring& path)
{
    // One perceived-type / association / icon-size lookup per extension per session
    // (kept across view switches, refreshes and folders).
    std::wstring extension = PathFindExtensionW(path.c_str());
    CharLowerBuffW(extension.data(), DWORD(extension.size()));
    const auto found = m_badges.find(extension);
    if (found != m_badges.end()) return found->second;
    ++m_counters.badgeResolves;
    BadgeInfo badge;
    PERCEIVED perceived = PERCEIVED_TYPE_UNSPECIFIED;
    PERCEIVEDFLAG flags = 0;
    AssocGetPerceivedType(extension.c_str(), &perceived, &flags, nullptr);
    badge.media = perceived == PERCEIVED_TYPE_IMAGE || perceived == PERCEIVED_TYPE_VIDEO || perceived == PERCEIVED_TYPE_AUDIO;
    SHFILEINFOW type{};
    if (SHGetFileInfoW(extension.empty() ? L"file" : extension.c_str(), FILE_ATTRIBUTE_NORMAL, &type, sizeof(type),
            SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES))
        badge.systemIcon = type.iIcon;
    if (badge.media) {
        badge.icon = AssociatedAppIcon(path);
        ICONINFO info{}; BITMAP bitmap{};
        if (badge.icon && GetIconInfo(badge.icon, &info)) {
            if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap))
                badge.size = (std::min)(int(bitmap.bmWidth), MulDiv(20, m_dpi, 96));
            if (info.hbmColor) DeleteObject(info.hbmColor);
            if (info.hbmMask) DeleteObject(info.hbmMask);
        }
    }
    return m_badges.emplace(extension, badge).first->second;
}

LRESULT ShellBrowserHost::DrawIconItem(NMLVCUSTOMDRAW* draw)
{
    if (!m_iconSlot) return CDRF_DODEFAULT;
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) { ++m_paintSeq; return CDRF_NOTIFYITEMDRAW; }
    if (draw->dwItemType != LVCDI_ITEM) return CDRF_DODEFAULT;
    if (draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    const int index = static_cast<int>(draw->nmcd.dwItemSpec);
    RECT cell{};
    if (!ListView_GetItemRect(m_listWindow, index, &cell, LVIR_BOUNDS)) return CDRF_DODEFAULT;
    RECT viewport{}, visible{};
    GetClientRect(m_listWindow, &viewport);
    if (!IntersectRect(&visible, &cell, &viewport)) return CDRF_SKIPDEFAULT;
    const int pad = MulDiv(8, m_dpi, 96);
    IconCell(index, cell);
    const bool selected = (ListView_GetItemState(m_listWindow, index, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    const bool hot = m_hotItem == index;
    HDC dc = draw->nmcd.hdc;
    HBRUSH fill = CreateSolidBrush(selected ? RGB(0xE5,0xF1,0xFB) : hot ? RGB(0xF5,0xF5,0xF5) : RGB(255,255,255));
    FillRect(dc, &cell, fill); DeleteObject(fill);
    if (selected) {
        if (index == m_probeItem && !m_probeTick) {
            LARGE_INTEGER tick{}; QueryPerformanceCounter(&tick); m_probeTick = tick.QuadPart;
        }
        HPEN pen = CreatePen(PS_SOLID, MulDiv(1, m_dpi, 96), RGB(0x99,0xD1,0xFF));
        auto oldPen = SelectObject(dc, pen); auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, cell.left, cell.top, cell.right, cell.bottom);
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen);
    }
    std::wstring title;
    IFolderView2* view = nullptr;
    if (SUCCEEDED(m_browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
        IShellItem* shellItem = nullptr;
        if (SUCCEEDED(view->GetItem(index, IID_PPV_ARGS(&shellItem)))) {
            SFGAOF attrs=0; shellItem->GetAttributes(SFGAO_FOLDER, &attrs);
            PWSTR name = nullptr;
            PWSTR fullPath = nullptr;
            if (SUCCEEDED(shellItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &fullPath))) {
                // Never extract on the UI thread: a cached thumbnail, or the Shell icon
                // now and the thumbnail from the worker (which repaints just this cell).
                const std::wstring key = ThumbKey(shellItem, fullPath, m_iconSlot);
                HBITMAP bitmap = nullptr;
                if (LookupThumb(key, bitmap)) ++m_counters.thumbHits;
                else {
                    ++m_counters.placeholders;
                    RequestThumb(shellItem, key, fullPath, index);
                    DrawPlaceholder(dc, fullPath, cell, (attrs & SFGAO_FOLDER) != 0);
                }
                if (bitmap) {
                    BITMAP info{}; GetObjectW(bitmap,sizeof(info),&info);
                    HDC source=CreateCompatibleDC(dc);
                    auto old=SelectObject(source,bitmap);
                    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
                    int width=info.bmWidth,height=info.bmHeight;
                    if(width>m_iconSlot) {height=MulDiv(height,m_iconSlot,width);width=m_iconSlot;}
                    if(height>m_iconSlot) {width=MulDiv(width,m_iconSlot,height);height=m_iconSlot;}
                    AlphaBlend(dc,cell.left+pad+(m_iconSlot-width)/2,
                        cell.top+pad+(m_iconSlot-height)/2,width,height,
                        source,0,0,info.bmWidth,info.bmHeight,blend);
                    SelectObject(source,old); DeleteDC(source);
                    const BadgeInfo& badge = Badge(fullPath);
                    if (badge.media && badge.icon && badge.size > 0)
                        DrawIconEx(dc,cell.left+pad+(m_iconSlot+width)/2-badge.size,
                            cell.top+pad+(m_iconSlot+height)/2-badge.size,badge.icon,badge.size,badge.size,0,nullptr,DI_NORMAL);

                    FileTagInfo tag = FileTagManager::Instance().GetTag(fullPath);
                    const int ix = cell.left + pad + (m_iconSlot - width) / 2;
                    const int iy = cell.top + pad + (m_iconSlot - height) / 2;
                    if (tag.color != FileTagColor::None) {
                        const int radius = MulDiv(6, m_dpi, 96);
                        FileTagManager::DrawTagDot(dc, ix + width - radius, iy + radius, radius, FileTagManager::GetColorRef(tag.color));
                    }
                    if (tag.starred) {
                        const int starR = MulDiv(7, m_dpi, 96);
                        FileTagManager::DrawStar(dc, ix + starR, iy + starR, starR);
                    }
                }
                CoTaskMemFree(fullPath);
            }
            if (SUCCEEDED(shellItem->GetDisplayName((attrs & SFGAO_FOLDER) ? SIGDN_NORMALDISPLAY : SIGDN_FILESYSPATH, &name))) {
                title = (attrs & SFGAO_FOLDER) ? name : PathFindFileNameW(name);
                CoTaskMemFree(name);
            }
            shellItem->Release();
        }
        view->Release();
    }
    if (title.empty()) { wchar_t text[1024]{}; ListView_GetItemText(m_listWindow, index, 0, text, _countof(text)); title=text; }
    RECT text = {cell.left + pad, cell.top + pad + m_iconSlot + MulDiv(4,m_dpi,96),
        cell.right-pad, cell.bottom-MulDiv(4,m_dpi,96)};
    const int saved = SaveDC(dc);
    SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(m_listWindow, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(0x1A,0x1A,0x1A));
    DrawTextW(dc, title.c_str(), -1, &text, DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    RestoreDC(dc, saved);
    return CDRF_SKIPDEFAULT;
}

LRESULT CALLBACK ShellBrowserHost::ViewSubclass(HWND window, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    auto* host = reinterpret_cast<ShellBrowserHost*>(data);
    // WM_CONTEXTMENU and NM_RCLICK pass straight through: DefView shows its own menu.
    if (msg == WM_NOTIFY && lp) {
        auto* header = reinterpret_cast<NMHDR*>(lp);
        if(header->hwndFrom==host->m_listWindow && header->code==LVN_ITEMACTIVATE
            && reinterpret_cast<NMLISTVIEW*>(lp)->iItem>=0) {
            IShellView* view=nullptr;
            if(SUCCEEDED(host->m_browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
                const HRESULT handled=host->DefaultCommand(view);view->Release();
                if(handled==S_OK)return 0;
            }
        }
        if (header->hwndFrom == host->m_listWindow && header->code == LVN_ITEMCHANGED) {
            auto* change = reinterpret_cast<NMLISTVIEW*>(lp);
            if ((change->uChanged & LVIF_STATE) && ((change->uOldState ^ change->uNewState) & LVIS_SELECTED))
                PostMessageW(host->m_parent, host->m_selectionMessage, 0, 0);
        }
        if (header->hwndFrom == host->m_listWindow && header->code == NM_CUSTOMDRAW && host->m_iconSlot)
        {
            return host->DrawIconItem(reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
        }
        if (header->hwndFrom == host->m_listWindow && header->code == NM_CUSTOMDRAW && host->m_listSpacer)
            return host->DrawListIcon(reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
    }
    if (msg == WM_NCDESTROY) { host->m_viewWindow=nullptr; RemoveWindowSubclass(window, ViewSubclass, id); }
    return DefSubclassProc(window, msg, wp, lp);
}

void ShellBrowserHost::EnsureSelectionVisible()
{
    if (!m_browser) return;
    IFolderView2* view=nullptr;
    if (SUCCEEDED(m_browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
        int index=-1;
        if (SUCCEEDED(view->GetSelectedItem(-1, &index)) && index>=0)
            view->SelectItem(index, SVSI_SELECT | SVSI_ENSUREVISIBLE);
        view->Release();
    }
}

void ShellBrowserHost::FlushPaint()
{
    if (m_listWindow && ::IsWindowVisible(m_listWindow)) ::UpdateWindow(m_listWindow);
}

void ShellBrowserHost::Redraw()
{
    if (m_listWindow && ::IsWindow(m_listWindow)) {
        ::InvalidateRect(m_listWindow, nullptr, TRUE);
    }
}

void ShellBrowserHost::PaintScrollBar(HWND window)
{
    SCROLLBARINFO info{sizeof(info)};
    if (!GetScrollBarInfo(window, OBJID_VSCROLL, &info) || (info.rgstate[0] & STATE_SYSTEM_INVISIBLE)) return;
    RECT bounds{}; GetWindowRect(window, &bounds);
    RECT bar=info.rcScrollBar; OffsetRect(&bar,-bounds.left,-bounds.top);
    POINT cursor{}; GetCursorPos(&cursor);
    const bool hover=PtInRect(&info.rcScrollBar,cursor)!=FALSE;
    const int width=MulDiv(hover ? 8 : 4,m_dpi,96);
    HDC dc=GetWindowDC(window);
    if (!dc) return;
    FillRect(dc,&bar,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    if (info.xyThumbBottom>info.xyThumbTop) {
        const int x=(bar.left+bar.right-width)/2;
        HBRUSH brush=CreateSolidBrush(RGB(0xC4,0xC4,0xC4));
        auto oldBrush=SelectObject(dc,brush); auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
        RoundRect(dc,x,bar.top+info.xyThumbTop,x+width,bar.top+info.xyThumbBottom,width,width);
        SelectObject(dc,oldPen); SelectObject(dc,oldBrush); DeleteObject(brush);
    }
    ReleaseDC(window,dc);
}

LRESULT CALLBACK ShellBrowserHost::ListSubclass(HWND window, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    auto* host=reinterpret_cast<ShellBrowserHost*>(data);
    if(msg==WM_PAINT) {
        ++host->m_counters.listPaints;
        RECT update{},client{};
        if(GetUpdateRect(window,&update,FALSE) && GetClientRect(window,&client)
            && LONGLONG(update.right-update.left)*(update.bottom-update.top)*10
               >= LONGLONG(client.right-client.left)*(client.bottom-client.top)*9)
            ++host->m_counters.fullPaints;
        int w=0,h=0;
        const HIMAGELIST images=ListView_GetImageList(window,LVSIL_SMALL);
        // Shell can replace its image list internally after sorting/enumeration,
        // without sending LVM_SETIMAGELIST through the subclass chain.
        if(host->m_listSpacer && images!=host->m_listSpacer
            && ImageList_GetIconSize(images,&w,&h)) {
            // A refreshed Shell list can retain a tall row image list. Height is
            // not an identity check: its new item indices belong to this handle.
            if(ImageList_GetImageCount(images)>1)host->m_shellSmallImages=images;
            ListView_SetImageList(window,host->m_listSpacer,LVSIL_SMALL);
        }
    }
    if(msg==LVM_SETIMAGELIST && wp==LVSIL_SMALL && host->m_listSpacer
        && reinterpret_cast<HIMAGELIST>(lp)!=host->m_listSpacer) {
        const HIMAGELIST previous=host->m_shellSmallImages;
        if(reinterpret_cast<HIMAGELIST>(lp))
            host->m_shellSmallImages=reinterpret_cast<HIMAGELIST>(lp);
        DefSubclassProc(window,msg,wp,reinterpret_cast<LPARAM>(host->m_listSpacer));
        return reinterpret_cast<LRESULT>(previous);
    }
    if (msg == WM_MOUSEMOVE && host->m_iconSlot) {
        LVHITTESTINFO hit{}; hit.pt={static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};
        const int hot=ListView_HitTest(window,&hit);
        if (hot!=host->m_hotItem) {host->m_hotItem=hot;InvalidateRect(window,nullptr,FALSE);}
        TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0}; TrackMouseEvent(&track);
    }
    if (msg == WM_MOUSELEAVE && host->m_hotItem>=0) {host->m_hotItem=-1;InvalidateRect(window,nullptr,FALSE);}
    const LRESULT result=DefSubclassProc(window,msg,wp,lp);
    if (msg==WM_NCPAINT || msg==WM_PAINT || msg==WM_NCMOUSEMOVE || msg==WM_NCMOUSELEAVE || msg==WM_VSCROLL || msg==WM_MOUSEWHEEL)
        host->PaintScrollBar(window);
    if (msg==WM_NCMOUSEMOVE) { TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE|TME_NONCLIENT,window,0}; TrackMouseEvent(&track); }
    if (msg==WM_NCDESTROY) { host->m_listWindow=nullptr; RemoveWindowSubclass(window,ListSubclass,id); }
    return result;
}

bool ShellBrowserHost::SetSort(int column, bool ascending)
{
    if (!m_browser)
        return false;
    PROPERTYKEY key = PKEY_ItemNameDisplay;
    switch (column) {
    case 1: key = PKEY_DateModified; break;
    case 2: key = PKEY_ItemTypeText; break;
    case 3: key = PKEY_Size; break;
    default: break;
    }
    SORTCOLUMN sort = {};
    sort.propkey = key;
    sort.direction = ascending ? SORT_ASCENDING : SORT_DESCENDING;
    IFolderView2* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))) || !view)
        return false;
    // SetSortColumns re-sorts and repaints even when nothing changes.
    int count = 0;
    SORTCOLUMN current = {};
    if (SUCCEEDED(view->GetSortColumnCount(&count)) && count == 1
        && SUCCEEDED(view->GetSortColumns(&current, 1))
        && current.direction == sort.direction && current.propkey.pid == key.pid
        && IsEqualGUID(current.propkey.fmtid, key.fmtid)) {
        view->Release();
        return true;
    }
    ++m_counters.sortSets;
    EnsureBufferedList(m_listWindow);
    const HRESULT hr = view->SetSortColumns(&sort, 1);
    view->Release();
    return SUCCEEDED(hr);
}

bool ShellBrowserHost::ApplyGrouping(IFolderView2* view,bool restore) {
    IShellView* shell=nullptr;HWND root=nullptr;
    if(SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&shell)))) {shell->GetWindow(&root);shell->Release();}
    if(root!=m_groupingView || m_groupingPath!=m_lastNavigation) {
        m_groupingView=root;m_groupingPath=m_lastNavigation;
        m_hasWindowsGrouping=SUCCEEDED(view->GetGroupBy(&m_windowsGrouping,&m_windowsGroupingAscending));
    }
    if(m_groupingMode<0) {
        if(!restore || !m_hasWindowsGrouping)return true;
        ++m_counters.groupSets;
        return SUCCEEDED(view->SetGroupBy(m_windowsGrouping,m_windowsGroupingAscending));
    }
    const PROPERTYKEY key=m_groupingMode==1 ? PKEY_DateModified : m_groupingMode==2 ? PKEY_ItemTypeText : PKEY_Null;
    // SetGroupBy regroups (and repaints) even when unchanged; skip equal requests.
    PROPERTYKEY current{};BOOL ascending=FALSE;
    if(SUCCEEDED(view->GetGroupBy(&current,&ascending)) && ascending
        && current.pid==key.pid && IsEqualGUID(current.fmtid,key.fmtid))return true;
    ++m_counters.groupSets;
    return SUCCEEDED(view->SetGroupBy(key,TRUE));
}
bool ShellBrowserHost::SetGrouping(int mode) {
    const int previous=m_groupingMode;m_groupingMode=mode;
    if(!m_browser)return false;
    IFolderView2* view=nullptr;if(FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))))return false;
    const bool result=ApplyGrouping(view,mode<0 && previous>=0);view->Release();return result;
}

HRESULT ShellBrowserHost::SelectAbsoluteItem(PCIDLIST_ABSOLUTE item, UINT flags)
{
    if (!m_browser || !item) return E_UNEXPECTED;
    wchar_t target[MAX_PATH * 4]{};
    if (!SHGetPathFromIDListEx(item, target, _countof(target), GPFIDL_DEFAULT)) return E_INVALIDARG;
    std::wstring parent(target);
    const size_t slash = parent.find_last_of(L'\\');
    if (slash == std::wstring::npos) return E_INVALIDARG;
    parent.resize(slash == 2 ? 3 : slash); // keep "C:\" for drive-root children
    if (!IsAtPath(parent)) return S_FALSE;  // navigation still pending

    IFolderView2* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))) || !view) return S_FALSE;
    // Locate the row by path: the caller's child id may carry different hidden data
    // than the id the view enumerated, and an unlisted item must be retried later.
    int count = 0, index = -1;
    view->ItemCount(SVGIO_ALLVIEW, &count);
    for (int i = 0; i < count && index < 0; ++i) {
        IShellItem* candidate = nullptr;
        if (FAILED(view->GetItem(i, IID_PPV_ARGS(&candidate))) || !candidate) continue;
        PWSTR path = nullptr;
        if (SUCCEEDED(candidate->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
            if (CompareStringOrdinal(path, -1, target, -1, TRUE) == CSTR_EQUAL) index = i;
            CoTaskMemFree(path);
        }
        candidate->Release();
    }
    HRESULT hr = S_FALSE;
    if (index >= 0) {
        UINT select = flags ? flags : (SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_ENSUREVISIBLE | SVSI_FOCUSED);
        // SVSI_EDIT is honoured when the caller asked for a rename (OFASI_EDIT).
        hr = view->SelectItem(index, select);
        if (SUCCEEDED(hr)) hr = S_OK;
    }
    view->Release();
    return hr;
}

bool ShellBrowserHost::GetSelection(std::vector<std::pair<std::wstring, bool>>& paths) const
{
    paths.clear();
    if (!m_browser)
        return false;
    IFolderView2* view = nullptr;
    if (FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))) || !view)
        return false;
    int selectedCount = 0;
    const HRESULT countResult = view->ItemCount(SVGIO_SELECTION, &selectedCount);
    if (SUCCEEDED(countResult) && selectedCount == 0) {
        view->Release();
        return true; // some providers fail GetSelection when their selection is empty
    }
    IShellItemArray* selection = nullptr;
    HRESULT hr = view->GetSelection(FALSE, &selection);
    view->Release();
    if (hr == S_FALSE && !selection)
        return true;
    if (FAILED(hr) || !selection)
        return false;

    DWORD count = 0;
    selection->GetCount(&count);
    for (DWORD i = 0; i < count; ++i) {
        IShellItem* item = nullptr;
        if (FAILED(selection->GetItemAt(i, &item)) || !item)
            continue;
        PWSTR path = nullptr;
        SFGAOF attrs = 0;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
            item->GetAttributes(SFGAO_FOLDER, &attrs);
            paths.emplace_back(path, (attrs & SFGAO_FOLDER) != 0);
            ::CoTaskMemFree(path);
        }
        item->Release();
    }
    selection->Release();
    return true;
}

bool ShellBrowserHost::HasVisibleViewBounds() const {
    if(!m_visible || !m_browser)return false;
    IShellView* view=nullptr;HWND root=nullptr;
    if(FAILED(m_browser->GetCurrentView(IID_PPV_ARGS(&view))) || !view)return false;
    const auto result=view->GetWindow(&root);view->Release();
    if(FAILED(result) || !root || !IsWindowVisible(root))return false;
    HWND list=nullptr;
    EnumChildWindows(root,[](HWND child,LPARAM value)->BOOL {
        wchar_t name[64]{};GetClassNameW(child,name,_countof(name));
        if(!wcscmp(name,L"SysListView32") && IsWindowVisible(child)) {
            *reinterpret_cast<HWND*>(value)=child;return FALSE;
        }return TRUE;
    },reinterpret_cast<LPARAM>(&list));
    RECT bounds{};
    return list && GetClientRect(list,&bounds) && bounds.right>0 && bounds.bottom>0;
}
