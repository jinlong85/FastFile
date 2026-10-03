// FastFile - Shell window registration (see ShellWindowRegistration.h).

#include "ShellWindowRegistration.h"

#include <exdisp.h>
#include <propvarutil.h>
#include <shlguid.h>
#include <shobjidl.h>

#include <cstdio>
#include <cstring>
#include <new>

#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "uuid.lib")

struct ShellWindowRegistration::State {
    HWND window = nullptr;
    PIDLIST_ABSOLUTE location = nullptr;
    SelectHandler onSelect;
    NavigateHandler onNavigate;
    ~State() { if (location) CoTaskMemFree(location); }
};

namespace {

// Optional diagnostics: FASTFILE_SHELLWINDOW_TRACE=<file> appends each Shell call.
void Trace(const wchar_t* what, REFIID riid = GUID_NULL, REFIID extra = GUID_NULL)
{
    wchar_t path[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"FASTFILE_SHELLWINDOW_TRACE", path, MAX_PATH)) return;
    wchar_t a[64]{}, b[64]{}, line[256]{};
    StringFromGUID2(riid, a, 64);
    StringFromGUID2(extra, b, 64);
    swprintf_s(line, L"%lu %s %s %s\r\n", GetTickCount(), what, a, b);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, line, DWORD(wcslen(line) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}

using StatePtr = std::shared_ptr<ShellWindowRegistration::State>;

template <typename Base>
class RefCounted : public Base {
public:
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG refs = InterlockedDecrement(&m_refs);
        if (!refs) delete this;
        return refs;
    }
    virtual ~RefCounted() = default;
private:
    LONG m_refs = 1;
};

// The Shell asks the document for an IShellView service and calls SelectItem with a
// child id relative to the folder this window registered.  Nothing else is used.
class ShellViewProxy final : public RefCounted<IShellView> {
#define TRACE_NAME L"ShellView"
public:
    explicit ShellViewProxy(StatePtr state) : m_state(std::move(state)) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        Trace(TRACE_NAME L"::QI", riid);
        if (!object) return E_POINTER;
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IOleWindow || riid == IID_IShellView) {
            *object = static_cast<IShellView*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP GetWindow(HWND* window) override
    {
        if (!window) return E_POINTER;
        *window = m_state->window;
        return *window ? S_OK : E_FAIL;
    }
    STDMETHODIMP ContextSensitiveHelp(BOOL) override { Trace(TRACE_NAME L"::ContextSensitiveHelp"); return E_NOTIMPL; }
    STDMETHODIMP TranslateAccelerator(MSG*) override { return S_FALSE; }
    STDMETHODIMP EnableModeless(BOOL) override { Trace(TRACE_NAME L"::EnableModeless"); return E_NOTIMPL; }
    STDMETHODIMP UIActivate(UINT) override { Trace(TRACE_NAME L"::UIActivate"); return E_NOTIMPL; }
    STDMETHODIMP Refresh() override { Trace(TRACE_NAME L"::Refresh"); return E_NOTIMPL; }
    STDMETHODIMP CreateViewWindow(IShellView*, LPCFOLDERSETTINGS, IShellBrowser*, RECT*, HWND*) override { Trace(TRACE_NAME L"::CreateViewWindow"); return E_NOTIMPL; }
    STDMETHODIMP DestroyViewWindow() override { Trace(TRACE_NAME L"::DestroyViewWindow"); return E_NOTIMPL; }
    STDMETHODIMP GetCurrentInfo(LPFOLDERSETTINGS) override { Trace(TRACE_NAME L"::GetCurrentInfo"); return E_NOTIMPL; }
    STDMETHODIMP AddPropertySheetPages(DWORD, LPFNSVADDPROPSHEETPAGE, LPARAM) override { Trace(TRACE_NAME L"::AddPropertySheetPages"); return E_NOTIMPL; }
    STDMETHODIMP SaveViewState() override { Trace(TRACE_NAME L"::SaveViewState"); return E_NOTIMPL; }
    STDMETHODIMP SelectItem(PCUITEMID_CHILD child, SVSIF flags) override
    {
        wchar_t detail[160]{};
        swprintf_s(detail,L"ShellView::SelectItem child=%p flags=%x location=%p handler=%d",child,
            static_cast<UINT>(flags),m_state->location,m_state->onSelect?1:0);
        Trace(detail);
        if (!child || !m_state->location || !m_state->onSelect) return E_FAIL;
        PIDLIST_ABSOLUTE item = ILCombine(m_state->location, child);
        if (!item) return E_OUTOFMEMORY;
        const HRESULT result = m_state->onSelect(item, flags);
        CoTaskMemFree(item);
        return result;
    }
    STDMETHODIMP GetItemObject(UINT, REFIID, void** object) override
    {
        if (object) *object = nullptr;
        return E_NOTIMPL;
    }
private:
    StatePtr m_state;
};

class DispatchStub {
protected:
    static HRESULT NoTypeInfo(UINT* count) { if (count) *count = 0; return S_OK; }
};

// The registered browser exposes its active Shell view through SID_STopLevelBrowser.
class ShellBrowserProxy final : public RefCounted<IShellBrowser> {
public:
    explicit ShellBrowserProxy(StatePtr state) : m_state(std::move(state)) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (riid != IID_IUnknown && riid != IID_IOleWindow && riid != IID_IShellBrowser) return E_NOINTERFACE;
        *object = static_cast<IShellBrowser*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP GetWindow(HWND* window) override {
        if (!window) return E_POINTER;
        *window = m_state->window; return *window ? S_OK : E_FAIL;
    }
    STDMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP InsertMenusSB(HMENU, LPOLEMENUGROUPWIDTHS) override { return E_NOTIMPL; }
    STDMETHODIMP SetMenuSB(HMENU, HOLEMENU, HWND) override { return E_NOTIMPL; }
    STDMETHODIMP RemoveMenusSB(HMENU) override { return E_NOTIMPL; }
    STDMETHODIMP SetStatusTextSB(LPCWSTR) override { return E_NOTIMPL; }
    STDMETHODIMP EnableModelessSB(BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP TranslateAcceleratorSB(MSG*, WORD) override { return S_FALSE; }
    STDMETHODIMP BrowseObject(PCUIDLIST_RELATIVE, UINT) override { return E_NOTIMPL; }
    STDMETHODIMP GetViewStateStream(DWORD, IStream** stream) override {
        if (stream) *stream = nullptr; return E_NOTIMPL;
    }
    STDMETHODIMP GetControlWindow(UINT, HWND* window) override {
        if (window) *window = nullptr; return E_NOTIMPL;
    }
    STDMETHODIMP SendControlMsg(UINT, UINT, WPARAM, LPARAM, LRESULT*) override { return E_NOTIMPL; }
    STDMETHODIMP QueryActiveShellView(IShellView** view) override {
        Trace(L"ShellBrowser::QueryActiveShellView");
        if (!view) return E_POINTER;
        *view = new (std::nothrow) ShellViewProxy(m_state);
        return *view ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP OnViewWindowActive(IShellView*) override { return S_OK; }
    STDMETHODIMP SetToolbarItems(LPTBBUTTONSB, UINT, UINT) override { return E_NOTIMPL; }
private:
    StatePtr m_state;
};

// get_Document result: an IDispatch that is really used as IServiceProvider.
#undef TRACE_NAME
class DocumentProvider final : public RefCounted<IDispatch>, public IServiceProvider, DispatchStub {
#define TRACE_NAME L"Document"
public:
    explicit DocumentProvider(StatePtr state) : m_state(std::move(state)) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        Trace(TRACE_NAME L"::QI", riid);
        if (!object) return E_POINTER;
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDispatch) *object = static_cast<IDispatch*>(this);
        else if (riid == IID_IServiceProvider) *object = static_cast<IServiceProvider*>(this);
        if (!*object) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return RefCounted<IDispatch>::AddRef(); }
    STDMETHODIMP_(ULONG) Release() override { return RefCounted<IDispatch>::Release(); }
    STDMETHODIMP GetTypeInfoCount(UINT* count) override { return NoTypeInfo(count); }
    STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo** info) override { if (info) *info = nullptr; return E_NOTIMPL; }
    STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { Trace(TRACE_NAME L"::GetIDsOfNames"); return E_NOTIMPL; }
    STDMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { Trace(TRACE_NAME L"::Invoke"); return DISP_E_MEMBERNOTFOUND; }
    STDMETHODIMP QueryService(REFGUID service, REFIID riid, void** object) override
    {
        Trace(L"Document::QueryService", service, riid);
        if (!object) return E_POINTER;
        *object = nullptr;
        if (service == IID_IFolderView || service == IID_IFolderView2 || service == IID_IShellView) {
            auto* view = new (std::nothrow) ShellViewProxy(m_state);
            if (!view) return E_OUTOFMEMORY;
            const HRESULT result = view->QueryInterface(riid, object);
            view->Release();
            return result;
        }
        return E_NOINTERFACE;
    }
private:
    StatePtr m_state;
};

// The object registered with IShellWindows.  Only get_HWND and get_Document matter;
// the remaining IWebBrowserApp members are not used by the Shell for this purpose.
#undef TRACE_NAME
class BrowserApp final : public RefCounted<IWebBrowser2>, public IServiceProvider, DispatchStub {
#define TRACE_NAME L"BrowserApp"
public:
    explicit BrowserApp(StatePtr state) : m_state(std::move(state)) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        Trace(TRACE_NAME L"::QI", riid);
        if (!object) return E_POINTER;
        *object = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IWebBrowser
            || riid == IID_IWebBrowserApp || riid == IID_IWebBrowser2) {
            *object = static_cast<IWebBrowserApp*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IServiceProvider) {
            *object = static_cast<IServiceProvider*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return RefCounted<IWebBrowser2>::AddRef(); }
    STDMETHODIMP_(ULONG) Release() override { return RefCounted<IWebBrowser2>::Release(); }
    STDMETHODIMP QueryService(REFGUID service, REFIID riid, void** object) override {
        Trace(L"BrowserApp::QueryService", service, riid);
        if (!object) return E_POINTER;
        *object = nullptr;
        if (service != SID_STopLevelBrowser && service != IID_IShellBrowser) return E_NOINTERFACE;
        auto* browser = new (std::nothrow) ShellBrowserProxy(m_state);
        if (!browser) return E_OUTOFMEMORY;
        const HRESULT hr = browser->QueryInterface(riid, object);
        browser->Release(); return hr;
    }
    // IDispatch
    STDMETHODIMP GetTypeInfoCount(UINT* count) override { return NoTypeInfo(count); }
    STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo** info) override { if (info) *info = nullptr; return E_NOTIMPL; }
    STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { Trace(TRACE_NAME L"::GetIDsOfNames"); return E_NOTIMPL; }
    STDMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { Trace(TRACE_NAME L"::Invoke"); return DISP_E_MEMBERNOTFOUND; }
    // IWebBrowser
    STDMETHODIMP GoBack() override { Trace(TRACE_NAME L"::GoBack"); return E_NOTIMPL; }
    STDMETHODIMP GoForward() override { Trace(TRACE_NAME L"::GoForward"); return E_NOTIMPL; }
    STDMETHODIMP GoHome() override { Trace(TRACE_NAME L"::GoHome"); return E_NOTIMPL; }
    STDMETHODIMP GoSearch() override { Trace(TRACE_NAME L"::GoSearch"); return E_NOTIMPL; }
    STDMETHODIMP Navigate(BSTR url, VARIANT*, VARIANT*, VARIANT*, VARIANT*) override {
        VARIANT value{};value.vt=VT_BSTR;value.bstrVal=url;
        return Navigate2(&value,nullptr,nullptr,nullptr,nullptr);
    }
    STDMETHODIMP Refresh() override { Trace(TRACE_NAME L"::Refresh"); return E_NOTIMPL; }
    STDMETHODIMP Refresh2(VARIANT*) override { Trace(TRACE_NAME L"::Refresh2"); return E_NOTIMPL; }
    STDMETHODIMP Stop() override { Trace(TRACE_NAME L"::Stop"); return E_NOTIMPL; }
    STDMETHODIMP get_Application(IDispatch** value) override { return Null(value); }
    STDMETHODIMP get_Parent(IDispatch** value) override { return Null(value); }
    STDMETHODIMP get_Container(IDispatch** value) override { return Null(value); }
    STDMETHODIMP get_Document(IDispatch** value) override
    {
        Trace(TRACE_NAME L"::get_Document");
        if (!value) return E_POINTER;
        *value = new (std::nothrow) DocumentProvider(m_state);
        return *value ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP get_TopLevelContainer(VARIANT_BOOL* value) override { if (!value) return E_POINTER; *value = VARIANT_TRUE; return S_OK; }
    STDMETHODIMP get_Type(BSTR* value) override { return NullBstr(value); }
    STDMETHODIMP get_Left(long*) override { Trace(TRACE_NAME L"::get_Left"); return E_NOTIMPL; }
    STDMETHODIMP put_Left(long) override { Trace(TRACE_NAME L"::put_Left"); return E_NOTIMPL; }
    STDMETHODIMP get_Top(long*) override { Trace(TRACE_NAME L"::get_Top"); return E_NOTIMPL; }
    STDMETHODIMP put_Top(long) override { Trace(TRACE_NAME L"::put_Top"); return E_NOTIMPL; }
    STDMETHODIMP get_Width(long*) override { Trace(TRACE_NAME L"::get_Width"); return E_NOTIMPL; }
    STDMETHODIMP put_Width(long) override { Trace(TRACE_NAME L"::put_Width"); return E_NOTIMPL; }
    STDMETHODIMP get_Height(long*) override { Trace(TRACE_NAME L"::get_Height"); return E_NOTIMPL; }
    STDMETHODIMP put_Height(long) override { Trace(TRACE_NAME L"::put_Height"); return E_NOTIMPL; }
    STDMETHODIMP get_LocationName(BSTR* value) override { return NullBstr(value); }
    STDMETHODIMP get_LocationURL(BSTR* value) override {
        if(!value)return E_POINTER;
        *value=nullptr;
        if(!m_state->location)return E_FAIL;
        PWSTR url=nullptr;HRESULT hr=SHGetNameFromIDList(m_state->location,SIGDN_URL,&url);
        if(SUCCEEDED(hr)){*value=SysAllocString(url);if(!*value)hr=E_OUTOFMEMORY;}
        CoTaskMemFree(url);return hr;
    }
    STDMETHODIMP get_Busy(VARIANT_BOOL* value) override { if (!value) return E_POINTER; *value = VARIANT_FALSE; return S_OK; }
    // IWebBrowserApp
    STDMETHODIMP Quit() override { Trace(TRACE_NAME L"::Quit"); return E_NOTIMPL; }
    STDMETHODIMP ClientToWindow(int*, int*) override { Trace(TRACE_NAME L"::ClientToWindow"); return E_NOTIMPL; }
    STDMETHODIMP PutProperty(BSTR, VARIANT) override { Trace(TRACE_NAME L"::PutProperty"); return E_NOTIMPL; }
    STDMETHODIMP GetProperty(BSTR, VARIANT*) override { Trace(TRACE_NAME L"::GetProperty"); return E_NOTIMPL; }
    STDMETHODIMP get_Name(BSTR* value) override
    {
        Trace(TRACE_NAME L"::get_Name");
        if (!value) return E_POINTER;
        *value = SysAllocString(L"FastFile");
        return *value ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP get_HWND(SHANDLE_PTR* value) override
    {
        Trace(TRACE_NAME L"::get_HWND");
        if (!value) return E_POINTER;
        *value = reinterpret_cast<SHANDLE_PTR>(m_state->window);
        return S_OK;
    }
    STDMETHODIMP get_FullName(BSTR* value) override { return NullBstr(value); }
    STDMETHODIMP get_Path(BSTR* value) override { return NullBstr(value); }
    STDMETHODIMP get_Visible(VARIANT_BOOL* value) override
    {
        Trace(TRACE_NAME L"::get_Visible");
        if (!value) return E_POINTER;
        *value = IsWindowVisible(m_state->window) ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }
    STDMETHODIMP put_Visible(VARIANT_BOOL) override { return S_OK; }
    STDMETHODIMP get_StatusBar(VARIANT_BOOL*) override { Trace(TRACE_NAME L"::get_StatusBar"); return E_NOTIMPL; }
    STDMETHODIMP put_StatusBar(VARIANT_BOOL) override { Trace(TRACE_NAME L"::put_StatusBar"); return E_NOTIMPL; }
    STDMETHODIMP get_StatusText(BSTR* value) override { return NullBstr(value); }
    STDMETHODIMP put_StatusText(BSTR) override { Trace(TRACE_NAME L"::put_StatusText"); return E_NOTIMPL; }
    STDMETHODIMP get_ToolBar(int*) override { Trace(TRACE_NAME L"::get_ToolBar"); return E_NOTIMPL; }
    STDMETHODIMP put_ToolBar(int) override { Trace(TRACE_NAME L"::put_ToolBar"); return E_NOTIMPL; }
    STDMETHODIMP get_MenuBar(VARIANT_BOOL*) override { Trace(TRACE_NAME L"::get_MenuBar"); return E_NOTIMPL; }
    STDMETHODIMP put_MenuBar(VARIANT_BOOL) override { Trace(TRACE_NAME L"::put_MenuBar"); return E_NOTIMPL; }
    STDMETHODIMP get_FullScreen(VARIANT_BOOL*) override { Trace(TRACE_NAME L"::get_FullScreen"); return E_NOTIMPL; }
    STDMETHODIMP put_FullScreen(VARIANT_BOOL) override { Trace(TRACE_NAME L"::put_FullScreen"); return E_NOTIMPL; }
    // The Shell queries IWebBrowser2 when it needs an existing window to navigate
    // before selecting an item. Refusing this interface makes cold-folder calls fail.
    STDMETHODIMP Navigate2(VARIANT* url, VARIANT*, VARIANT*, VARIANT*, VARIANT*) override {
        Trace(L"BrowserApp::Navigate2");
        if(!url)return E_POINTER;
        if(!m_state->window || !m_state->onNavigate)return E_FAIL;
        VARIANT resolved{};VariantInit(&resolved);
        HRESULT hr=VariantCopyInd(&resolved,url);
        if(FAILED(hr))return hr;
        PIDLIST_ABSOLUTE folder=nullptr;
        if(resolved.vt==VT_BSTR && resolved.bstrVal)
            hr=SHParseDisplayName(resolved.bstrVal,nullptr,&folder,0,nullptr);
        else if(resolved.vt==(VT_ARRAY|VT_UI1) && resolved.parray
            && SafeArrayGetDim(resolved.parray)==1 && SafeArrayGetElemsize(resolved.parray)==1) {
            LONG lower=0,upper=-1;
            hr=SafeArrayGetLBound(resolved.parray,1,&lower);
            if(SUCCEEDED(hr))hr=SafeArrayGetUBound(resolved.parray,1,&upper);
            BYTE* data=nullptr;
            if(SUCCEEDED(hr))hr=SafeArrayAccessData(resolved.parray,reinterpret_cast<void**>(&data));
            if(SUCCEEDED(hr)) {
                const LONGLONG bytes=static_cast<LONGLONG>(upper)-lower+1;
                LONGLONG offset=0;bool valid=false;
                while(offset+static_cast<LONGLONG>(sizeof(USHORT))<=bytes) {
                    USHORT size=0;std::memcpy(&size,data+offset,sizeof(size));
                    if(size==0){valid=offset+sizeof(size)==bytes;break;}
                    if(size<sizeof(size) || offset+size>bytes)break;
                    offset+=size;
                }
                if(valid)folder=ILCloneFull(reinterpret_cast<PCIDLIST_ABSOLUTE>(data));
                hr=valid?(folder?S_OK:E_OUTOFMEMORY):E_INVALIDARG;
                SafeArrayUnaccessData(resolved.parray);
            }
        } else hr=E_INVALIDARG;
        VariantClear(&resolved);
        if(SUCCEEDED(hr) && folder)hr=m_state->onNavigate(folder);
        CoTaskMemFree(folder);return hr;
    }
    STDMETHODIMP QueryStatusWB(OLECMDID, OLECMDF* flags) override { if(flags)*flags=static_cast<OLECMDF>(0);return E_NOTIMPL; }
    STDMETHODIMP ExecWB(OLECMDID, OLECMDEXECOPT, VARIANT*, VARIANT*) override { return E_NOTIMPL; }
    STDMETHODIMP ShowBrowserBar(VARIANT*, VARIANT*, VARIANT*) override { return E_NOTIMPL; }
    STDMETHODIMP get_ReadyState(READYSTATE* value) override { if(!value)return E_POINTER;*value=READYSTATE_COMPLETE;return S_OK; }
#define BROWSER_BOOL_PROPERTY(name,initial) \
    STDMETHODIMP get_##name(VARIANT_BOOL* value) override { if(!value)return E_POINTER;*value=initial;return S_OK; } \
    STDMETHODIMP put_##name(VARIANT_BOOL) override { return S_OK; }
    BROWSER_BOOL_PROPERTY(Offline,VARIANT_FALSE)
    BROWSER_BOOL_PROPERTY(Silent,VARIANT_FALSE)
    BROWSER_BOOL_PROPERTY(RegisterAsBrowser,VARIANT_TRUE)
    BROWSER_BOOL_PROPERTY(RegisterAsDropTarget,VARIANT_FALSE)
    BROWSER_BOOL_PROPERTY(TheaterMode,VARIANT_FALSE)
    BROWSER_BOOL_PROPERTY(AddressBar,VARIANT_TRUE)
    BROWSER_BOOL_PROPERTY(Resizable,VARIANT_TRUE)
#undef BROWSER_BOOL_PROPERTY
private:
    static HRESULT Null(IDispatch** value) { if (value) *value = nullptr; return E_NOTIMPL; }
    static HRESULT NullBstr(BSTR* value) { if (value) *value = nullptr; return E_NOTIMPL; }
    StatePtr m_state;
};

#undef TRACE_NAME
HRESULT LocationVariant(PCIDLIST_ABSOLUTE location, VARIANT* value)
{
    VariantInit(value);
    return InitVariantFromBuffer(location, ILGetSize(location), value);
}

} // namespace

ShellWindowRegistration::~ShellWindowRegistration()
{
    Revoke();
}

HRESULT ShellWindowRegistration::Register(HWND window, PCIDLIST_ABSOLUTE location,
    SelectHandler onSelect, NavigateHandler onNavigate)
{
    if (!window || !location) return E_INVALIDARG;
    IShellWindows* windows = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows));
    if (FAILED(hr) || !windows) return FAILED(hr) ? hr : E_FAIL;

    // A navigation reannouncement replaces the Shell cookie, not the live browser
    // state: the caller may already hold its view between QueryActiveShellView and
    // SelectItem. Only an actual window close may invalidate those outstanding views.
    auto state = m_state ? m_state : std::make_shared<State>();
    PIDLIST_ABSOLUTE nextLocation=ILCloneFull(location);
    if(!nextLocation){windows->Release();return E_OUTOFMEMORY;}
    if(m_registered && m_windows) {
        m_windows->Revoke(m_cookie);m_windows->Release();
        m_windows=nullptr;m_registered=false;m_cookie=0;
    }
    state->window = window;
    if(state->location)CoTaskMemFree(state->location);
    state->location = nextLocation;
    state->onSelect = std::move(onSelect);
    state->onNavigate = std::move(onNavigate);
    if (!state->location) { windows->Release(); return E_OUTOFMEMORY; }

    VARIANT where{}, root{};
    hr = LocationVariant(location, &where);
    VariantInit(&root);
    long pendingCookie = 0;
    // The Shell searches SWC_BROWSER windows. RegisterPending supplies the location
    // that a SHOpenFolderAndSelectItems request waits for; Register then attaches the
    // object to the pending entry created by this thread.
    if (SUCCEEDED(hr))
        hr = windows->RegisterPending(static_cast<long>(GetCurrentThreadId()), &where, &root,
            SWC_BROWSER, &pendingCookie);
    VariantClear(&where);
    BrowserApp* app = SUCCEEDED(hr) ? new (std::nothrow) BrowserApp(state) : nullptr;
    if (SUCCEEDED(hr) && !app) hr = E_OUTOFMEMORY;
    long cookie = 0;
    if (SUCCEEDED(hr))
        hr = windows->Register(static_cast<IDispatch*>(app),
            static_cast<long>(reinterpret_cast<LONG_PTR>(window)), SWC_BROWSER, &cookie);
    if (app) app->Release();
    if (FAILED(hr)) {
        if (pendingCookie) windows->Revoke(pendingCookie);
        windows->Release();
        return hr;
    }
    m_pendingCookie = pendingCookie; // diagnostics: equals Cookie() when the entry was completed
    if (pendingCookie && pendingCookie != cookie) {
        // Register normally completes the pending entry (same cookie). If the Shell
        // created a separate entry, give it the location and drop the orphan.
        windows->Revoke(pendingCookie);
        pendingCookie = 0;
        VARIANT again{};
        if (SUCCEEDED(LocationVariant(location, &again))) windows->OnNavigate(cookie, &again);
        VariantClear(&again);
    }
    m_windows = windows;
    m_cookie = cookie;
    m_state = std::move(state);
    m_registered = true;
    return S_OK;
}

HRESULT ShellWindowRegistration::Navigate(PCIDLIST_ABSOLUTE location)
{
    if (!m_registered || !m_windows || !location) return E_UNEXPECTED;
    PIDLIST_ABSOLUTE copy = ILCloneFull(location);
    if (!copy) return E_OUTOFMEMORY;
    if (m_state->location) CoTaskMemFree(m_state->location);
    m_state->location = copy;
    VARIANT where{};
    HRESULT hr = LocationVariant(location, &where);
    if (SUCCEEDED(hr)) hr = m_windows->OnNavigate(m_cookie, &where);
    VariantClear(&where);
    return hr;
}

void ShellWindowRegistration::Revoke()
{
    if (m_windows) {
        if (m_registered) m_windows->Revoke(m_cookie);
        m_windows->Release();
        m_windows = nullptr;
    }
    if (m_state) {
        // Outstanding Shell references keep the objects alive; they must no longer
        // reach a window that is going away.
        m_state->onSelect = nullptr;
        m_state->onNavigate = nullptr;
        m_state->window = nullptr;
        m_state.reset();
    }
    m_registered = false;
    m_cookie = 0;
}
