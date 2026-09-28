#include "MainWnd.h"

#include <algorithm>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include <KnownFolders.h>
#include <shlwapi.h>
#include <propsys.h>
#include <propkey.h>
#include <sstream>
#include <cstring>
#include <cctype>
#include <cwctype>
#include <functional>
#include <gdiplus.h>
#include <oleidl.h>
#include <objidl.h>
#include <fstream>
#include <cstdio>
#include <map>
#include <new>
#include <deque>
#include <condition_variable>
#include <dwmapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "propsys.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWCP_ROUNDSMALL
#define DWMWCP_ROUNDSMALL 3
#endif

namespace {

CLabelUI* MakeCell(LPCTSTR text, int fixedWidth, int padL)
{
    auto* p = new CLabelUI;
    p->SetText(text ? text : _T(""));
    p->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
    p->SetAttribute(_T("align"), _T("left"));
    p->SetAttribute(_T("valign"), _T("vcenter"));
    p->SetAttribute(_T("endellipsis"), _T("true"));
    if (padL > 0) {
        CDuiString pad;
        pad.Format(_T("%d,0,0,0"), padL);
        p->SetAttribute(_T("padding"), pad);
    }
    if (fixedWidth > 0)
        p->SetFixedWidth(fixedWidth);
    return p;
}

std::wstring FormatFileTimeULONGLONG(ULONGLONG ft)
{
    if (ft == 0) return L"";
    FILETIME ftw{};
    ftw.dwLowDateTime = static_cast<DWORD>(ft & 0xFFFFFFFFu);
    ftw.dwHighDateTime = static_cast<DWORD>((ft >> 32) & 0xFFFFFFFFu);
    FILETIME local{};
    if (!::FileTimeToLocalFileTime(&ftw, &local))
        return L"";
    SYSTEMTIME st{};
    if (!::FileTimeToSystemTime(&local, &st))
        return L"";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04u/%02u/%02u %02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

ULONGLONG FileTimeToU64(const FILETIME& ft)
{
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

bool ReadPropertyUInt64(IPropertyStore* store, REFPROPERTYKEY key, ULONGLONG& value)
{
    value = 0;
    if (!store) return false;
    PROPVARIANT pv;
    ::PropVariantInit(&pv);
    const HRESULT hr = store->GetValue(key, &pv);
    bool ok = false;
    if (SUCCEEDED(hr)) {
        switch (pv.vt) {
        case VT_UI8: value = pv.uhVal.QuadPart; ok = true; break;
        case VT_UI4: value = pv.ulVal; ok = true; break;
        case VT_I8:  value = static_cast<ULONGLONG>(pv.hVal.QuadPart); ok = pv.hVal.QuadPart >= 0; break;
        case VT_I4:  value = static_cast<ULONGLONG>(pv.lVal); ok = pv.lVal >= 0; break;
        default: break;
        }
    }
    ::PropVariantClear(&pv);
    return ok;
}

struct VideoPropertyInfo {
    ULONGLONG duration100ns = 0;
    ULONGLONG width = 0;
    ULONGLONG height = 0;
    ULONGLONG frameRateMilli = 0;
    ULONGLONG audioBitRate = 0;
};

VideoPropertyInfo ReadVideoProperties(const std::wstring& path)
{
    VideoPropertyInfo info;
    IPropertyStore* store = nullptr;
    if (FAILED(::SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_DEFAULT,
            IID_PPV_ARGS(&store))) || !store)
        return info;
    ReadPropertyUInt64(store, PKEY_Media_Duration, info.duration100ns);
    ReadPropertyUInt64(store, PKEY_Video_FrameWidth, info.width);
    ReadPropertyUInt64(store, PKEY_Video_FrameHeight, info.height);
    ReadPropertyUInt64(store, PKEY_Video_FrameRate, info.frameRateMilli);
    ReadPropertyUInt64(store, PKEY_Audio_EncodingBitrate, info.audioBitRate);
    store->Release();
    return info;
}

std::wstring FormatMediaDuration(ULONGLONG duration100ns)
{
    if (duration100ns == 0) return L"";
    const ULONGLONG seconds = duration100ns / 10000000ULL;
    wchar_t buf[48] = {};
    swprintf_s(buf, L"%02llu:%02llu:%02llu", seconds / 3600ULL,
        (seconds / 60ULL) % 60ULL, seconds % 60ULL);
    return buf;
}

std::wstring FormatBitRate(ULONGLONG bitsPerSecond)
{
    if (bitsPerSecond == 0) return L"";
    wchar_t buf[48] = {};
    if (bitsPerSecond >= 1000000ULL)
        swprintf_s(buf, L"%.2f Mbps", static_cast<double>(bitsPerSecond) / 1000000.0);
    else
        swprintf_s(buf, L"%llu kbps", bitsPerSecond / 1000ULL);
    return buf;
}

class DriveTileButtonUI final : public CButtonUI {
public:
    void SetDriveSpace(ULONGLONG freeBytes, ULONGLONG totalBytes) {
        m_freeBytes = freeBytes;
        m_totalBytes = totalBytes;
    }
    void PaintStatusImage(HDC hDC) override {
        CButtonUI::PaintStatusImage(hDC);
        if (!hDC || m_totalBytes == 0) return;
        RECT rc = GetPos();
        rc.left += 56;
        rc.right -= 8;
        rc.top = rc.bottom - 10;
        rc.bottom = rc.top + 4;
        if (rc.right <= rc.left) return;
        HBRUSH track = ::CreateSolidBrush(RGB(224, 224, 224));
        ::FillRect(hDC, &rc, track);
        ::DeleteObject(track);
        const ULONGLONG used = m_totalBytes > m_freeBytes ? m_totalBytes - m_freeBytes : 0;
        RECT fill = rc;
        fill.right = fill.left + static_cast<LONG>((used * static_cast<ULONGLONG>(rc.right - rc.left)) / m_totalBytes);
        HBRUSH usedBrush = ::CreateSolidBrush(RGB(0, 120, 212));
        ::FillRect(hDC, &fill, usedBrush);
        ::DeleteObject(usedBrush);
    }
private:
    ULONGLONG m_freeBytes = 0;
    ULONGLONG m_totalBytes = 0;
};

CControlUI* FindListItemRoot(CControlUI* pSender)
{
    CControlUI* pItem = pSender;
    while (pItem) {
        if (pItem->GetInterface(DUI_CTR_LISTCONTAINERELEMENT)
            || pItem->GetInterface(DUI_CTR_LISTELEMENT))
            return pItem;
        CDuiString ud = pItem->GetUserData();
        if (!ud.IsEmpty() && pItem->GetInterface(DUI_CTR_LISTCONTAINERELEMENT))
            return pItem;
        pItem = pItem->GetParent();
    }
    // Fallback: walk up until UserData is set
    pItem = pSender;
    while (pItem) {
        if (!pItem->GetUserData().IsEmpty())
            return pItem;
        pItem = pItem->GetParent();
    }
    return pSender;
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


// ---- CF_HDROP IDataObject for outbound drag ----
class HDropDataObject : public IDataObject
{
public:
    explicit HDropDataObject(const std::vector<std::wstring>& paths)
        : m_ref(1)
    {
        size_t bytes = sizeof(DROPFILES);
        for (const auto& p : paths)
            bytes += (p.size() + 1) * sizeof(wchar_t);
        bytes += sizeof(wchar_t);
        m_hGlobal = ::GlobalAlloc(GHND | GMEM_SHARE, bytes);
        if (!m_hGlobal) return;
        auto* df = static_cast<DROPFILES*>(::GlobalLock(m_hGlobal));
        if (!df) { ::GlobalFree(m_hGlobal); m_hGlobal = nullptr; return; }
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        df->pt.x = df->pt.y = 0;
        df->fNC = FALSE;
        wchar_t* dest = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES));
        for (const auto& p : paths) {
            wcsncpy(dest, p.c_str(), p.size() + 1);
            dest[p.size()] = L'\0';
            dest += p.size() + 1;
        }
        *dest = L'\0';
        ::GlobalUnlock(m_hGlobal);
    }
    ~HDropDataObject() {
        if (m_hGlobal) ::GlobalFree(m_hGlobal);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* fmt, STGMEDIUM* med) override {
        if (!fmt || !med || !m_hGlobal) return E_INVALIDARG;
        if (fmt->cfFormat != CF_HDROP || !(fmt->tymed & TYMED_HGLOBAL))
            return DV_E_FORMATETC;
        SIZE_T sz = ::GlobalSize(m_hGlobal);
        HGLOBAL copy = ::GlobalAlloc(GHND | GMEM_SHARE, sz);
        if (!copy) return E_OUTOFMEMORY;
        void* s = ::GlobalLock(m_hGlobal);
        void* d = ::GlobalLock(copy);
        if (s && d) memcpy(d, s, sz);
        if (s) ::GlobalUnlock(m_hGlobal);
        if (d) ::GlobalUnlock(copy);
        med->tymed = TYMED_HGLOBAL;
        med->hGlobal = copy;
        med->pUnkForRelease = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* fmt) override {
        if (!fmt) return E_INVALIDARG;
        if (fmt->cfFormat == CF_HDROP && (fmt->tymed & TYMED_HGLOBAL))
            return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        if (out) { out->ptd = nullptr; return DATA_S_SAMEFORMATETC; }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dir, IEnumFORMATETC** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = nullptr;
        if (dir != DATADIR_GET) return E_NOTIMPL;
        FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return ::SHCreateStdEnumFmtEtc(1, &fe, ppEnum);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
private:
    LONG m_ref;
    HGLOBAL m_hGlobal = nullptr;
};

class DropSource : public IDropSource
{
public:
    DropSource() : m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keyState) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (!(keyState & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
private:
    LONG m_ref;
};

class FastFileDropTarget : public IDropTarget
{
public:
    explicit FastFileDropTarget(CMainWnd* owner) : m_ref(1), m_owner(owner) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        m_allow = CanAccept(pDataObj);
        return DragOver(grfKeyState, pt, pdwEffect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        if (!pdwEffect) return E_INVALIDARG;
        if (!m_allow || !m_owner) {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }
        POINT p = { pt.x, pt.y };
        std::wstring dest;
        m_owner->HitTestDropPath(p, dest);
        if (dest.empty()) {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }
        if (dest == L"::FavoritePin") {
            *pdwEffect = DROPEFFECT_LINK;
            return S_OK;
        }
        if (grfKeyState & MK_CONTROL)
            *pdwEffect = DROPEFFECT_COPY;
        else if (grfKeyState & MK_SHIFT)
            *pdwEffect = DROPEFFECT_MOVE;
        else
            *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override { m_allow = false; return S_OK; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override {
        if (!pdwEffect || !m_owner || !pDataObj) return E_INVALIDARG;
        POINT p = { pt.x, pt.y };
        std::wstring dest;
        m_owner->HitTestDropPath(p, dest);
        if (dest.empty()) {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }
        DWORD effect = DROPEFFECT_COPY;
        if (dest == L"::FavoritePin") effect = DROPEFFECT_LINK;
        else if (grfKeyState & MK_SHIFT) effect = DROPEFFECT_MOVE;
        else if (grfKeyState & MK_CONTROL) effect = DROPEFFECT_COPY;
        std::vector<std::wstring> paths;
        if (!ExtractPaths(pDataObj, paths) || paths.empty()) {
            *pdwEffect = DROPEFFECT_NONE;
            return S_OK;
        }
        if (!m_owner->PerformDropTransfer(paths, dest, effect))
            effect = DROPEFFECT_NONE;
        *pdwEffect = effect;
        m_allow = false;
        return S_OK;
    }
private:
    static bool CanAccept(IDataObject* obj) {
        if (!obj) return false;
        FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return obj->QueryGetData(&fe) == S_OK;
    }
    static bool ExtractPaths(IDataObject* obj, std::vector<std::wstring>& out) {
        FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM med = {};
        if (FAILED(obj->GetData(&fe, &med)) || !med.hGlobal) return false;
        HDROP hDrop = static_cast<HDROP>(med.hGlobal);
        UINT n = ::DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        out.clear();
        wchar_t buf[MAX_PATH * 2] = {};
        for (UINT i = 0; i < n; ++i) {
            UINT len = ::DragQueryFileW(hDrop, i, buf, (UINT)_countof(buf));
            if (len > 0) out.emplace_back(buf);
        }
        ::ReleaseStgMedium(&med);
        return !out.empty();
    }
    LONG m_ref;
    CMainWnd* m_owner;
    bool m_allow = false;
};

} // namespace

CMainWnd::~CMainWnd()
{
    CaptureColumnWidths();
    SaveSession();
    if (m_hWnd) {
        ::KillTimer(m_hWnd, kTimerVirtSync);
        ::KillTimer(m_hWnd, kTimerColWidth);
    }
    StopDetailsFill();
    UninitDragDrop();
    CancelThumbJobs();
    StopThumbWorker();
    StopCopyThread(true);
}


#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

namespace {
UINT QueryWindowDpi(HWND hwnd)
{
    if (hwnd) {
        using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
        static GetDpiForWindowFn fn = reinterpret_cast<GetDpiForWindowFn>(
            ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (fn) {
            UINT d = fn(hwnd);
            if (d >= 72) return d;
        }
        HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (mon) {
            using GetDpiForMonitorFn = HRESULT (WINAPI*)(HMONITOR, int, UINT*, UINT*);
            static HMODULE shcore = ::LoadLibraryW(L"Shcore.dll");
            static GetDpiForMonitorFn getMon = shcore
                ? reinterpret_cast<GetDpiForMonitorFn>(::GetProcAddress(shcore, "GetDpiForMonitor"))
                : nullptr;
            if (getMon) {
                UINT dx = 0, dy = 0;
                if (SUCCEEDED(getMon(mon, 0, &dx, &dy)) && dx >= 72)
                    return dx;
            }
        }
    }
    HDC hdc = ::GetDC(hwnd ? hwnd : nullptr);
    int dpi = hdc ? ::GetDeviceCaps(hdc, LOGPIXELSX) : 96;
    if (hdc) ::ReleaseDC(hwnd ? hwnd : nullptr, hdc);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}
} // namespace dpi helpers

int CMainWnd::DpiScale(int px) const
{
    if (px == 0) return 0;
    return ::MulDiv(px, static_cast<int>(m_dpi), 96);
}

float CMainWnd::DpiScaleF(float v) const
{
    return v * (static_cast<float>(m_dpi) / 96.0f);
}

void CMainWnd::RefreshDpiFromWindow()
{
    m_dpi = QueryWindowDpi(m_hWnd);
    if (m_dpi < 72) m_dpi = 96;
}

void CMainWnd::ApplyDpiScaledFonts()
{
    // XML Font entries use 96-DPI pixel sizes; re-register at current DPI.
    // Prefer Segoe UI (Fluent); YaHei UI as CJK-friendly companion (id 4).
    const LPCTSTR face = _T("Segoe UI");
    const LPCTSTR faceCn = _T("Microsoft YaHei UI");
    m_PaintManager.AddFont(0, face, DpiScale(UiTokens::FontBody), false, false, false);
    m_PaintManager.SetDefaultFont(face, DpiScale(UiTokens::FontBody), false, false, false);
    m_PaintManager.AddFont(1, face, DpiScale(UiTokens::FontBody), true, false, false);
    m_PaintManager.AddFont(2, face, DpiScale(UiTokens::FontSmall), false, false, false);
    m_PaintManager.AddFont(3, face, DpiScale(UiTokens::FontCaption), false, false, false);
    m_PaintManager.AddFont(4, faceCn, DpiScale(UiTokens::FontBody), false, false, false);
    m_PaintManager.AddFont(5, face, DpiScale(UiTokens::FontPreviewTitle), true, false, false);
    m_PaintManager.AddFont(6, _T("Segoe MDL2 Assets"), DpiScale(18), false, false, false);
}

static void ScaleNamedFixed(CPaintManagerUI& pm, LPCTSTR name, int designW, int designH, UINT dpi)
{
    CControlUI* c = pm.FindControl(name);
    if (!c) return;
    if (designW > 0) c->SetFixedWidth(::MulDiv(designW, (int)dpi, 96));
    if (designH > 0) c->SetFixedHeight(::MulDiv(designH, (int)dpi, 96));
}

void CMainWnd::ApplyDpiScaledChrome()
{
    // Scale chrome bands + key panels from 96-DPI design sizes in main.xml.
    // Phase 3: command bar is 40px; separators/gaps DPI-scaled.
    ScaleNamedFixed(m_PaintManager, _T("title_bar"), 0, UiTokens::TitleBarH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("tab_bar"), 0, UiTokens::TabBarH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("toolbar"), 0, UiTokens::ToolbarH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("favorites_bar"), 0, UiTokens::FavoritesBarH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("address_bar"), 0, UiTokens::AddressBarH, m_dpi);
    // breadcrumb merged into address_bar (Explorer-style); no separate breadcrumb_bar
    ScaleNamedFixed(m_PaintManager, _T("status_bar"), 0, UiTokens::StatusBarH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("left_panel"), 220, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_pane"), UiTokens::PreviewPaneW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_image"), 0, UiTokens::PreviewImageH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_title"), 0, UiTokens::PreviewTitleH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_title"), 0, UiTokens::PreviewTitleGap, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_action"), 0, UiTokens::SpaceSm + 2, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_image"), 0, UiTokens::PreviewImageGap, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_text"), 0, UiTokens::PreviewTextGap, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_type"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_size"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_mtime"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_ctime"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_location"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_dimensions"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_duration"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_framerate"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_bitrate"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_totalbitrate"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_lbl_type"), UiTokens::PreviewMetaLabelW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_lbl_size"), UiTokens::PreviewMetaLabelW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_lbl_mtime"), UiTokens::PreviewMetaLabelW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_lbl_ctime"), UiTokens::PreviewMetaLabelW, 0, m_dpi);
    for (LPCTSTR label : { _T("preview_lbl_location"), _T("preview_lbl_dimensions"),
        _T("preview_lbl_duration"), _T("preview_lbl_framerate"), _T("preview_lbl_bitrate"),
        _T("preview_lbl_totalbitrate") })
        ScaleNamedFixed(m_PaintManager, label, UiTokens::PreviewMetaLabelW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("btn_preview_share"), UiTokens::PreviewActionW, UiTokens::PreviewActionH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("btn_cancel_copy"), UiTokens::StatusCancelW, UiTokens::StatusCancelH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("view_switcher"), 248, 28, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("search_box"), 210, UiTokens::SearchBoxH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("nav_hdr_quick"), 0, UiTokens::NavSectionHeaderH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("nav_hdr_thispc"), 0, UiTokens::NavSectionHeaderH, m_dpi);
    if (m_pLeftQuick) {
        m_pLeftQuick->SetSepHeight(DpiScale(UiTokens::LeftNavSepH));
        m_pLeftQuick->SetMinHeight(DpiScale(UiTokens::LeftQuickMinH));
        m_pLeftQuick->SetMaxHeight(DpiScale(720));
        if (m_leftQuickDesignH > 0)
            m_pLeftQuick->SetFixedHeight(DpiScale(m_leftQuickDesignH));
    }
    ScaleNamedFixed(m_PaintManager, _T("fav_bar_label"), UiTokens::FavLabelW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("fav_bar_hint"), 180, 0, m_dpi);

    // Subtle vertical separators between command-bar groups
    const LPCTSTR seps[] = {
        _T("sep_new"), _T("sep_organize"), _T("sep_more"),
    };
    for (auto name : seps) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) continue;
        c->SetFixedWidth(DpiScale(1));
        c->SetFixedHeight(DpiScale(24));
    }
    const LPCTSTR gaps[] = {
        _T("gap_clip"), _T("gap_clip2"),
        _T("gap_org"), _T("gap_org2"), _T("gap_more"), _T("gap_more2"),
    };
    for (auto name : gaps) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) continue;
        c->SetFixedWidth(DpiScale(8));
    }

    // Compact command-bar buttons.
    const LPCTSTR toolbarBtns[] = {
        _T("btn_copy"), _T("btn_paste"), _T("btn_cut"), _T("btn_share"),
        _T("btn_delete"), _T("btn_rename"),
        _T("btn_new_glyph"), _T("btn_new"), _T("btn_newfolder"), _T("btn_sort_glyph"), _T("btn_sort"), _T("btn_view_glyph"), _T("btn_view_menu"), _T("btn_more"),
        _T("btn_toggle_preview"),
        _T("btn_view_xlarge"), _T("btn_view_large"), _T("btn_view_medium"),
        _T("btn_view_list"), _T("btn_view_details"), _T("btn_view_tiles"),
        _T("fav_thispc"), _T("fav_documents"), _T("fav_desktop"), _T("fav_downloads"),
    };
    for (auto name : toolbarBtns) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) continue;
        const int fh = c->GetFixedHeight();
        if (fh > 0)
            c->SetFixedHeight(DpiScale(UiTokens::CmdBtnH));
    }
    // Explicit widths for command-bar glyph buttons
    struct BtnW { LPCTSTR name; int w; };
    const BtnW widths[] = {
        { _T("btn_back"), UiTokens::ToolbarNavBtnW }, { _T("btn_forward"), UiTokens::ToolbarNavBtnW },
        { _T("btn_up"), UiTokens::ToolbarNavBtnW }, { _T("btn_refresh"), UiTokens::ToolbarNavBtnW },
        { _T("btn_new_glyph"), 32 }, { _T("btn_new"), 58 },
        { _T("btn_cut"), UiTokens::ToolbarBtnW }, { _T("btn_copy"), UiTokens::ToolbarBtnW },
        { _T("btn_paste"), UiTokens::ToolbarBtnW }, { _T("btn_rename"), UiTokens::ToolbarBtnW },
        { _T("btn_share"), UiTokens::ToolbarBtnW }, { _T("btn_delete"), UiTokens::ToolbarBtnW },
        { _T("btn_sort_glyph"), 32 }, { _T("btn_sort"), 58 },
        { _T("btn_view_glyph"), 32 }, { _T("btn_view_menu"), 58 },
        { _T("btn_more"), UiTokens::ToolbarBtnW },
        { _T("btn_toggle_preview"), UiTokens::ToolbarBtnW },
        { _T("btn_tab_add"), 28 },
        { _T("btn_search"), 44 },
        { _T("chk_recursive"), UiTokens::SearchChkW },
    };
    for (const auto& bw : widths) {
        CControlUI* c = m_PaintManager.FindControl(bw.name);
        if (!c) continue;
        c->SetFixedWidth(DpiScale(bw.w));
    }
    if (CControlUI* addTab = m_PaintManager.FindControl(_T("btn_tab_add")))
        addTab->SetFixedHeight(DpiScale(UiTokens::HitTabH));
    // Search row: align with address (~28-32), not CmdBtnH 42
    {
        const int sh = DpiScale(UiTokens::SearchBoxH);
        for (LPCTSTR nm : { _T("btn_search"), _T("chk_recursive") }) {
            if (CControlUI* c = m_PaintManager.FindControl(nm))
            c->SetFixedHeight(DpiScale(UiTokens::SearchBoxH));
        }
        if (CControlUI* box = m_PaintManager.FindControl(_T("search_box")))
            box->SetFixedHeight(sh);
        for (LPCTSTR nm : { _T("btn_back"), _T("btn_forward"), _T("btn_up"), _T("btn_refresh") }) {
            if (CControlUI* c = m_PaintManager.FindControl(nm))
                c->SetFixedHeight(sh);
        }
        if (CControlUI* gap = m_PaintManager.FindControl(_T("gap_address_nav")))
            gap->SetFixedWidth(DpiScale(UiTokens::SpaceXs));
    }
    // Standard DuiLib caption controls: max/restore visibility is updated by WindowImplBase.
    const LPCTSTR captionBtns[] = { _T("minbtn"), _T("maxbtn"), _T("restorebtn"), _T("closebtn") };
    for (auto name : captionBtns) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) continue;
        c->SetFixedWidth(DpiScale(46));
        c->SetFixedHeight(DpiScale(32));
    }
    // XML attributes are design values. Keep all non-client hit areas in the same DPI space.
    RECT sizeBox = { DpiScale(4), DpiScale(4), DpiScale(4), DpiScale(4) };
    m_PaintManager.SetSizeBox(sizeBox);
    RECT caption = { 0, 0, 0, DpiScale(UiTokens::TitleBarH) };
    m_PaintManager.SetCaptionRect(caption);
    m_PaintManager.SetMinInfo(DpiScale(900), DpiScale(540));
    if (m_pDirTree)
        m_pDirTree->SetItemMinWidth(DpiScale(200));

    // Phase 2: details list header height from tokens
    if (m_pFileList) {
        CListHeaderUI* hdr = m_pFileList->GetHeader();
        if (hdr)
            hdr->SetFixedHeight(DpiScale(UiTokens::DetailsHeaderH));
    }

    // Phase 2: left-nav Quick Access rows use NavRowH
    for (LPCTSTR favName : {
        _T("fav_thispc"), _T("fav_documents"), _T("fav_desktop"), _T("fav_downloads")
    }) {
        if (CControlUI* cFav = m_PaintManager.FindControl(favName))
            cFav->SetFixedHeight(DpiScale(UiTokens::NavRowH));
    }

    // Grow client area to design*scale on first apply so 150%/200% feels premium
    if (!m_dpiChromeApplied && m_hWnd && m_dpi != 96) {
        RECT rc = {};
        ::GetWindowRect(m_hWnd, &rc);
        int w = DpiScale(m_designClientW);
        int h = DpiScale(m_designClientH);
        ::SetWindowPos(m_hWnd, nullptr, rc.left, rc.top, w, h,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    m_dpiChromeApplied = true;
    m_PaintManager.NeedUpdate();
}

void CMainWnd::EnsureDpiLayout()
{
    RefreshDpiFromWindow();
    ApplyDpiScaledFonts();
    m_dpiChromeApplied = false;
    ApplyDpiScaledChrome();
    ApplyWindowCornerAndPadding();
    ApplyChromeShellIcons();
    RefreshTreeShellIcons();
    if (m_hWnd) {
        RECT rc = {};
        ::GetWindowRect(m_hWnd, &rc);
        const int w = DpiScale(m_designClientW);
        const int h = DpiScale(m_designClientH);
        if (w > 0 && h > 0) {
            ::SetWindowPos(m_hWnd, nullptr, rc.left, rc.top, w, h,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    RebuildBreadcrumb();
    RebuildTabStrip();
    if (m_hasListingCache)
        SetViewMode(m_viewMode);
    m_PaintManager.NeedUpdate();
}

void CMainWnd::OnDpiChanged(UINT newDpi, const RECT* suggested)
{
    if (newDpi < 72) newDpi = 96;
    const bool dpiChanged = (newDpi != m_dpi);
    m_dpi = newDpi;
    if (dpiChanged) {
        ApplyDpiScaledFonts();
        m_dpiChromeApplied = false;
        ApplyDpiScaledChrome();
        ApplyWindowCornerAndPadding();
        ApplyLeftNavSplitterHeight(m_leftQuickDesignH);
        ApplyFileViewScrollBars();
        ApplyChromeShellIcons();
        RefreshTreeShellIcons();
        RebuildBreadcrumb();
        RebuildTabStrip();
        SetViewMode(m_viewMode);
    }
    // Keep design*DPI outer size. Suggested rect is for cross-monitor position only.
    if (m_hWnd) {
        RECT rc = {};
        ::GetWindowRect(m_hWnd, &rc);
        const int w = DpiScale(m_designClientW);
        const int h = DpiScale(m_designClientH);
        int x = rc.left, y = rc.top;
        if (suggested) { x = suggested->left; y = suggested->top; }
        ::SetWindowPos(m_hWnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    m_PaintManager.NeedUpdate();
}


CDuiString CMainWnd::GetSkinFolder()
{
    return CDuiString(_T("skin"));
}

CDuiString CMainWnd::GetSkinFile()
{
    return CDuiString(_T("main.xml"));
}

LPCTSTR CMainWnd::GetWindowClassName() const
{
    return _T("FastFile_MainWnd");
}

void CMainWnd::InitWindow()
{
    RefreshDpiFromWindow();
    ApplyDpiScaledFonts();

    m_pAddressEdit = static_cast<CEditUI*>(m_PaintManager.FindControl(_T("edit_address")));
    m_pAddressEditHost = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("address_edit_host")));
    m_pPathHost = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("path_host")));
    m_pSearchEdit = static_cast<CEditUI*>(m_PaintManager.FindControl(_T("edit_search")));
    m_addressEditMode = false;
    m_pFileList = static_cast<CListUI*>(m_PaintManager.FindControl(_T("file_list")));
    m_pDirTree = static_cast<CTreeViewUI*>(m_PaintManager.FindControl(_T("dir_tree")));
    m_pTabStrip = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("tab_strip")));
    m_pIconScroll = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("icon_scroll")));
    m_pIconTiles = static_cast<CTileLayoutUI*>(m_PaintManager.FindControl(_T("file_icons")));
    m_pBreadcrumb = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("breadcrumb")));
    m_pFavoritesBar = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("favorites_bar")));
    m_pFavoritesStrip = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("favorites_strip")));
    m_pLeftFavPins = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("left_fav_pins")));
    m_pLeftQuick = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("left_quick")));
    m_pLeftThisPc = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("left_thispc")));
    m_pPreviewPane = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("preview_pane")));
    m_pPreviewTitle = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_title")));
    m_pPreviewImage = m_PaintManager.FindControl(_T("preview_image"));
    m_pPreviewText = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_text")));
    m_pPreviewType = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_type")));
    m_pPreviewSize = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_size")));
    m_pPreviewMTime = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_mtime")));
    m_pPreviewCTime = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_ctime")));
    m_pPreviewLocation = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_location")));
    m_pPreviewDimensions = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_dimensions")));
    m_pPreviewDuration = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_duration")));
    m_pPreviewFrameRate = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_framerate")));
    m_pPreviewBitRate = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_bitrate")));
    m_pPreviewTotalBitRate = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_totalbitrate")));
    m_pBtnTogglePreview = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_toggle_preview")));
    m_pStatus = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("status_text")));
    m_pBtnCopy = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_copy")));
    m_pBtnPaste = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_paste")));
    m_pBtnCancelCopy = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_cancel_copy")));
    m_pBtnBack = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_back")));
    m_pBtnForward = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_forward")));
    m_pChkRecursive = static_cast<COptionUI*>(m_PaintManager.FindControl(_T("chk_recursive")));

    if (m_pFileList)
        m_pFileList->EnableScrollBar(true, false);
    // A: scrollbar on TileLayout (owns cyNeeded); host must not steal wheel
    if (m_pIconScroll)
        m_pIconScroll->EnableScrollBar(false, false);
    if (m_pIconTiles)
        m_pIconTiles->EnableScrollBar(true, false);
    ApplyFileViewScrollBars();
    if (m_pDirTree) {
        m_pDirTree->SetVisibleCheckBtn(false);
        m_pDirTree->SetVisibleFolderBtn(true);
        m_pDirTree->SetItemMinWidth(DpiScale(200));
        StyleVerticalScrollBar(m_pDirTree);
    }

    wchar_t tmp[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, tmp);
    m_iconCacheDir = tmp;
    m_iconCacheDir += L"FastFileIconCache";
    // v6: PNG + true alpha; wipe prior BMP/v5 cache on every relaunch.
    WipeDirectoryFiles(m_iconCacheDir);
    m_iconCacheDir.push_back(L'\\');
    ::CreateDirectoryW(m_iconCacheDir.c_str(), nullptr);

    ApplyDpiScaledChrome();
    LoadLeftNavSplitter();
    ApplyChromeShellIcons();
    ApplyCopyUiState();
    StartThumbWorker();
    InitDirectoryTree();
    InitTabs();
    LoadFavorites();
    LoadQuickAccess();
    RebuildFavoritesBar();
    RebuildLeftPinnedFavorites();
    InitDragDrop();
    ApplyFileViewScrollBars();
    ApplyColumnWidths();
    SetPreviewVisible(m_previewVisible);
    if (!LoadSession()) {
        const std::wstring start = GetDefaultStartPath();
        AddTab(start, true);
    }
    ApplyColumnWidths();
    UpdateHeaderSortIndicators();
    RebuildBreadcrumb();
    ClearPreview();
    UpdateViewModeButtons();
    UpdateFavoritesHighlight();
    UpdateNavButtons();
    if (m_hWnd)
        ::SetTimer(m_hWnd, kTimerColWidth, 2000, nullptr);
    ApplyWindowCornerAndPadding();
    SyncRecursiveCheckLabel();
    SetSearchPlaceholder(true);
}

void CMainWnd::Notify(TNotifyUI& msg)
{
    if (msg.sType == DUI_MSGTYPE_RETURN) {
        if (msg.pSender == m_pAddressEdit && m_pAddressEdit) {
            ExitAddressEditMode(true); // Enter -> navigate, then breadcrumb
            return;
        }
        if (msg.pSender == m_pSearchEdit) {
            ApplySearchFilter();
            return;
        }
    }
    else if (msg.sType == DUI_MSGTYPE_SETFOCUS) {
        if (msg.pSender == m_pSearchEdit && m_searchPlaceholder) {
            SetSearchPlaceholder(false);
            return;
        }
    }
    else if (msg.sType == DUI_MSGTYPE_KILLFOCUS) {
        if (msg.pSender == m_pAddressEdit && m_addressEditMode) {
            ExitAddressEditMode(false); // blur -> breadcrumb, no navigate
            return;
        }
        if (msg.pSender == m_pSearchEdit) {
            if (m_pSearchEdit && m_pSearchEdit->GetText().IsEmpty())
                SetSearchPlaceholder(true);
            return;
        }
    }
    else if (msg.sType == DUI_MSGTYPE_TEXTCHANGED) {
        if (msg.pSender == m_pSearchEdit) {
            if (m_searchPlaceholder)
                return;
            if (!IsRecursiveSearch())
                ApplySearchFilter();
            return;
        }
    }
    else if (msg.sType == DUI_MSGTYPE_SELECTCHANGED) {
        if (msg.pSender == m_pChkRecursive) {
            SyncRecursiveCheckLabel();
            if (!m_searchFilter.empty())
                ApplySearchFilter();
            return;
        }
    }
    else if (msg.sType == DUI_MSGTYPE_ITEMACTIVATE || msg.sType == DUI_MSGTYPE_ITEMDBCLICK) {
        // Tree node double-click / activate
        CControlUI* p = msg.pSender;
        while (p) {
            if (p->GetInterface(DUI_CTR_TREENODE)) {
                OnTreeNodeActivate(static_cast<CTreeNodeUI*>(p));
                return;
            }
            p = p->GetParent();
        }
        OnItemActivate(msg.pSender);
        return;
    }
    else if (msg.sType == DUI_MSGTYPE_ITEMCLICK) {
        CControlUI* p = msg.pSender;
        while (p) {
            if (p->GetInterface(DUI_CTR_TREENODE)) {
                if ((::GetKeyState(VK_RBUTTON) & 0x8000) != 0) {
                    POINT pt = {};
                    ::GetCursorPos(&pt);
                    ShowTreeContextMenu(static_cast<CTreeNodeUI*>(p), pt);
                    return;
                }
                // Single click on tree: load children + navigate
                OnTreeNodeActivate(static_cast<CTreeNodeUI*>(p));
                return;
            }
            p = p->GetParent();
        }
        // Right-click on file list/tiles is handled in WM_RBUTTONUP so it works
        // in every view mode (icons/tiles/list/details), including blank area.
        // ITEMCLICK is raised before Select() in DuiLib list items, so
        // CollectSelectedItems still sees the previous selection. Prefer the
        // clicked row; Ctrl+deselect already cleared IsSelected before notify.
        {
            CControlUI* clicked = FindListItemRoot(msg.pSender);
            if (clicked && !clicked->GetUserData().IsEmpty()) {
                IListItemUI* li = static_cast<IListItemUI*>(
                    clicked->GetInterface(DUI_CTR_ILISTITEM));
                const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (li && ctrl && !li->IsSelected()) {
                    UpdatePreviewForSelection();
                } else {
                    UpdatePreviewPath(clicked->GetUserData().GetData(),
                        clicked->GetTag() != 0);
                }
            } else {
                UpdatePreviewForSelection();
            }
        }
        return;
    }
    else if (msg.sType == DUI_MSGTYPE_HEADERCLICK) {
        OnHeaderColumnClick(msg.pSender);
        return;
    }
    else if (msg.sType == DUI_MSGTYPE_SCROLL) {
        if (m_iconVirtMode)
            SyncVisibleIconWindow(false);
        return;
    }
    else if (msg.sType == DUI_MSGTYPE_ITEMSELECT) {
        UpdatePreviewForSelection();
        UpdateListingStatusTip();
    }
    WindowImplBase::Notify(msg);
}

void CMainWnd::OnClick(TNotifyUI& msg)
{
    CDuiString name = msg.pSender->GetName();

    if (name == _T("btn_go")) {
        // Legacy go button removed from skin; keep handler harmless
        ExitAddressEditMode(true);
        return;
    }
    if (name == _T("bc_edit") || name == _T("path_host") || name == _T("address_bar")) {
        EnterAddressEditMode();
        return;
    }
    if (name == _T("btn_back")) {
        GoBack();
        return;
    }
    if (name == _T("btn_forward")) {
        GoForward();
        return;
    }
    if (name == _T("btn_up")) {
        GoUp();
        return;
    }
    if (name == _T("btn_refresh")) {
        RefreshListing();
        return;
    }
    if (name == _T("btn_search")) {
        ApplySearchFilter();
        return;
    }
    if (name == _T("btn_search_clear")) {
        ClearSearchFilter();
        return;
    }
    if (name == _T("chk_recursive")) {
        if (!m_searchFilter.empty())
            ApplySearchFilter();
        return;
    }
    if (name == _T("btn_view_xlarge")) {
        SetViewMode(ViewMode::ExtraLargeIcons);
        return;
    }
    if (name == _T("btn_view_large")) {
        SetViewMode(ViewMode::LargeIcons);
        return;
    }
    if (name == _T("btn_view_medium")) {
        SetViewMode(ViewMode::MediumIcons);
        return;
    }
    if (name == _T("btn_view_list")) {
        SetViewMode(ViewMode::List);
        return;
    }
    if (name == _T("btn_view_details")) {
        SetViewMode(ViewMode::Details);
        return;
    }
    if (name == _T("btn_view_tiles") || name == _T("btn_view_icons")) {
        SetViewMode(ViewMode::Tiles);
        return;
    }
    if (name == _T("btn_tab_add")) {
        AddTab(m_currentPath.empty() ? GetDefaultStartPath() : m_currentPath, true);
        return;
    }
    if (name.Find(_T("tab_btn_")) == 0) {
        int idx = _ttoi(name.GetData() + 8);
        ActivateTab(idx);
        return;
    }
    if (name.Find(_T("tab_close_")) == 0) {
        int idx = _ttoi(name.GetData() + 10);
        CloseTab(idx);
        return;
    }
    if (name == _T("btn_copy")) { OnCopyClicked(); return; }
    if (name == _T("btn_cut")) { OnCutClicked(); return; }
    if (name == _T("btn_paste")) { OnPasteClicked(); return; }
    if (name == _T("btn_cancel_copy")) { OnCancelCopyClicked(); return; }
    if (name == _T("btn_delete")) { OnDeleteClicked(); return; }
    if (name == _T("btn_rename")) { OnRenameClicked(); return; }
    if (name == _T("btn_share") || name == _T("btn_preview_share")) { OnShareClicked(); return; }
    if (name == _T("btn_new") || name == _T("btn_new_glyph") || name == _T("btn_newfolder")) { OnNewMenuClicked(); return; }
    if (name == _T("btn_sort") || name == _T("btn_sort_glyph")) { OnSortMenuClicked(); return; }
    if (name == _T("btn_view_menu") || name == _T("btn_view_glyph")) { OnViewMenuClicked(); return; }
    if (name == _T("btn_more")) { OnMoreMenuClicked(); return; }
    if (name == _T("fav_thispc") || name == _T("fav_documents")
        || name == _T("fav_desktop") || name == _T("fav_downloads")) {
        OnFavoriteClicked(name);
        return;
    }
    if (name.Find(_T("fav_pin_")) == 0 || name.Find(_T("fav_dyn_")) == 0) {
        OnPinnedFavoriteClick(msg.pSender);
        return;
    }
    if (name == _T("btn_toggle_preview")) {
        SetPreviewVisible(!m_previewVisible);
        if (m_previewVisible) UpdatePreviewForSelection();
        return;
    }
    if (name == _T("bc_seg")) {
        OnBreadcrumbSegmentClick(msg.pSender);
        return;
    }
    // Icon tile: Ctrl/Shift multi-select; double-click opens
    if (msg.pSender && !msg.pSender->GetUserData().IsEmpty()) {
        if (name.Find(_T("icon_")) == 0
            || (IsTileViewMode() && msg.pSender->GetParent() == m_pIconTiles)) {
            OnIconTileClick(msg.pSender);
            UpdatePreviewForSelection();
            return;
        }
    }
    if (name == _T("btn_min")) {
        SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
        return;
    }
    if (name == _T("btn_max")) {
        if (::IsZoomed(m_hWnd))
            SendMessage(WM_SYSCOMMAND, SC_RESTORE, 0);
        else
            SendMessage(WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        return;
    }

    WindowImplBase::OnClick(msg);
}

LRESULT CMainWnd::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // Forward owner-draw / cascading submenu messages to IContextMenu2/3
    if (m_pCtxMenu2 || m_pCtxMenu3) {
        if (uMsg == WM_INITMENUPOPUP || uMsg == WM_DRAWITEM || uMsg == WM_MEASUREITEM || uMsg == WM_MENUCHAR) {
            LRESULT lr = 0;
            bool handled = false;
            ForwardShellMenuMessage(uMsg, wParam, lParam, &lr, &handled);
            if (handled)
                return lr;
        }
    }
    if (uMsg == WM_DPICHANGED) {
        UINT newDpi = LOWORD(wParam);
        const RECT* prc = reinterpret_cast<const RECT*>(lParam);
        OnDpiChanged(newDpi, prc);
        return 0;
    }
    if (uMsg == WM_CLOSE) {
        CaptureColumnWidths();
        if (m_hWnd) {
            ::KillTimer(m_hWnd, kTimerVirtSync);
            ::KillTimer(m_hWnd, kTimerColWidth);
        }
        StopDetailsFill();
        SaveFavorites();
        SaveQuickAccess();
        SaveSession();
    }
    if (uMsg == WM_TIMER) {
        if (wParam == kTimerVirtSync) { SyncVisibleIconWindow(false); return 0; }
        if (wParam == kTimerColWidth) { CaptureColumnWidths(); return 0; }
    }
    if (uMsg == WM_LBUTTONDOWN && !m_inDoDragDrop) {
        m_dragTracking = true;
        m_dragStartPt.x = (short)LOWORD(lParam);
        m_dragStartPt.y = (short)HIWORD(lParam);
    }
    if (uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONDOWN) {
        m_dragTracking = false;
    }
    if (uMsg == WM_LBUTTONUP) {
        CaptureLeftNavSplitterIfChanged();
        POINT ptClient = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        CControlUI* hit = m_PaintManager.FindControl(ptClient);
        if (IsFileViewBlankHit(hit)) {
            ClearFileSelection();
            UpdateListingStatusTip();
        }
    }
    if (uMsg == WM_RBUTTONUP) {
        POINT ptClient = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        CControlUI* hit = m_PaintManager.FindControl(ptClient);
        CControlUI* p = hit;
        while (p) {
            CDuiString nm = p->GetName();
            if (nm.Find(_T("fav_pin_")) == 0 || nm.Find(_T("fav_dyn_")) == 0) {
                POINT ptScreen = ptClient;
                ::ClientToScreen(m_hWnd, &ptScreen);
                ShowFavoriteContextMenu(p, ptScreen);
                return 0;
            }
            p = p->GetParent();
        }
        if (IsFileViewBlankHit(hit)) {
            // Explorer clears selection before its folder-background menu.
            ClearFileSelection();
            UpdateListingStatusTip();
            POINT ptScreen = ptClient;
            ::ClientToScreen(m_hWnd, &ptScreen);
            ShowBlankAreaContextMenu(ptScreen);
            return 0;
        }
        bool overFileView = false;
        p = hit;
        while (p) {
            CDuiString nm = p->GetName();
            if (nm == _T("file_list") || nm == _T("file_icons") || nm == _T("icon_scroll") || nm == _T("list_host")) {
                overFileView = true;
                break;
            }
            if (p->GetInterface(DUI_CTR_TREENODE) || nm == _T("dir_tree") || nm == _T("left_panel")
                || nm == _T("left_quick") || nm == _T("left_thispc") || nm == _T("favorites_bar"))
                break;
            p = p->GetParent();
        }
        if (overFileView) {
            POINT ptScreen = ptClient;
            ::ClientToScreen(m_hWnd, &ptScreen);
            CControlUI* listItem = FindListItemRoot(hit);
            CControlUI* tile = nullptr;
            if (IsTileViewMode() && hit) {
                CControlUI* t = hit;
                while (t && t->GetParent() != m_pIconTiles)
                    t = t->GetParent();
                if (t && t->GetParent() == m_pIconTiles)
                    tile = t;
            }
            if (tile) {
                // Icons/tiles/list: select hit tile if needed, then IContextMenu
                if ((tile->GetTag() & 0x100) == 0) {
                    ClearIconSelection();
                    SetIconSelected(tile, true);
                    m_iconAnchor = FindIconIndex(tile);
                }
                ShowItemContextMenu(tile, ptScreen);
                return 0;
            }
            if (listItem) {
                // Details: DuiLib already selected on RBUTTONDOWN; show IContextMenu
                ShowItemContextMenu(listItem, ptScreen);
                return 0;
            }
            ShowBlankAreaContextMenu(ptScreen);
            return 0;
        }
    }
    if (uMsg == WM_MOUSEMOVE && m_dragTracking && !m_inDoDragDrop
        && (::GetKeyState(VK_LBUTTON) & 0x8000)) {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        const int dx = pt.x - m_dragStartPt.x;
        const int dy = pt.y - m_dragStartPt.y;
        const int thresh = ::GetSystemMetrics(SM_CXDRAG);
        if (dx * dx + dy * dy >= thresh * thresh) {
            m_dragTracking = false;
            BeginDragSelectedItems();
            return 0;
        }
    }
    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) {
        CControlUI* pFocus = m_PaintManager.GetFocus();
        const bool inEdit = (pFocus && pFocus->GetInterface(DUI_CTR_EDIT) != nullptr);
        if (wParam == VK_ESCAPE && m_addressEditMode) {
            ExitAddressEditMode(false);
            return 0;
        }
        if (!inEdit) {
            if (wParam == VK_DELETE) {
                OnDeleteClicked();
                return 0;
            }
            if (wParam == VK_F2) {
                OnRenameClicked();
                return 0;
            }
            if (wParam == VK_F5) {
                RefreshListing();
                return 0;
            }
            if (wParam == VK_BACK && !(::GetKeyState(VK_CONTROL) & 0x8000)) {
                GoBack();
                return 0;
            }
            if ((::GetKeyState(VK_MENU) & 0x8000) && wParam == VK_LEFT) {
                GoBack();
                return 0;
            }
            if ((::GetKeyState(VK_MENU) & 0x8000) && wParam == VK_RIGHT) {
                GoForward();
                return 0;
            }
            if ((::GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'C') {
                OnCopyClicked();
                return 0;
            }
            if ((::GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'V') {
                OnPasteClicked();
                return 0;
            }
            if ((::GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'A') {
                if (IsTileViewMode() && m_pIconTiles) {
                    const int n = m_pIconTiles->GetCount();
                    for (int i = 0; i < n; ++i)
                        SetIconSelected(m_pIconTiles->GetItemAt(i), true);
                    if (n > 0) m_iconAnchor = 0;
                    CDuiString tip;
                    tip.Format(_T("已选 %d 项"), n);
                    UpdateStatus(tip.GetData());
                    return 0;
                }
            }
        }
    }
    return WindowImplBase::HandleMessage(uMsg, wParam, lParam);
}

LRESULT CMainWnd::ResponseDefaultKeyEvent(WPARAM wParam)
{
    if (wParam == VK_ESCAPE) {
        if (m_addressEditMode) {
            ExitAddressEditMode(false);
            return TRUE;
        }
        CControlUI* pFocus = m_PaintManager.GetFocus();
        const bool inEdit = (pFocus && pFocus->GetInterface(DUI_CTR_EDIT) != nullptr);
        if (!inEdit && HasFileSelection()) {
            ClearFileSelection();
            UpdateListingStatusTip();
        }
        return TRUE;
    }
    return WindowImplBase::ResponseDefaultKeyEvent(wParam);
}

bool CMainWnd::HasFileSelection() const
{
    if (IsTileViewMode() && m_pIconTiles) {
        const int n = m_pIconTiles->GetCount();
        for (int i = 0; i < n; ++i) {
            CControlUI* p = m_pIconTiles->GetItemAt(i);
            if (p && (p->GetTag() & 0x100) != 0)
                return true;
        }
        return false;
    }
    if (m_pFileList) {
        const int n = m_pFileList->GetCount();
        for (int i = 0; i < n; ++i) {
            CControlUI* p = m_pFileList->GetItemAt(i);
            if (!p) continue;
            IListItemUI* li = static_cast<IListItemUI*>(p->GetInterface(DUI_CTR_ILISTITEM));
            if (li && li->IsSelected())
                return true;
        }
        if (m_pFileList->GetCurSel() >= 0)
            return true;
    }
    return false;
}

void CMainWnd::ClearFileSelection()
{
    if (IsTileViewMode()) {
        ClearIconSelection();
        m_iconAnchor = -1;
    } else if (m_pFileList) {
        m_pFileList->UnSelectAllItems();
    }
    ClearPreview();
}

bool CMainWnd::IsFileViewBlankHit(CControlUI* hit) const
{
    if (!hit)
        return false;

    bool overFileView = false;
    for (CControlUI* p = hit; p; p = p->GetParent()) {
        // Scrolling and column resizing are not blank-area gestures.
        if (p->GetInterface(DUI_CTR_SCROLLBAR)
            || p->GetInterface(DUI_CTR_LISTHEADER)
            || p->GetInterface(DUI_CTR_LISTHEADERITEM))
            return false;

        const CDuiString name = p->GetName();
        if (name == _T("file_list") || name == _T("file_icons")
            || name == _T("icon_scroll") || name == _T("list_host")) {
            overFileView = true;
            continue;
        }
        if (p->GetInterface(DUI_CTR_TREENODE) || name == _T("dir_tree")
            || name == _T("left_panel") || name == _T("left_quick")
            || name == _T("left_thispc") || name == _T("favorites_bar"))
            return false;
    }
    if (!overFileView)
        return false;

    CControlUI* listItem = FindListItemRoot(hit);
    if (listItem && !listItem->GetUserData().IsEmpty())
        return false;

    if (IsTileViewMode() && m_pIconTiles) {
        CControlUI* tile = hit;
        while (tile && tile->GetParent() != m_pIconTiles)
            tile = tile->GetParent();
        if (tile && !tile->GetUserData().IsEmpty())
            return false;
    }
    return true;
}

LRESULT CMainWnd::HandleCustomMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    if (uMsg == kMsgDetailsFill) {
        bHandled = TRUE;
        OnDetailsFillTick();
        return 0;
    }
    if (uMsg == kMsgVirtSync) {
        bHandled = TRUE;
        SyncVisibleIconWindow(false);
        return 0;
    }
    if (uMsg == kMsgReactivate) {
        bHandled = TRUE;
        BringToForeground();
        return 0;
    }
    if (uMsg == WM_ACTIVATE) {
        if (LOWORD(wParam) != WA_INACTIVE)
            EnsureMainWindowVisible();
        bHandled = FALSE;
        return 0;
    }
    if (uMsg == WM_ACTIVATEAPP) {
        if (wParam)
            EnsureMainWindowVisible();
        bHandled = FALSE;
        return 0;
    }
    if (uMsg == kMsgCopyProgress) {
        bHandled = TRUE;
        OnCopyProgressMessage();
        return 0;
    }
    if (uMsg == kMsgCopyFinished) {
        bHandled = TRUE;
        OnCopyFinishedMessage(wParam);
        return 0;
    }
    if (uMsg == kMsgThumbReady) {
        bHandled = TRUE;
        OnThumbReadyMessage(lParam);
        return 0;
    }
    return WindowImplBase::HandleCustomMessage(uMsg, wParam, lParam, bHandled);
}



std::wstring CMainWnd::GetDefaultStartPath()
{
    wchar_t docs[MAX_PATH] = {};
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs))
        && docs[0] != L'\0' && ::PathFileExistsW(docs))
        return docs;
    wchar_t profile[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
        return profile;
    return L"C:\\";
}
std::wstring CMainWnd::GetKnownFolderPath(int csidl)
{
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(::SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buf))
        && buf[0] != L'\0' && ::PathFileExistsW(buf))
        return buf;
    return {};
}

std::wstring CMainWnd::GetDownloadsPath()
{
    PWSTR p = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p)) && p) {
        std::wstring path(p);
        ::CoTaskMemFree(p);
        if (!path.empty() && ::PathFileExistsW(path.c_str()))
            return path;
    }
    // Fallback: %USERPROFILE%\Downloads
    wchar_t profile[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring path = profile;
        path += L"\\Downloads";
        if (::PathFileExistsW(path.c_str()))
            return path;
    }
    return GetKnownFolderPath(CSIDL_PERSONAL);
}

bool CMainWnd::PathEquals(const std::wstring& a, const std::wstring& b)
{
    if (a.empty() || b.empty())
        return false;
    return ::_wcsicmp(a.c_str(), b.c_str()) == 0;
}

bool CMainWnd::IsThisPcPath(const std::wstring& path)
{
    return path == kThisPcPath;
}


std::wstring CMainWnd::NormalizePath(const std::wstring& path)
{
    std::wstring trimmed = path;
    while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L'"'))
        trimmed.pop_back();
    while (!trimmed.empty() && (trimmed.front() == L' ' || trimmed.front() == L'"'))
        trimmed.erase(trimmed.begin());
    if (trimmed.empty())
        return {};

    wchar_t expanded[MAX_PATH * 4] = {};
    DWORD expLen = ::ExpandEnvironmentStringsW(trimmed.c_str(), expanded, _countof(expanded));
    const wchar_t* src = (expLen > 0 && expLen < _countof(expanded)) ? expanded : trimmed.c_str();

    wchar_t full[MAX_PATH * 4] = {};
    DWORD fullLen = ::GetFullPathNameW(src, _countof(full), full, nullptr);
    if (fullLen == 0 || fullLen >= _countof(full))
        return trimmed;

    std::wstring result(full);
    while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/'))
        result.pop_back();
    if (result.size() == 2 && result[1] == L':')
        result.push_back(L'\\');
    return result;
}

std::wstring CMainWnd::ParentPath(const std::wstring& path)
{
    if (path.empty())
        return {};
    if (path.size() == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        return {};

    std::wstring p = path;
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/'))
        p.pop_back();
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return {};
    if (pos == 2 && p[1] == L':')
        return p.substr(0, 3);
    return p.substr(0, pos);
}

std::wstring CMainWnd::FormatFileSize(ULONGLONG bytes)
{
    wchar_t buf[64] = {};
    if (bytes < 1024ULL)
        swprintf_s(buf, L"%llu B", bytes);
    else if (bytes < 1024ULL * 1024ULL)
        swprintf_s(buf, L"%.1f KB", bytes / 1024.0);
    else if (bytes < 1024ULL * 1024ULL * 1024ULL)
        swprintf_s(buf, L"%.1f MB", bytes / (1024.0 * 1024.0));
    else
        swprintf_s(buf, L"%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

std::wstring CMainWnd::GetLeafName(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return path;
    return path.substr(slash + 1);
}

void CMainWnd::PumpUiMessages()
{
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            ::PostQuitMessage(static_cast<int>(msg.wParam));
            return;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

void CMainWnd::NavigateTo(const std::wstring& path, bool addToHistory)
{
    std::wstring raw = path;
    while (!raw.empty() && (raw.front() == L' ' || raw.front() == L'"'))
        raw.erase(raw.begin());
    while (!raw.empty() && (raw.back() == L' ' || raw.back() == L'"'))
        raw.pop_back();

    // Accept "此电脑" typed in the address bar.
    if (raw == kThisPcPath || raw == L"此电脑" || ::_wcsicmp(raw.c_str(), L"This PC") == 0) {
        if (addToHistory && !m_currentPath.empty() && !IsThisPcPath(m_currentPath))
            PushHistoryBeforeNav(m_currentPath);
        m_currentPath = kThisPcPath;
        m_searchFilter.clear();
        if (m_pSearchEdit) SetSearchPlaceholder(true);
        if (m_pAddressEdit)
            m_pAddressEdit->SetText(_T("此电脑"));
        {
            ViewMode remembered = LoadFolderViewForPath(m_currentPath);
            if (m_viewMode != remembered) {
                m_viewMode = remembered;
                m_iconAnchor = -1;
                m_lastIconClickTile = nullptr;
                m_lastIconClickTick = 0;
            }
            UpdateViewModeButtons();
        }
        UpdateActiveTabPath(m_currentPath);
        RefreshListing();
        SyncTreeToPath(m_currentPath);
        UpdateFavoritesHighlight();
        UpdateNavButtons();
        if (m_addressEditMode) {
            m_addressEditMode = false;
            if (m_pAddressEditHost) m_pAddressEditHost->SetVisible(false);
            if (m_pBreadcrumb) m_pBreadcrumb->SetVisible(true);
        }
        RebuildBreadcrumb();
        ClearPreview();
        return;
    }

    std::wstring target = NormalizePath(raw);
    if (target.empty()) {
        UpdateStatus(_T("路径为空"));
        return;
    }

    DWORD attrs = ::GetFileAttributesW(target.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        CDuiString tip;
        tip.Format(_T("无法打开目录: %s"), target.c_str());
        UpdateStatus(tip.GetData());
        return;
    }

    if (addToHistory && !m_currentPath.empty() && m_currentPath != target)
        PushHistoryBeforeNav(m_currentPath);

    m_currentPath = target;
    m_searchFilter.clear();
    if (m_pSearchEdit) SetSearchPlaceholder(true);
    if (m_pAddressEdit)
        m_pAddressEdit->SetText(m_currentPath.c_str());

    {
        ViewMode remembered = LoadFolderViewForPath(m_currentPath);
        if (m_viewMode != remembered) {
            m_viewMode = remembered;
            m_iconAnchor = -1;
            m_lastIconClickTile = nullptr;
            m_lastIconClickTick = 0;
        }
        UpdateViewModeButtons();
    }

    UpdateActiveTabPath(m_currentPath);
    RefreshListing();
    SyncTreeToPath(m_currentPath);
    UpdateFavoritesHighlight();
    UpdateNavButtons();
    if (m_addressEditMode) {
        m_addressEditMode = false;
        if (m_pAddressEditHost) m_pAddressEditHost->SetVisible(false);
        if (m_pBreadcrumb) m_pBreadcrumb->SetVisible(true);
    }
    RebuildBreadcrumb();
    ClearPreview();
}

void CMainWnd::GoUp()
{
    if (IsThisPcPath(m_currentPath)) {
        UpdateStatus(_T("已在此电脑"));
        return;
    }
    std::wstring parent = ParentPath(m_currentPath);
    if (parent.empty()) {
        NavigateTo(kThisPcPath, true);
        return;
    }
    NavigateTo(parent, true);
}

void CMainWnd::PushHistoryBeforeNav(const std::wstring& fromPath)
{
    if (m_navigatingHistory) return;
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return;
    TabInfo& tab = m_tabs[m_activeTab];
    if (!fromPath.empty())
        tab.backStack.push_back(fromPath);
    tab.forwardStack.clear();
    if (tab.backStack.size() > 64)
        tab.backStack.erase(tab.backStack.begin());
}

void CMainWnd::GoBack()
{
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return;
    TabInfo& tab = m_tabs[m_activeTab];
    if (tab.backStack.empty()) {
        UpdateStatus(_T("没有后退记录"));
        return;
    }
    std::wstring dest = tab.backStack.back();
    tab.backStack.pop_back();
    if (!m_currentPath.empty())
        tab.forwardStack.push_back(m_currentPath);
    m_navigatingHistory = true;
    NavigateTo(dest, false);
    m_navigatingHistory = false;
    UpdateNavButtons();
}

void CMainWnd::GoForward()
{
    if (m_activeTab < 0 || m_activeTab >= (int)m_tabs.size()) return;
    TabInfo& tab = m_tabs[m_activeTab];
    if (tab.forwardStack.empty()) {
        UpdateStatus(_T("没有前进记录"));
        return;
    }
    std::wstring dest = tab.forwardStack.back();
    tab.forwardStack.pop_back();
    if (!m_currentPath.empty())
        tab.backStack.push_back(m_currentPath);
    m_navigatingHistory = true;
    NavigateTo(dest, false);
    m_navigatingHistory = false;
    UpdateNavButtons();
}

void CMainWnd::UpdateNavButtons()
{
    bool canBack = false, canFwd = false;
    if (m_activeTab >= 0 && m_activeTab < (int)m_tabs.size()) {
        canBack = !m_tabs[m_activeTab].backStack.empty();
        canFwd = !m_tabs[m_activeTab].forwardStack.empty();
    }
    if (m_pBtnBack) {
        m_pBtnBack->SetEnabled(canBack);
        m_pBtnBack->SetAttribute(_T("textcolor"), canBack ? _T("#FF1F2937") : _T("#FF9CA3AF"));
    }
    if (m_pBtnForward) {
        m_pBtnForward->SetEnabled(canFwd);
        m_pBtnForward->SetAttribute(_T("textcolor"), canFwd ? _T("#FF1F2937") : _T("#FF9CA3AF"));
    }
}

void CMainWnd::OnItemActivate(CControlUI* pSender)
{
    if (!pSender) return;
    CControlUI* pItem = FindListItemRoot(pSender);
    if (!pItem) return;
    CDuiString ud = pItem->GetUserData();
    if (ud.IsEmpty()) return;

    const bool isDir = (pItem->GetTag() != 0);
    std::wstring path = ud.GetData();
    if (isDir) {
        NavigateTo(path, true);
    } else {
        ::ShellExecuteW(m_hWnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        CDuiString tip;
        tip.Format(_T("已打开: %s"), path.c_str());
        UpdateStatus(tip.GetData());
    }
}

void CMainWnd::RefreshListing()
{
    if (m_currentPath.empty()) {
        UpdateStatus(_T("当前路径为空"));
        return;
    }

    CancelThumbJobs();

    if (!m_copyRunning.load())
        UpdateStatus(_T("正在枚举…"));

    if (m_pFileList)
        m_pFileList->RemoveAll();
    ClearIconView();

    std::vector<DirEntry> dirs;
    std::vector<DirEntry> files;
    bool truncated = false;

    if (IsThisPcPath(m_currentPath)) {
        wchar_t drives[512] = {};
        const DWORD n = ::GetLogicalDriveStringsW(_countof(drives) - 1, drives);
        if (n > 0 && n < _countof(drives)) {
            for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
                DirEntry e;
                e.name = FormatDriveDisplayName(p);
                e.fullPath = p;
                e.isDir = true;
                ULARGE_INTEGER available = {}, total = {}, totalFree = {};
                if (::GetDiskFreeSpaceExW(p, &available, &total, &totalFree)) {
                    e.size = available.QuadPart;
                    e.capacity = total.QuadPart;
                }
                e.mtime = 0;
                e.attrs = FILE_ATTRIBUTE_DIRECTORY;
                if (EntryMatchesFilter(e))
                    dirs.push_back(std::move(e));
            }
        }
        std::stable_sort(dirs.begin(), dirs.end(), [](const DirEntry& a, const DirEntry& b) {
            const wchar_t da = a.fullPath.empty() ? L'Z' : static_cast<wchar_t>(::towupper(a.fullPath[0]));
            const wchar_t db = b.fullPath.empty() ? L'Z' : static_cast<wchar_t>(::towupper(b.fullPath[0]));
            if (da == L'C') return db != L'C';
            if (db == L'C') return false;
            return da < db;
        });
    } else if (IsRecursiveSearch() && !m_searchFilter.empty()) {
        CollectRecursiveMatches(m_currentPath, m_searchFilter, dirs, files, truncated);
    } else {
        std::wstring pattern = m_currentPath;
        if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
            pattern.push_back(L'\\');
        pattern += L"*";

        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = ::FindFirstFileExW(
            pattern.c_str(), FindExInfoBasic, &fd,
            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);

        if (hFind == INVALID_HANDLE_VALUE) {
            CDuiString tip;
            tip.Format(_T("枚举失败 (%lu): %s"), ::GetLastError(), m_currentPath.c_str());
            UpdateStatus(tip.GetData());
            m_hasListingCache = false;
            return;
        }

        dirs.reserve(256);
        files.reserve(1024);
        int scanned = 0;
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;

            DirEntry e;
            e.name = fd.cFileName;
            e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            e.size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            e.mtime = FileTimeToU64(fd.ftLastWriteTime);
            e.attrs = fd.dwFileAttributes;
            e.fullPath = m_currentPath;
            if (!e.fullPath.empty() && e.fullPath.back() != L'\\' && e.fullPath.back() != L'/')
                e.fullPath.push_back(L'\\');
            e.fullPath += e.name;

            if (ShouldHideByAttributes(e.attrs))
                continue;
            if (!EntryMatchesFilter(e))
                continue;

            if (e.isDir) dirs.push_back(std::move(e));
            else files.push_back(std::move(e));

            ++scanned;
            if ((scanned % kPumpEvery) == 0)
                PumpUiMessages();

            if (static_cast<int>(dirs.size() + files.size()) >= kMaxListItems) {
                truncated = true;
                break;
            }
        } while (::FindNextFileW(hFind, &fd));
        ::FindClose(hFind);
    }

    StoreListingCache(std::move(dirs), std::move(files), truncated);
    SortListingCache();

    if (IsTileViewMode())
        RebuildIconsView(m_listingDirs, m_listingFiles, m_listingTruncated);
    else
        RebuildDetailsView(m_listingDirs, m_listingFiles, m_listingTruncated);

    if (m_copyRunning.load()) {
        OnCopyProgressMessage();
        return;
    }

    UpdateListingStatusTip();
}

void CMainWnd::StoreListingCache(std::vector<DirEntry> dirs, std::vector<DirEntry> files, bool truncated)
{
    m_listingDirs = std::move(dirs);
    m_listingFiles = std::move(files);
    m_listingTruncated = truncated;
    m_listingPath = m_currentPath;
    m_listingFilter = m_searchFilter;
    m_listingRecursive = IsRecursiveSearch();
    m_hasListingCache = true;
}

void CMainWnd::UpdateListingStatusTip()
{
    if (m_copyRunning.load())
        return;
    const int shown = static_cast<int>(m_listingDirs.size() + m_listingFiles.size());
    CDuiString tip;
    if (!m_searchFilter.empty()) {
        if (IsRecursiveSearch())
            tip.Format(_T("递归搜索 \"%s\"  ·  %d 项%s"), m_searchFilter.c_str(), shown,
                m_listingTruncated ? _T("（已截断）") : _T(""));
        else
            tip.Format(_T("筛选 \"%s\"  ·  %d 项"), m_searchFilter.c_str(), shown);
    } else if (IsThisPcPath(m_currentPath)) {
        tip.Format(_T("此电脑  ·  %d 个驱动器"), shown);
    } else if (m_listingTruncated) {
        tip.Format(_T("%s  ·  显示 %d 项（已截断）"), m_currentPath.c_str(), shown);
    } else {
        tip.Format(_T("%s  ·  %d 文件夹 / %d 文件"),
            m_currentPath.c_str(),
            static_cast<int>(m_listingDirs.size()),
            static_cast<int>(m_listingFiles.size()));
    }

    // Selected / clipboard counts - separator spacing from StatusCountSepPad
    {
        CDuiString sep;
        {
            const int n = UiTokens::StatusCountSepPad / 4; // design spaces (~SpaceSm -> 2)
            const int spaces = n > 0 ? n : 1;
            sep = _T(" ");
            for (int i = 1; i < spaces; ++i) sep += _T(" ");
            sep += _T("|");
            for (int i = 0; i < spaces; ++i) sep += _T(" ");
        }
        std::vector<ClipboardItem> sel;
        CollectSelectedItems(sel);
        if (!sel.empty()) {
            CDuiString selTip;
            selTip.Format(_T("%s已选 %d 项"), sep.GetData(), static_cast<int>(sel.size()));
            tip += selTip;
        }
        if (!m_clipboard.empty()) {
            CDuiString clip;
            clip.Format(_T("%s剪贴板 %d 项"), sep.GetData(), static_cast<int>(m_clipboard.size()));
            tip += clip;
        }
    }
    UpdateStatus(tip.GetData());
}

void CMainWnd::RebuildCurrentViewFromCache()
{
    if (!m_hasListingCache)
        return;
    CancelThumbJobs();
    if (m_pFileList)
        m_pFileList->RemoveAll();
    // ClearIconView 会在非复用路径里调用；图标尺寸切换尽量 TryReuse
    if (IsTileViewMode())
        RebuildIconsView(m_listingDirs, m_listingFiles, m_listingTruncated);
    else {
        ClearIconView();
        RebuildDetailsView(m_listingDirs, m_listingFiles, m_listingTruncated);
    }
    UpdateListingStatusTip();
}

void CMainWnd::EnsureMainWindowVisible()
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
    if (!::IsWindowVisible(m_hWnd))
        ::ShowWindow(m_hWnd, SW_SHOW);
    if (::IsIconic(m_hWnd))
        ::ShowWindow(m_hWnd, SW_RESTORE);
    ::SetWindowPos(m_hWnd, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
}

void CMainWnd::BringToForeground()
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
    EnsureMainWindowVisible();
    if (::IsIconic(m_hWnd))
        ::ShowWindow(m_hWnd, SW_RESTORE);
    else
        ::ShowWindow(m_hWnd, SW_SHOW);

    HWND hFg = ::GetForegroundWindow();
    DWORD ourTid = ::GetCurrentThreadId();
    DWORD fgTid = 0;
    BOOL attached = FALSE;
    if (hFg && hFg != m_hWnd) {
        fgTid = ::GetWindowThreadProcessId(hFg, nullptr);
        if (fgTid != 0 && fgTid != ourTid)
            attached = ::AttachThreadInput(ourTid, fgTid, TRUE);
    }
    ::BringWindowToTop(m_hWnd);
    ::SetWindowPos(m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ::SetForegroundWindow(m_hWnd);
    ::SetActiveWindow(m_hWnd);
    ::SetFocus(m_hWnd);
    if (attached)
        ::AttachThreadInput(ourTid, fgTid, FALSE);
    if (::GetForegroundWindow() != m_hWnd) {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_MENU;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = VK_MENU;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        ::SendInput(2, inputs, sizeof(INPUT));
        ::SetForegroundWindow(m_hWnd);
        ::BringWindowToTop(m_hWnd);
    }
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
    ::UpdateWindow(m_hWnd);
    UpdateStatus(_T("已恢复前台显示"));
}

void CMainWnd::UpdateStatus(LPCTSTR text)
{
    if (m_pStatus && text)
        m_pStatus->SetText(text);
}

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


void CMainWnd::OnFavoriteClicked(const CDuiString& name)
{
    if (name == _T("fav_thispc")) {
        AddTab(kThisPcPath, true);
        return;
    }
    if (name == _T("fav_documents")) {
        std::wstring p = GetKnownFolderPath(CSIDL_PERSONAL);
        if (p.empty()) { UpdateStatus(_T("无法定位文档文件夹")); return; }
        AddTab(p, true);
        return;
    }
    if (name == _T("fav_desktop")) {
        std::wstring p = GetKnownFolderPath(CSIDL_DESKTOPDIRECTORY);
        if (p.empty()) { UpdateStatus(_T("无法定位桌面")); return; }
        AddTab(p, true);
        return;
    }
    if (name == _T("fav_downloads")) {
        std::wstring p = GetDownloadsPath();
        if (p.empty()) { UpdateStatus(_T("无法定位下载文件夹")); return; }
        AddTab(p, true);
        return;
    }
}

void CMainWnd::UpdateFavoritesHighlight()
{
    const std::wstring docs = GetKnownFolderPath(CSIDL_PERSONAL);
    const std::wstring desk = GetKnownFolderPath(CSIDL_DESKTOPDIRECTORY);
    const std::wstring downs = GetDownloadsPath();

    struct FavBtn { LPCTSTR name; bool active; };
    const FavBtn btns[] = {
        { _T("fav_thispc"), IsThisPcPath(m_currentPath) },
        { _T("fav_documents"), PathEquals(m_currentPath, docs) },
        { _T("fav_desktop"), PathEquals(m_currentPath, desk) },
        { _T("fav_downloads"), PathEquals(m_currentPath, downs) },
    };

    for (const auto& b : btns) {
        auto* btn = static_cast<CButtonUI*>(m_PaintManager.FindControl(b.name));
        if (!btn) continue;
        if (b.active) {
            btn->SetAttribute(_T("bkcolor"), UiTokens::ColorNavSelected);
            btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
            btn->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
            btn->SetAttribute(_T("bordersize"), _T("0"));
        } else {
            btn->SetAttribute(_T("bkcolor"), UiTokens::ColorSurface);
            btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
            btn->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
            btn->SetAttribute(_T("bordersize"), _T("0"));
        }
        btn->Invalidate();
    }

    auto stylePin = [&](CContainerUI* host) {
        if (!host) return;
        const int n = host->GetCount();
        for (int i = 0; i < n; ++i) {
            CControlUI* c = host->GetItemAt(i);
            if (!c) continue;
            CDuiString ud = c->GetUserData();
            const bool active = !ud.IsEmpty() && PathEquals(m_currentPath, ud.GetData());
            if (active) {
                c->SetAttribute(_T("bkcolor"), UiTokens::ColorNavSelected);
                c->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
            } else {
                CDuiString nm = c->GetName();
                if (nm.Find(_T("fav_dyn_")) == 0)
                    c->SetAttribute(_T("bkcolor"), UiTokens::ColorSurface);
                else
                    c->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
                c->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
            }
            c->Invalidate();
        }
    };
    stylePin(m_pFavoritesStrip);
    stylePin(m_pLeftFavPins);
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

void CMainWnd::ShowToolbarPopupMenu(CControlUI* anchor, HMENU hMenu)
{
    if (!anchor || !hMenu || !m_hWnd) {
        if (hMenu) ::DestroyMenu(hMenu);
        return;
    }
    RECT rc = anchor->GetPos();
    POINT pt = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &pt);
    ::TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        pt.x, pt.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
}

void CMainWnd::OnNewMenuClicked()
{
    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;
    ::AppendMenuW(hMenu, MF_STRING, 1, L"新建文件夹");
    CControlUI* anchor = m_PaintManager.FindControl(_T("btn_new"));
    if (!anchor) anchor = m_PaintManager.FindControl(_T("btn_newfolder"));

    if (!anchor || !m_hWnd) {
        ::DestroyMenu(hMenu);
        OnNewFolderClicked();
        return;
    }
    RECT rc = anchor->GetPos();
    POINT pt = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &pt);
    UINT cmd = ::TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
        pt.x, pt.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
    if (cmd == 1)
        OnNewFolderClicked();
}

void CMainWnd::OnSortMenuClicked()
{
    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;
    auto check = [&](SortColumn col) -> UINT {
        return (m_sortColumn == col) ? (MF_STRING | MF_CHECKED) : MF_STRING;
    };
    ::AppendMenuW(hMenu, check(SortColumn::Name), 1, L"名称");
    ::AppendMenuW(hMenu, check(SortColumn::Modified), 2, L"修改日期");
    ::AppendMenuW(hMenu, check(SortColumn::Type), 3, L"类型");
    ::AppendMenuW(hMenu, check(SortColumn::Size), 4, L"大小");
    ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(hMenu, m_sortAscending ? (MF_STRING | MF_CHECKED) : MF_STRING, 5, L"升序");
    ::AppendMenuW(hMenu, !m_sortAscending ? (MF_STRING | MF_CHECKED) : MF_STRING, 6, L"降序");

    CControlUI* anchor = m_PaintManager.FindControl(_T("btn_sort"));
    if (!anchor || !m_hWnd) {
        ::DestroyMenu(hMenu);
        return;
    }
    RECT rc = anchor->GetPos();
    POINT pt = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &pt);
    UINT cmd = ::TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
        pt.x, pt.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
    if (cmd == 0) return;
    if (cmd >= 1 && cmd <= 4) {
        SortColumn col = static_cast<SortColumn>(cmd - 1);
        if (m_sortColumn == col)
            m_sortAscending = !m_sortAscending;
        else {
            m_sortColumn = col;
            m_sortAscending = true;
        }
    } else if (cmd == 5) {
        m_sortAscending = true;
    } else if (cmd == 6) {
        m_sortAscending = false;
    }
    SortListingCache();
    UpdateHeaderSortIndicators();
    RebuildCurrentViewFromCache();
}

void CMainWnd::OnViewMenuClicked()
{
    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;
    auto check = [&](ViewMode m) -> UINT {
        return (m_viewMode == m) ? (MF_STRING | MF_CHECKED) : MF_STRING;
    };
    ::AppendMenuW(hMenu, check(ViewMode::ExtraLargeIcons), 1, L"超大图标");
    ::AppendMenuW(hMenu, check(ViewMode::LargeIcons), 2, L"大图标");
    ::AppendMenuW(hMenu, check(ViewMode::MediumIcons), 3, L"中等图标");
    ::AppendMenuW(hMenu, check(ViewMode::List), 4, L"列表");
    ::AppendMenuW(hMenu, check(ViewMode::Details), 5, L"详细信息");
    ::AppendMenuW(hMenu, check(ViewMode::Tiles), 6, L"平铺");
    ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(hMenu, m_previewVisible ? (MF_STRING | MF_CHECKED) : MF_STRING, 7, L"预览窗格");

    CControlUI* anchor = m_PaintManager.FindControl(_T("btn_view_menu"));
    if (!anchor || !m_hWnd) {
        ::DestroyMenu(hMenu);
        return;
    }
    RECT rc = anchor->GetPos();
    POINT pt = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &pt);
    UINT cmd = ::TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
        pt.x, pt.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
    switch (cmd) {
    case 1: SetViewMode(ViewMode::ExtraLargeIcons); break;
    case 2: SetViewMode(ViewMode::LargeIcons); break;
    case 3: SetViewMode(ViewMode::MediumIcons); break;
    case 4: SetViewMode(ViewMode::List); break;
    case 5: SetViewMode(ViewMode::Details); break;
    case 6: SetViewMode(ViewMode::Tiles); break;
    case 7:
        SetPreviewVisible(!m_previewVisible);
        if (m_previewVisible) UpdatePreviewForSelection();
        break;
    default: break;
    }
}

void CMainWnd::OnMoreMenuClicked()
{
    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;
    ::AppendMenuW(hMenu, MF_STRING, 1, L"刷新");
    ::AppendMenuW(hMenu, m_previewVisible ? (MF_STRING | MF_CHECKED) : MF_STRING,
        2, L"预览窗格");
    ::AppendMenuW(hMenu, m_showHidden ? (MF_STRING | MF_CHECKED) : MF_STRING,
        3, L"显示隐藏的项目");

    CControlUI* anchor = m_PaintManager.FindControl(_T("btn_more"));
    if (!anchor || !m_hWnd) {
        ::DestroyMenu(hMenu);
        return;
    }
    RECT rc = anchor->GetPos();
    POINT pt = { rc.left, rc.bottom };
    ::ClientToScreen(m_hWnd, &pt);
    const UINT cmd = ::TrackPopupMenuEx(hMenu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
        pt.x, pt.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
    if (cmd == 1) {
        RefreshListing();
    } else if (cmd == 2) {
        SetPreviewVisible(!m_previewVisible);
        if (m_previewVisible) UpdatePreviewForSelection();
    } else if (cmd == 3) {
        ToggleShowHidden();
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

// ---- A: visible vertical scrollbars --------------------------------------

void CMainWnd::StyleVerticalScrollBar(CContainerUI* host)
{
    if (!host) return;
    CScrollBarUI* sb = host->GetVerticalScrollBar();
    if (!sb) {
        host->EnableScrollBar(true, host->GetHorizontalScrollBar() != nullptr);
        sb = host->GetVerticalScrollBar();
    }
    if (!sb) return;

    const int w = (std::max)(DpiScale(UiTokens::ScrollBarW), 8);
    sb->SetFixedWidth(w);
    sb->SetShowButton1(false);
    sb->SetShowButton2(false);
    sb->SetAttribute(_T("bkcolor"), UiTokens::ColorScrollTrack);
    sb->SetThumbColor(0xFFC4C4C4); // ColorScrollThumb #FFC4C4C4
    sb->SetAttribute(_T("button1color"), UiTokens::ColorScrollTrack);
    sb->SetAttribute(_T("button2color"), UiTokens::ColorScrollTrack);
}

void CMainWnd::ApplyFileViewScrollBars()
{
    if (m_pFileList)
        StyleVerticalScrollBar(m_pFileList);
    if (m_pIconTiles)
        StyleVerticalScrollBar(m_pIconTiles);
    if (m_pIconScroll)
        m_pIconScroll->EnableScrollBar(false, false);
}

// ---- C: left Quick Access / This PC splitter -----------------------------

std::wstring CMainWnd::GetLeftNavFilePath()
{
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir;
    if (n > 0 && n < MAX_PATH)
        dir = appdata;
    else
        dir = L".";
    dir += L"\\FastFile";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\left_nav.ini";
}

void CMainWnd::ApplyLeftNavSplitterHeight(int designHeight)
{
    if (designHeight < UiTokens::LeftQuickMinH) designHeight = UiTokens::LeftQuickMinH;
    if (designHeight > 720) designHeight = 720;
    m_leftQuickDesignH = designHeight;
    if (m_pLeftQuick) {
        // DuiLib's native layout splitter is reliable for child hit testing;
        // the former custom label grip was not receiving all mouse messages.
        m_pLeftQuick->SetSepHeight(DpiScale(UiTokens::LeftNavSepH));
        m_pLeftQuick->SetMinHeight(DpiScale(UiTokens::LeftQuickMinH));
        m_pLeftQuick->SetMaxHeight(DpiScale(720));
        m_pLeftQuick->SetFixedHeight(DpiScale(designHeight));
        m_pLeftQuick->NeedParentUpdate();
    }
}

void CMainWnd::LoadLeftNavSplitter()
{
    int h = m_leftQuickDesignH;
    std::wstring file = GetLeftNavFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, file.c_str(), L"rb") == 0 && fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz >= 4) {
            std::wstring content;
            content.resize(sz / sizeof(wchar_t));
            fread(&content[0], 1, sz, fp);
            if (!content.empty() && content[0] == 0xFEFF)
                content.erase(content.begin());
            size_t pos = 0;
            while (pos < content.size()) {
                size_t eol = content.find(L'\n', pos);
                if (eol == std::wstring::npos) eol = content.size();
                std::wstring line = content.substr(pos, eol - pos);
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                pos = eol + 1;
                if (line.compare(0, 16, L"LeftQuickHeight=") == 0)
                    h = _wtoi(line.c_str() + 16);
            }
        }
        fclose(fp);
    }
    ApplyLeftNavSplitterHeight(h);
}

void CMainWnd::SaveLeftNavSplitter() const
{
    if (m_pLeftQuick) {
        const int phy = m_pLeftQuick->GetFixedHeight();
        if (phy > 0 && m_dpi > 0) {
            const_cast<CMainWnd*>(this)->m_leftQuickDesignH =
                (std::max)(UiTokens::LeftQuickMinH, ::MulDiv(phy, 96, static_cast<int>(m_dpi)));
        }
    }
    std::wstring path = GetLeftNavFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || !fp)
        return;
    unsigned char bom[2] = { 0xFF, 0xFE };
    fwrite(bom, 1, 2, fp);
    wchar_t buf[128];
    swprintf_s(buf, L"[LeftNav]\nLeftQuickHeight=%d\n", m_leftQuickDesignH);
    fwrite(buf, sizeof(wchar_t), wcslen(buf), fp);
    fclose(fp);
}

void CMainWnd::CaptureLeftNavSplitterIfChanged()
{
    if (!m_pLeftQuick) return;
    const int phy = m_pLeftQuick->GetFixedHeight();
    if (phy <= 0 || m_dpi == 0) return;
    const int design = (std::max)(UiTokens::LeftQuickMinH, ::MulDiv(phy, 96, static_cast<int>(m_dpi)));
    if (design != m_leftQuickDesignH) {
        m_leftQuickDesignH = design;
        SaveLeftNavSplitter();
    }
}


std::wstring CMainWnd::FormatModifiedTime(ULONGLONG ft)
{
    return FormatFileTimeULONGLONG(ft);
}

std::wstring CMainWnd::FormatDriveDisplayName(const std::wstring& rootPath)
{
    std::wstring root = rootPath;
    if (root.empty())
        return L"本地磁盘";
    if (root.back() != L'\\' && root.back() != L'/')
        root.push_back(L'\\');
    wchar_t label[MAX_PATH + 1] = {};
    wchar_t fsName[MAX_PATH + 1] = {};
    DWORD serial = 0, maxComp = 0, flags = 0;
    const BOOL ok = ::GetVolumeInformationW(root.c_str(), label, MAX_PATH,
        &serial, &maxComp, &flags, fsName, MAX_PATH);
    const wchar_t letter = static_cast<wchar_t>(::towupper(root[0]));
    const std::wstring name = (ok && label[0] != L'\0') ? label : L"本地磁盘";
    wchar_t buf[MAX_PATH + 64] = {};
    swprintf_s(buf, L"%s (%c:)", name.c_str(), letter);
    return buf;
}

bool CMainWnd::ShouldHideByAttributes(DWORD attrs) const
{
    if (m_showHidden)
        return false;
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false;
    return (attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
}

void CMainWnd::SetShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    const std::wstring path = m_currentPath;
    InitDirectoryTree();
    if (!path.empty())
        SyncTreeToPath(path);
    RefreshListing();
    SaveSession();
    UpdateStatus(m_showHidden ? _T("已显示隐藏的项目") : _T("已隐藏隐藏的项目"));
}

void CMainWnd::ToggleShowHidden()
{
    SetShowHidden(!m_showHidden);
}

void CMainWnd::ApplyWindowCornerAndPadding()
{
    if (!m_hWnd || !::IsWindow(m_hWnd))
        return;
    // Win11 DWM rounded corners (ignored on older Windows)
    DWORD pref = DWMWCP_ROUND;
    ::DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    // Soften outer gutter padding with DPI (token OuterGutter=14, was 8)
    if (CControlUI* gutter = m_PaintManager.FindControl(_T("outer_gutter"))) {
        CDuiString pad;
        const int g = DpiScale(UiTokens::OuterGutter);
        pad.Format(_T("%d,%d,%d,%d"), g, g, g, g);
        gutter->SetAttribute(_T("padding"), pad);
        gutter->SetAttribute(_T("bkcolor"), UiTokens::ColorGutter);
    }
    if (CControlUI* chrome = m_PaintManager.FindControl(_T("chrome_root"))) {
        SIZE roundSz = { DpiScale(UiTokens::ChromeRound), DpiScale(UiTokens::ChromeRound) };
        chrome->SetBorderRound(roundSz);
        chrome->SetAttribute(_T("bkcolor"), UiTokens::ColorSurface);
        chrome->SetAttribute(_T("bordercolor"), UiTokens::ColorBorderStrong);
    }
    ApplyUiChromeTokens();
}

void CMainWnd::ApplyUiChromeTokens()
{
    // Phase 1: DPI-scaled inner paddings + unify tab/toolbar/breadcrumb to Win11 light surface.
    auto setPad = [&](LPCTSTR name, int l, int t, int r, int b) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) return;
        CDuiString pad;
        pad.Format(_T("%d,%d,%d,%d"),
            DpiScale(l), DpiScale(t), DpiScale(r), DpiScale(b));
        c->SetAttribute(_T("padding"), pad);
    };
    auto setBk = [&](LPCTSTR name, LPCWSTR color) {
        if (CControlUI* c = m_PaintManager.FindControl(name))
            c->SetAttribute(_T("bkcolor"), color);
    };
    auto setBorder = [&](LPCTSTR name, LPCWSTR color, LPCTSTR size) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) return;
        c->SetAttribute(_T("bordercolor"), color);
        c->SetAttribute(_T("bordersize"), size);
    };

    const int px = UiTokens::InnerPadX;
    const int py = UiTokens::InnerPadY;

    setPad(_T("title_bar"), UiTokens::SpaceSm, 0, 0, 0);
    setPad(_T("tab_bar"), px, py, px, 0);
    setPad(_T("toolbar"), px, py, px, py);
    setPad(_T("favorites_bar"), px, UiTokens::FavBarPadY, px, UiTokens::FavBarPadY);
    setPad(_T("address_bar"), px, UiTokens::AddressBarPadY, px, UiTokens::AddressBarPadY);
    setPad(_T("left_panel"), UiTokens::SpaceSm, UiTokens::SpaceSm, UiTokens::SpaceSm, UiTokens::SpaceSm);
    setPad(_T("icon_scroll"), px, px, px, px);
    setPad(_T("preview_pane"), UiTokens::PreviewPad, UiTokens::PreviewPad, UiTokens::PreviewPad, UiTokens::PreviewPad);

    // Unified Win11 light surface for command bar / tabs / breadcrumb / address / favorites
    const LPCWSTR surf = UiTokens::ColorSurface;
    const LPCWSTR border = UiTokens::ColorBorder;
    for (LPCTSTR band : {
        _T("title_bar"), _T("tab_bar"), _T("favorites_bar"), _T("toolbar"),
        _T("address_bar"),
        _T("status_bar"), _T("left_panel")
    }) {
        setBk(band, surf);
        setBorder(band, border, _T("0,0,0,1"));
    }
    // status_bar top border only
    setBorder(_T("status_bar"), UiTokens::ColorBorderStrong, _T("0,1,0,0"));
    setBorder(_T("left_panel"), border, _T("0,0,1,0"));
    setBorder(_T("preview_pane"), _T("#00000000"), _T("0"));
    setBk(_T("preview_pane"), UiTokens::ColorContent);
    // breadcrumb_bar removed (merged into address_bar)

    // Body / list stay content white
    setBk(_T("body_host"), UiTokens::ColorContent);
    setBk(_T("list_host"), UiTokens::ColorContent);
    if (m_pFileList)
        m_pFileList->SetAttribute(_T("bkcolor"), UiTokens::ColorContent);

    // Toolbar group gaps use SpaceSm
    for (LPCTSTR gap : {
        _T("gap_clip"), _T("gap_clip2"), _T("gap_org"), _T("gap_org2"),
        _T("gap_more"), _T("gap_more2")
    }) {
        if (CControlUI* c = m_PaintManager.FindControl(gap))
            c->SetFixedWidth(DpiScale(UiTokens::GapGroup));
    }
    for (LPCTSTR sep : { _T("sep_new"), _T("sep_organize"), _T("sep_more") }) {
        if (CControlUI* c = m_PaintManager.FindControl(sep)) {
            c->SetFixedWidth(DpiScale(1));
            c->SetFixedHeight(DpiScale(UiTokens::SepH));
            c->SetAttribute(_T("bkcolor"), UiTokens::ColorSeparator);
        }
    }

    // Phase 2: details list hover/select + header chrome-aligned surface
    if (m_pFileList) {
        m_pFileList->SetAttribute(_T("itemhotbkcolor"), UiTokens::ColorListHover);
        m_pFileList->SetAttribute(_T("itemselectedbkcolor"), UiTokens::ColorListSelected);
        m_pFileList->SetAttribute(_T("itemtextcolor"), UiTokens::ColorTextPrimary);
        m_pFileList->SetAttribute(_T("itemselectedtextcolor"), UiTokens::ColorTextPrimary);
        m_pFileList->SetAttribute(_T("headerbkcolor"), UiTokens::ColorListHeaderBg);
        if (CListHeaderUI* hdr = m_pFileList->GetHeader()) {
            hdr->SetAttribute(_T("bkcolor"), UiTokens::ColorListHeaderBg);
            hdr->SetAttribute(_T("bordercolor"), UiTokens::ColorBorder);
            hdr->SetAttribute(_T("bordersize"), _T("0,0,0,1"));
            hdr->SetFixedHeight(DpiScale(UiTokens::DetailsHeaderH));
            const int n = hdr->GetCount();
            for (int i = 0; i < n; ++i) {
                if (CControlUI* hi = hdr->GetItemAt(i)) {
                    hi->SetAttribute(_T("textcolor"), UiTokens::ColorTextSecondary);
                    hi->SetAttribute(_T("sepcolor"), UiTokens::ColorBorder);
                }
            }
        }
    }

    // Phase 2: left nav surface + section headers + tree interaction colors
    setBk(_T("left_quick"), surf);
    setBk(_T("left_thispc"), surf);
    if (m_pDirTree) {
        m_pDirTree->SetAttribute(_T("bkcolor"), surf);
        m_pDirTree->SetAttribute(_T("itemhotbkcolor"), UiTokens::ColorListHover);
        m_pDirTree->SetAttribute(_T("itemselectedbkcolor"), UiTokens::ColorListSelected);
        m_pDirTree->SetAttribute(_T("itemtextcolor"), UiTokens::ColorTextPrimary);
        m_pDirTree->SetAttribute(_T("itemhottextcolor"), UiTokens::ColorTextPrimary);
        m_pDirTree->SetAttribute(_T("selitemtextcolor"), UiTokens::ColorTextPrimary);
        m_pDirTree->SetAttribute(_T("selitemhottextcolor"), UiTokens::ColorTextPrimary);
    }
    for (LPCTSTR hdrName : { _T("nav_hdr_quick"), _T("nav_hdr_thispc") }) {
        if (CControlUI* h = m_PaintManager.FindControl(hdrName)) {
            h->SetFixedHeight(DpiScale(UiTokens::NavSectionHeaderH));
            h->SetAttribute(_T("textcolor"), UiTokens::ColorNavSection);
            h->SetAttribute(_T("font"), _T("3"));  // FontCaption — Explorer section density
            CDuiString pad;
            pad.Format(_T("%d,%d,0,0"), DpiScale(UiTokens::NavHeaderPadL), DpiScale(UiTokens::SpaceXs));
            h->SetAttribute(_T("padding"), pad);
        }
    }
    for (LPCTSTR favName : {
        _T("fav_thispc"), _T("fav_documents"), _T("fav_desktop"), _T("fav_downloads")
    }) {
        if (CControlUI* b = m_PaintManager.FindControl(favName)) {
            b->SetFixedHeight(DpiScale(UiTokens::NavRowH));
            b->SetAttribute(_T("bkcolor"), surf);
            b->SetAttribute(_T("hotbkcolor"), UiTokens::ColorNavHover);
            b->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorNavSelected);
            b->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
            CDuiString pad;
            pad.Format(_T("%d,0,%d,0"), DpiScale(UiTokens::NavIconPad), DpiScale(UiTokens::NavIconPad));
            b->SetAttribute(_T("padding"), pad);
        }
    }

    // Phase 3: preview pane density + Surface header chrome; status bar density
    setPad(_T("preview_pane"), UiTokens::PreviewPad, UiTokens::PreviewPad,
        UiTokens::PreviewPad, UiTokens::PreviewPad);
    setPad(_T("status_bar"), UiTokens::StatusPadX, UiTokens::StatusPadY,
        UiTokens::StatusPadX, UiTokens::StatusPadY);
    setBorder(_T("status_bar"), UiTokens::ColorBorderStrong, _T("0,1,0,0"));
    if (CControlUI* st = m_PaintManager.FindControl(_T("status_text"))) {
        st->SetAttribute(_T("textcolor"), UiTokens::ColorTextSecondary);
        st->SetAttribute(_T("font"), _T("2"));  // FontSmall
        st->SetAttribute(_T("padding"), _T("0,0,0,0"));
    }
    if (m_pPreviewTitle) {
        m_pPreviewTitle->SetFixedHeight(DpiScale(UiTokens::PreviewTitleH));
        m_pPreviewTitle->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        m_pPreviewTitle->SetAttribute(_T("font"), _T("5"));  // FontPreviewTitle
        m_pPreviewTitle->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
    }
    if (CControlUI* fl = m_PaintManager.FindControl(_T("fav_bar_label"))) {
        fl->SetAttribute(_T("font"), _T("0"));
        fl->SetAttribute(_T("textcolor"), UiTokens::ColorTextSecondary);
        fl->SetAttribute(_T("valign"), _T("vcenter"));
    }
    if (CControlUI* fh = m_PaintManager.FindControl(_T("fav_bar_hint"))) {
        fh->SetAttribute(_T("font"), _T("0"));
        fh->SetAttribute(_T("valign"), _T("vcenter"));
    }
    // Gap spacers DPI-scaled (also in ApplyDpiScaledChrome; re-apply after chrome token pass)
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_title"), 0, UiTokens::PreviewTitleGap, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_action"), 0, UiTokens::SpaceSm + 2, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_image"), 0, UiTokens::PreviewImageGap, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_text"), 0, UiTokens::PreviewTextGap, m_dpi);
    for (LPCTSTR lblName : {
        _T("preview_lbl_type"), _T("preview_lbl_size"), _T("preview_lbl_location"),
        _T("preview_lbl_mtime"), _T("preview_lbl_ctime"), _T("preview_lbl_dimensions"),
        _T("preview_lbl_duration"), _T("preview_lbl_framerate"), _T("preview_lbl_bitrate"),
        _T("preview_lbl_totalbitrate")
    }) {
        if (CControlUI* lbl = m_PaintManager.FindControl(lblName)) {
            lbl->SetFixedWidth(DpiScale(UiTokens::PreviewMetaLabelW));
            lbl->SetAttribute(_T("textcolor"), UiTokens::ColorTextMuted);
            lbl->SetAttribute(_T("font"), _T("0"));  // FontPreviewMeta
        }
    }
    for (LPCTSTR valName : {
        _T("preview_type"), _T("preview_size"), _T("preview_location"),
        _T("preview_mtime"), _T("preview_ctime"), _T("preview_dimensions"),
        _T("preview_duration"), _T("preview_framerate"), _T("preview_bitrate"),
        _T("preview_totalbitrate"), _T("preview_text")
    }) {
        if (CControlUI* v = m_PaintManager.FindControl(valName)) {
            v->SetAttribute(_T("font"), _T("0"));  // FontPreviewMeta
            if (_tcscmp(valName, _T("preview_text")) == 0)
                v->SetAttribute(_T("textcolor"), UiTokens::ColorTextMuted);
            else
                v->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        }
    }
    for (LPCTSTR rowName : {
        _T("preview_row_type"), _T("preview_row_size"), _T("preview_row_location"),
        _T("preview_row_mtime"), _T("preview_row_ctime"), _T("preview_row_dimensions"),
        _T("preview_row_duration"), _T("preview_row_framerate"), _T("preview_row_bitrate"),
        _T("preview_row_totalbitrate")
    }) {
        if (CControlUI* row = m_PaintManager.FindControl(rowName))
            row->SetFixedHeight(DpiScale(UiTokens::PreviewMetaRowH));
    }
    if (m_pPreviewImage) {
        m_pPreviewImage->SetFixedHeight(DpiScale(UiTokens::PreviewImageH));
        m_pPreviewImage->SetAttribute(_T("bkcolor"), UiTokens::ColorContent);
        m_pPreviewImage->SetAttribute(_T("bordercolor"), _T("#00000000"));
        m_pPreviewImage->SetAttribute(_T("bordersize"), _T("0"));
        CDuiString br;
        br.Format(_T("%d,%d"), DpiScale(UiTokens::PreviewImageRound), DpiScale(UiTokens::PreviewImageRound));
        m_pPreviewImage->SetAttribute(_T("borderround"), br.GetData());
    }
    if (CControlUI* pane = m_PaintManager.FindControl(_T("preview_pane"))) {
        CDuiString br;
        br.Format(_T("%d,%d"), DpiScale(UiTokens::PreviewRound), DpiScale(UiTokens::PreviewRound));
        pane->SetAttribute(_T("borderround"), br.GetData());
        pane->SetAttribute(_T("bordercolor"), _T("#00000000"));
        pane->SetAttribute(_T("bordersize"), _T("0"));
    }
}

void CMainWnd::ForwardShellMenuMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* pResult, bool* handled)
{
    if (handled) *handled = false;
    if (m_pCtxMenu3) {
        if (uMsg == WM_MENUCHAR) {
            LRESULT lr = 0;
            if (SUCCEEDED(m_pCtxMenu3->HandleMenuMsg2(uMsg, wParam, lParam, &lr))) {
                if (pResult) *pResult = lr;
                if (handled) *handled = true;
            }
            return;
        }
        if (uMsg == WM_INITMENUPOPUP || uMsg == WM_DRAWITEM || uMsg == WM_MEASUREITEM) {
            if (SUCCEEDED(m_pCtxMenu3->HandleMenuMsg(uMsg, wParam, lParam))) {
                if (pResult) *pResult = 0;
                if (handled) *handled = true;
            }
            return;
        }
    } else if (m_pCtxMenu2) {
        if (uMsg == WM_INITMENUPOPUP || uMsg == WM_DRAWITEM || uMsg == WM_MEASUREITEM) {
            if (SUCCEEDED(m_pCtxMenu2->HandleMenuMsg(uMsg, wParam, lParam))) {
                if (pResult) *pResult = 0;
                if (handled) *handled = true;
            }
        }
    }
}

bool CMainWnd::TrackPopupShellMenu(IContextMenu* pMenu, HMENU hMenu, POINT ptScreen,
    UINT idCmdFirst, UINT idShellMax, bool appendHiddenToggle)
{
    if (!pMenu || !hMenu)
        return false;

    if (appendHiddenToggle) {
        ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        UINT flags = MF_STRING | (m_showHidden ? MF_CHECKED : MF_UNCHECKED);
        ::AppendMenuW(hMenu, flags, kCmdToggleHidden, L"显示隐藏的项目");
    }

    // Hold IContextMenu2/3 so owner-draw + cascading submenus work during TrackPopupMenu.
    // Do NOT use TPM_NONOTIFY — Shell needs WM_INITMENUPOPUP / DRAWITEM / MEASUREITEM.
    m_pCtxMenu = pMenu;
    m_pCtxMenu2 = nullptr;
    m_pCtxMenu3 = nullptr;
    pMenu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void**>(&m_pCtxMenu2));
    pMenu->QueryInterface(IID_IContextMenu3, reinterpret_cast<void**>(&m_pCtxMenu3));

    UINT cmd = ::TrackPopupMenuEx(hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        ptScreen.x, ptScreen.y, m_hWnd, nullptr);

    IContextMenu2* pcm2 = m_pCtxMenu2;
    IContextMenu3* pcm3 = m_pCtxMenu3;
    m_pCtxMenu = nullptr;
    m_pCtxMenu2 = nullptr;
    m_pCtxMenu3 = nullptr;
    if (pcm3) pcm3->Release();
    if (pcm2) pcm2->Release();

    if (cmd == kCmdToggleHidden) {
        ToggleShowHidden();
        return true;
    }

    if (cmd >= idCmdFirst && cmd < idShellMax) {
        wchar_t verb[128] = {};
        const bool hasVerb = SUCCEEDED(pMenu->GetCommandString(cmd - idCmdFirst,
            GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), _countof(verb)));
        CMINVOKECOMMANDINFOEX info = {};
        info.cbSize = sizeof(info);
        info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
        info.hwnd = m_hWnd;
        info.lpVerb = MAKEINTRESOURCEA(cmd - idCmdFirst);
        info.lpVerbW = MAKEINTRESOURCEW(cmd - idCmdFirst);
        info.nShow = SW_SHOWNORMAL;
        info.ptInvoke = ptScreen;
        HRESULT hr = pMenu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
        if (SUCCEEDED(hr) && hasVerb && m_shellMenuPaths.size() == 1) {
            if (_wcsicmp(verb, L"pintohome") == 0)
                PinQuickAccess(m_shellMenuPaths.front());
            else if (_wcsicmp(verb, L"unpinfromhome") == 0)
                UnpinQuickAccess(m_shellMenuPaths.front());
        }
        RefreshListing();
    }
    return true;
}

void CMainWnd::ShowBlankAreaContextMenu(POINT ptScreen)
{
    // Background right-clicks must be Shell-owned in every view. Do not mix a
    // FastFile fallback menu into the native Windows folder context menu.
    if (!ShowShellBackgroundContextMenu(m_currentPath, ptScreen))
        UpdateStatus(_T("无法显示 Windows 文件夹菜单"));
}

void CMainWnd::ShowTreeContextMenu(CTreeNodeUI* node, POINT ptScreen)
{
    if (!node) return;
    CDuiString ud = node->GetUserData();
    if (ud.IsEmpty()) return;
    std::wstring path = ud.GetData();
    if (IsThisPcPath(path) || path == kPendingMarker)
        return;
    std::vector<std::wstring> paths;
    paths.push_back(NormalizePath(path));
    if (!ShowShellContextMenu(paths, ptScreen)) {
        ClipboardItem it;
        it.path = paths[0];
        it.isDir = true;
        ShowFallbackContextMenu({ it }, ptScreen);
    }
}

bool CMainWnd::ShowShellBackgroundContextMenu(const std::wstring& folderPath, POINT ptScreen)
{
    PIDLIST_ABSOLUTE pidlFolder = nullptr;
    SFGAOF sfgao = 0;
    HRESULT hr = S_OK;
    if (folderPath.empty() || IsThisPcPath(folderPath)) {
        // The Computer folder has its own native background verbs (View, Sort,
        // Refresh, etc.) and must not fall through to FastFile's custom menu.
        hr = ::SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &pidlFolder);
    } else {
        hr = ::SHParseDisplayName(folderPath.c_str(), nullptr, &pidlFolder, 0, &sfgao);
    }
    if (FAILED(hr) || !pidlFolder) return false;

    // Bind from the desktop shell folder, as Explorer does, so this is the
    // directory background context rather than a FastFile-owned fallback.
    IShellFolder* pDesktop = nullptr;
    IShellFolder* pFolder = nullptr;
    hr = ::SHGetDesktopFolder(&pDesktop);
    if (SUCCEEDED(hr) && pDesktop) {
        hr = pDesktop->BindToObject(pidlFolder, nullptr, IID_IShellFolder,
            reinterpret_cast<void**>(&pFolder));
        pDesktop->Release();
    }
    ::CoTaskMemFree(pidlFolder);
    if (FAILED(hr) || !pFolder) return false;

    IContextMenu* pMenu = nullptr;
    hr = pFolder->CreateViewObject(m_hWnd, IID_IContextMenu, reinterpret_cast<void**>(&pMenu));
    pFolder->Release();
    if (FAILED(hr) || !pMenu) return false;

    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) {
        pMenu->Release();
        return false;
    }

    const UINT idCmdFirst = 1;
    const UINT idCmdLast = 0x7FFF;
    hr = pMenu->QueryContextMenu(hMenu, 0, idCmdFirst, idCmdLast,
        CMF_NORMAL | CMF_EXPLORE | CMF_EXTENDEDVERBS);
    if (FAILED(hr)) {
        ::DestroyMenu(hMenu);
        pMenu->Release();
        return false;
    }

    const UINT idShellMax = idCmdFirst + static_cast<UINT>(HRESULT_CODE(hr));
    // Full Shell menu (IContextMenu2/3), with no FastFile-injected commands.
    TrackPopupShellMenu(pMenu, hMenu, ptScreen, idCmdFirst, idShellMax, false);

    ::DestroyMenu(hMenu);
    pMenu->Release();
    return true;
}

// ---- Context menu (IContextMenu + fallback) ------------------------------

void CMainWnd::ShowItemContextMenu(CControlUI* /*pItem*/, POINT ptScreen)
{
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.empty()) {
        ShowBlankAreaContextMenu(ptScreen);
        return;
    }
    std::vector<std::wstring> paths;
    paths.reserve(items.size());
    for (const auto& it : items)
        paths.push_back(it.path);

    if (!ShowShellContextMenu(paths, ptScreen))
        ShowFallbackContextMenu(items, ptScreen);
}

bool CMainWnd::ShowShellContextMenu(const std::vector<std::wstring>& paths, POINT ptScreen)
{
    if (paths.empty()) return false;

    HRESULT hrInit = S_OK;
    // COM already initialized in wWinMain

    // Use parent folder of first item; all items should share parent for multi
    std::wstring parent = ParentPath(paths[0]);
    if (parent.empty()) {
        // drive root file?
        if (paths[0].size() >= 3 && paths[0][1] == L':')
            parent = paths[0].substr(0, 3);
        else
            return false;
    }

    PIDLIST_ABSOLUTE pidlFolder = nullptr;
    SFGAOF sfgao = 0;
    HRESULT hr = ::SHParseDisplayName(parent.c_str(), nullptr, &pidlFolder, 0, &sfgao);
    if (FAILED(hr) || !pidlFolder) return false;

    IShellFolder* pFolder = nullptr;
    hr = ::SHBindToObject(nullptr, pidlFolder, nullptr, IID_IShellFolder, reinterpret_cast<void**>(&pFolder));
    ::CoTaskMemFree(pidlFolder);
    if (FAILED(hr) || !pFolder) return false;

    std::vector<PIDLIST_RELATIVE> pidlChildren;
    pidlChildren.reserve(paths.size());
    bool ok = true;
    for (const auto& path : paths) {
        std::wstring leaf = GetLeafName(path);
        PIDLIST_RELATIVE pidlChild = nullptr;
        DWORD attrs = 0;
        hr = pFolder->ParseDisplayName(m_hWnd, nullptr, const_cast<LPWSTR>(leaf.c_str()),
            nullptr, &pidlChild, &attrs);
        if (FAILED(hr) || !pidlChild) {
            ok = false;
            break;
        }
        pidlChildren.push_back(pidlChild);
    }

    IContextMenu* pMenu = nullptr;
    if (ok && !pidlChildren.empty()) {
        std::vector<LPCITEMIDLIST> pidlArgs(pidlChildren.begin(), pidlChildren.end());
        hr = pFolder->GetUIObjectOf(m_hWnd,
            static_cast<UINT>(pidlArgs.size()),
            pidlArgs.data(),
            IID_IContextMenu, nullptr, reinterpret_cast<void**>(&pMenu));
        if (FAILED(hr)) pMenu = nullptr;
    }

    for (auto* p : pidlChildren)
        ::CoTaskMemFree(p);
    pFolder->Release();

    if (!pMenu) return false;

    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) {
        pMenu->Release();
        return false;
    }

    const UINT idCmdFirst = 1;
    const UINT idCmdLast = 0x7FFF;
    hr = pMenu->QueryContextMenu(hMenu, 0, idCmdFirst, idCmdLast,
        CMF_NORMAL | CMF_EXPLORE);
    if (FAILED(hr)) {
        ::DestroyMenu(hMenu);
        pMenu->Release();
        return false;
    }

    const UINT idShellMax = idCmdFirst + static_cast<UINT>(HRESULT_CODE(hr));
    // Full Shell menu with owner-draw / cascaded submenus via IContextMenu2/3
    m_shellMenuPaths = paths;
    TrackPopupShellMenu(pMenu, hMenu, ptScreen, idCmdFirst, idShellMax, false);
    m_shellMenuPaths.clear();

    ::DestroyMenu(hMenu);
    pMenu->Release();
    return true;
}

void CMainWnd::ShowFallbackContextMenu(const std::vector<ClipboardItem>& items, POINT ptScreen)
{
    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;

    if (!items.empty()) {
        ::AppendMenuW(hMenu, MF_STRING, kCmdCtxOpen, L"打开");
        ::AppendMenuW(hMenu, MF_STRING, kCmdCtxCopy, L"复制");
        ::AppendMenuW(hMenu, MF_STRING, kCmdCtxDelete, L"删除到回收站");
        if (items.size() == 1)
            ::AppendMenuW(hMenu, MF_STRING, kCmdCtxRename, L"重命名");
        ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }
    ::AppendMenuW(hMenu, MF_STRING, kCmdCtxRefresh, L"刷新");
    ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    {
        UINT flags = MF_STRING | (m_showHidden ? MF_CHECKED : MF_UNCHECKED);
        ::AppendMenuW(hMenu, flags, kCmdToggleHidden, L"显示隐藏的项目");
    }

    UINT cmd = ::TrackPopupMenuEx(hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        ptScreen.x, ptScreen.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);

    switch (cmd) {
    case kCmdCtxOpen:
        if (!items.empty()) {
            if (items[0].isDir)
                NavigateTo(items[0].path, true);
            else
                ::ShellExecuteW(m_hWnd, L"open", items[0].path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
    case kCmdCtxCopy: OnCopyClicked(); break;
    case kCmdCtxDelete: OnDeleteClicked(); break;
    case kCmdCtxRename: OnRenameClicked(); break;
    case kCmdCtxRefresh: RefreshListing(); break;
    case kCmdToggleHidden: ToggleShowHidden(); break;
    default: break;
    }
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


// ---- Search filter -------------------------------------------------------

bool CMainWnd::EntryMatchesFilter(const DirEntry& e) const
{
    if (m_searchFilter.empty())
        return true;
    return ::StrStrIW(e.name.c_str(), m_searchFilter.c_str()) != nullptr;
}

bool CMainWnd::IsRecursiveSearch() const
{
    return m_pChkRecursive && m_pChkRecursive->IsSelected();
}

void CMainWnd::CollectRecursiveMatches(const std::wstring& root, const std::wstring& filter,
    std::vector<DirEntry>& dirs, std::vector<DirEntry>& files, bool& truncated)
{
    truncated = false;
    dirs.clear();
    files.clear();
    if (root.empty() || filter.empty()) return;

    std::vector<std::wstring> queue;
    queue.push_back(root);
    int visited = 0;

    while (!queue.empty()) {
        std::wstring dir = queue.back();
        queue.pop_back();

        std::wstring pattern = dir;
        if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
            pattern.push_back(L'\\');
        pattern += L"*";

        WIN32_FIND_DATAW fd = {};
        HANDLE hFind = ::FindFirstFileExW(
            pattern.c_str(), FindExInfoBasic, &fd,
            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (hFind == INVALID_HANDLE_VALUE)
            continue;

        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            std::wstring full = dir;
            if (!full.empty() && full.back() != L'\\' && full.back() != L'/')
                full.push_back(L'\\');
            full += fd.cFileName;

            if (isDir && !ShouldHideByAttributes(fd.dwFileAttributes)) {
                queue.push_back(full);
            }

            if (::StrStrIW(fd.cFileName, filter.c_str()) != nullptr) {
                DirEntry e;
                // Show relative path under root for clarity
                std::wstring rel = full;
                if (rel.size() > root.size()
                    && _wcsnicmp(rel.c_str(), root.c_str(), (int)root.size()) == 0) {
                    rel = rel.substr(root.size());
                    while (!rel.empty() && (rel.front() == L'\\' || rel.front() == L'/'))
                        rel.erase(rel.begin());
                }
                e.name = rel.empty() ? fd.cFileName : rel;
                e.fullPath = full;
                e.isDir = isDir;
                e.size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                e.mtime = FileTimeToU64(fd.ftLastWriteTime);
                e.attrs = fd.dwFileAttributes;
                if (ShouldHideByAttributes(e.attrs))
                    continue;
                if (isDir) dirs.push_back(std::move(e));
                else files.push_back(std::move(e));
            }

            ++visited;
            if ((visited % kPumpEvery) == 0)
                PumpUiMessages();

            if ((int)(dirs.size() + files.size()) >= kMaxRecursiveItems) {
                truncated = true;
                ::FindClose(hFind);
                queue.clear();
                break;
            }
        } while (::FindNextFileW(hFind, &fd));
        if (truncated) break;
        ::FindClose(hFind);
    }
}

void CMainWnd::ApplySearchFilter()
{
    if (!m_pSearchEdit) return;
    if (m_searchPlaceholder) {
        m_searchFilter.clear();
    } else {
        m_searchFilter = m_pSearchEdit->GetText().GetData();
        while (!m_searchFilter.empty() && (m_searchFilter.front() == L' ' || m_searchFilter.front() == L'\t'))
            m_searchFilter.erase(m_searchFilter.begin());
        while (!m_searchFilter.empty() && (m_searchFilter.back() == L' ' || m_searchFilter.back() == L'\t'))
            m_searchFilter.pop_back();
    }
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
        m_tabs[m_activeTab].searchFilter = m_searchFilter;
    if (IsRecursiveSearch() && !m_searchFilter.empty())
        UpdateStatus(_T("正在递归搜索…"));
    RefreshListing();
}

void CMainWnd::ClearSearchFilter()
{
    m_searchFilter.clear();
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
        m_tabs[m_activeTab].searchFilter.clear();
    SetSearchPlaceholder(true);
    RefreshListing();
}

void CMainWnd::SetSearchPlaceholder(bool show)
{
    if (!m_pSearchEdit) return;
    m_searchPlaceholder = show;
    if (show) {
        m_pSearchEdit->SetText(L"搜索");
        m_pSearchEdit->SetAttribute(_T("textcolor"), _T("#FFB0B0B0"));
    } else {
        // Clear placeholder glyph; keep any real typed text.
        if (std::wstring(m_pSearchEdit->GetText().GetData()) == L"搜索")
            m_pSearchEdit->SetText(_T(""));
        m_pSearchEdit->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
    }
}

void CMainWnd::SyncRecursiveCheckLabel()
{
    if (!m_pChkRecursive) return;
    const bool on = m_pChkRecursive->IsSelected();
    m_pChkRecursive->SetText(on ? L"☑ 含子目录" : L"☐ 含子目录");
    m_pChkRecursive->SetAttribute(_T("font"), _T("0"));
    m_pChkRecursive->SetAttribute(_T("valign"), _T("vcenter"));
    m_pChkRecursive->SetAttribute(_T("align"), _T("left"));
}


// ---- Directory tree ------------------------------------------------------

void CMainWnd::InitDirectoryTree()
{
    if (!m_pDirTree) return;
    m_pDirTree->RemoveAll();

    CTreeNodeUI* root = AddTreeFolderNode(nullptr, kThisPcPath, L"此电脑");
    if (!root) return;

    wchar_t drives[512] = {};
    const DWORD n = ::GetLogicalDriveStringsW(_countof(drives) - 1, drives);
    if (n > 0 && n < _countof(drives)) {
        for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
            std::wstring title = FormatDriveDisplayName(p);
            CTreeNodeUI* driveNode = AddTreeFolderNode(root, p, title);
            if (driveNode)
                AttachPendingChild(driveNode);
        }
    }
    ExpandTreeNode(root, false);
}

void CMainWnd::StyleTreeNode(CTreeNodeUI* node, const std::wstring& title, bool hasChildrenHint)
{
    if (!node) return;
    // DuiLib CTreeNodeUI defaults to FixedWidth(250); left panel is ~220px, so labels
    // were clipped / only ellipsis remained visible. Stretch to list width instead.
    node->SetFixedWidth(0);
    node->SetFixedHeight(DpiScale(UiTokens::TreeRowH)); // Phase2: Win11 Explorer tree density
    node->SetVisibleCheckBtn(false);
    node->SetVisibleFolderBtn(hasChildrenHint);

    if (COptionUI* item = node->GetItemButton()) {
        item->SetText(title.c_str());
        item->SetAttribute(_T("align"), _T("left"));
        item->SetAttribute(_T("valign"), _T("vcenter"));
        item->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        item->SetAttribute(_T("endellipsis"), _T("true"));
        item->SetMouseEnabled(false);
    }
    node->SetItemText(title.c_str());
    node->SetItemTextColor(UiTokens::ArgbTextPrimary);
    node->SetItemHotTextColor(UiTokens::ArgbTextPrimary);
    node->SetSelItemTextColor(UiTokens::ArgbTextPrimary);
    node->SetSelItemHotTextColor(UiTokens::ArgbTextPrimary);
        // Phase2: per-level indent (DuiLib hardcodes +16; retarget to TreeIndent).
    // GetTreeLevel() is declared but not defined in this DuiLib build — walk parents.
    if (CLabelUI* dotted = node->GetDottedLine()) {
        int level = 0;
        for (CTreeNodeUI* p = node->GetParentNode(); p; p = p->GetParentNode())
            ++level;
        if (level > 0) {
            dotted->SetFixedWidth(DpiScale(2 + level * UiTokens::TreeIndent));
            dotted->SetVisible(true);
        }
    }
    // Hover / selected bk via list attrs on tree host; also paint option button
    if (COptionUI* itemBtn = node->GetItemButton()) {
        itemBtn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorListHover);
        itemBtn->SetAttribute(_T("selectedbkcolor"), UiTokens::ColorListSelected);
    }

    if (CCheckBoxUI* folder = node->GetFolderButton()) {
        folder->SetFixedWidth(DpiScale(16));
        folder->SetAttribute(_T("align"), _T("center"));
        folder->SetAttribute(_T("valign"), _T("vcenter"));
        folder->SetAttribute(_T("textcolor"), UiTokens::ColorTextMuted);
        folder->SetText(hasChildrenHint ? _T("+") : _T(" "));
    }
}

void CMainWnd::AttachPendingChild(CTreeNodeUI* parent)
{
    if (!parent) return;
    CTreeNodeUI* pending = new CTreeNodeUI(parent);
    pending->SetFixedWidth(0);
    pending->SetFixedHeight(0);
    pending->SetItemText(_T(""));
    pending->SetUserData(kPendingMarker);
    pending->SetVisibleCheckBtn(false);
    pending->SetVisibleFolderBtn(false);
    parent->AddChildNode(pending);
    pending->SetVisible(false);
    if (CCheckBoxUI* folder = parent->GetFolderButton()) {
        folder->Selected(true); // selected == collapsed in DuiLib TreeView
        folder->SetText(_T("+"));
        folder->OnNotify += MakeDelegate(this, &CMainWnd::OnTreeFolderNotify);
    }
}

CTreeNodeUI* CMainWnd::AddTreeFolderNode(CTreeNodeUI* parent, const std::wstring& path, const std::wstring& title)
{
    if (!m_pDirTree) return nullptr;
    CTreeNodeUI* node = new CTreeNodeUI(parent);
    node->SetUserData(path.c_str());
    StyleTreeNode(node, title, true);
    if (parent)
        parent->AddChildNode(node);
    else
        m_pDirTree->Add(node);
    // Re-apply text after insert (some DuiLib paths reset child attrs on Add)
    StyleTreeNode(node, title, true);
    ApplyTreeNodeIcon(node, path);
    return node;
}

bool CMainWnd::OnTreeFolderNotify(void* param)
{
    auto* pMsg = static_cast<TNotifyUI*>(param);
    if (!pMsg || pMsg->sType != DUI_MSGTYPE_SELECTCHANGED || !pMsg->pSender)
        return true;
    CControlUI* p = pMsg->pSender;
    while (p && !p->GetInterface(DUI_CTR_TREENODE))
        p = p->GetParent();
    if (!p) return true;

    auto* node = static_cast<CTreeNodeUI*>(p);
    CCheckBoxUI* folder = node->GetFolderButton();
    const bool collapsed = folder && folder->IsSelected();
    if (!collapsed) {
        EnsureTreeChildren(node);
        ExpandTreeNode(node, false);
        if (folder) folder->SetText(_T("-"));
    } else {
        if (m_pDirTree)
            m_pDirTree->SetItemExpand(false, node);
        if (folder) folder->SetText(_T("+"));
    }
    return true;
}

void CMainWnd::ExpandTreeNode(CTreeNodeUI* node, bool navigate)
{
    if (!node || !m_pDirTree) return;
    EnsureTreeChildren(node);

    // Show direct children but PRESERVE each child's own expand/collapse state.
    // Previously we forced every child folder button to Selected(true)=collapsed,
    // which made SyncTreeToPath / single-click navigate wipe expanded branches.
    // Pending placeholders stay hidden; newly attached children already start collapsed.
    const int n = node->GetCountChild();
    for (int i = 0; i < n; ++i) {
        CTreeNodeUI* c = node->GetChildNode(i);
        if (!c) continue;
        if (c->GetUserData() == CDuiString(kPendingMarker)) {
            c->SetVisible(false);
            continue;
        }
        c->SetVisible(true);
    }
    if (CCheckBoxUI* fb = node->GetFolderButton()) {
        fb->Selected(false);
        fb->SetText(_T("-"));
    }
    m_pDirTree->SetItemExpand(true, node);

    // SetItemExpand may reveal pending under already-expanded descendants — re-hide.
    std::function<void(CTreeNodeUI*)> hidePending = [&](CTreeNodeUI* n) {
        if (!n) return;
        for (int i = 0; i < n->GetCountChild(); ++i) {
            CTreeNodeUI* c = n->GetChildNode(i);
            if (!c) continue;
            if (c->GetUserData() == CDuiString(kPendingMarker))
                c->SetVisible(false);
            else
                hidePending(c);
        }
    };
    hidePending(node);

    if (navigate) {
        CDuiString ud = node->GetUserData();
        if (!ud.IsEmpty() && ud != CDuiString(kPendingMarker))
            NavigateTo(ud.GetData(), true);
    }
}

void CMainWnd::EnsureTreeChildren(CTreeNodeUI* node)
{
    if (!node || !m_pDirTree) return;
    CDuiString ud = node->GetUserData();
    if (ud.IsEmpty() || ud == CDuiString(kPendingMarker))
        return;
    if (ud == CDuiString(kThisPcPath))
        return;

    bool hasPending = false;
    const int childCount = node->GetCountChild();
    if (childCount == 1) {
        CTreeNodeUI* c = node->GetChildNode(0);
        if (c && c->GetUserData() == CDuiString(kPendingMarker))
            hasPending = true;
    } else if (childCount > 0) {
        return; // already loaded
    }

    if (hasPending) {
        CTreeNodeUI* pending = node->GetChildNode(0);
        node->RemoveAt(pending);
    }

    const std::wstring path = ud.GetData();
    std::wstring pattern = path;
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/')
        pattern.push_back(L'\\');
    pattern += L"*";

    WIN32_FIND_DATAW fd = {};
    HANDLE h = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
        FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE)
        return;

    std::vector<std::wstring> subdirs;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        if (ShouldHideByAttributes(fd.dwFileAttributes))
            continue;
        subdirs.push_back(fd.cFileName);
        if (static_cast<int>(subdirs.size()) >= 500)
            break;
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);

    std::sort(subdirs.begin(), subdirs.end(), [](const std::wstring& a, const std::wstring& b) {
        return ::_wcsicmp(a.c_str(), b.c_str()) < 0;
    });

    for (const auto& name : subdirs) {
        std::wstring full = JoinPath(path, name);
        CTreeNodeUI* child = AddTreeFolderNode(node, full, name);
        if (!child) continue;
        AttachPendingChild(child);
        child->SetVisible(false); // parent ExpandTreeNode will show
    }
}

void CMainWnd::OnTreeNodeActivate(CTreeNodeUI* node)
{
    if (!node) return;
    CDuiString ud = node->GetUserData();
    if (ud.IsEmpty() || ud == CDuiString(kPendingMarker))
        return;
    // Single click: select + navigate only. Do NOT expand/collapse here —
    // +/- folder button (OnTreeFolderNotify) owns expand/collapse so that
    // clicking a folder never auto-collapses already-expanded branches.
    node->Select(true);
    NavigateTo(ud.GetData(), true);
}

CTreeNodeUI* CMainWnd::FindTreeNodeByPath(CTreeNodeUI* parent, const std::wstring& path) const
{
    if (!m_pDirTree) return nullptr;
    if (parent) {
        CDuiString ud = parent->GetUserData();
        if (!ud.IsEmpty() && PathEquals(ud.GetData(), path))
            return parent;
        const int n = parent->GetCountChild();
        for (int i = 0; i < n; ++i) {
            CTreeNodeUI* c = parent->GetChildNode(i);
            if (!c) continue;
            if (c->GetUserData() == CDuiString(kPendingMarker))
                continue;
            CTreeNodeUI* found = FindTreeNodeByPath(c, path);
            if (found) return found;
        }
        return nullptr;
    }
    const int n = m_pDirTree->GetCount();
    for (int i = 0; i < n; ++i) {
        CControlUI* p = m_pDirTree->GetItemAt(i);
        if (!p || !p->GetInterface(DUI_CTR_TREENODE)) continue;
        auto* node = static_cast<CTreeNodeUI*>(p);
        if (node->GetParentNode() != nullptr) continue;
        CTreeNodeUI* found = FindTreeNodeByPath(node, path);
        if (found) return found;
    }
    return nullptr;
}

void CMainWnd::SyncTreeToPath(const std::wstring& path)
{
    if (!m_pDirTree || m_syncingTree) return;
    m_syncingTree = true;

    if (IsThisPcPath(path)) {
        CTreeNodeUI* root = FindTreeNodeByPath(nullptr, kThisPcPath);
        if (root) {
            ExpandTreeNode(root, false);
            root->Select(true);
        }
        m_syncingTree = false;
        return;
    }

    std::wstring norm = NormalizePath(path);
    if (norm.empty()) { m_syncingTree = false; return; }

    std::vector<std::wstring> chain;
    if (norm.size() >= 2 && norm[1] == L':') {
        std::wstring drive = norm.substr(0, 2) + L"\\";
        chain.push_back(drive);
        size_t start = 3;
        while (start < norm.size()) {
            size_t slash = norm.find_first_of(L"\\/", start);
            if (slash == std::wstring::npos) {
                chain.push_back(norm);
                break;
            }
            chain.push_back(norm.substr(0, slash));
            start = slash + 1;
        }
        if (chain.empty() || !PathEquals(chain.back(), norm))
            chain.push_back(norm);
    }

    CTreeNodeUI* node = FindTreeNodeByPath(nullptr, kThisPcPath);
    if (node)
        ExpandTreeNode(node, false);

    CTreeNodeUI* last = nullptr;
    for (size_t i = 0; i < chain.size(); ++i) {
        const auto& prefix = chain[i];
        CTreeNodeUI* found = FindTreeNodeByPath(nullptr, prefix);
        if (!found && last) {
            EnsureTreeChildren(last);
            ExpandTreeNode(last, false);
            found = FindTreeNodeByPath(last, prefix);
        }
        if (!found)
            found = FindTreeNodeByPath(nullptr, prefix);
        if (found) {
            // Expand ancestors so the leaf is visible; do NOT expand the leaf
            // itself — folder label click is select+navigate only; +/- expands.
            if (i + 1 < chain.size())
                ExpandTreeNode(found, false);
            last = found;
        }
    }
    if (last)
        last->Select(true);

    m_syncingTree = false;
}

// ---- Tabs ----------------------------------------------------------------

void CMainWnd::InitTabs()
{
    m_tabs.clear();
    m_activeTab = -1;
    if (m_pTabStrip) {
        m_pTabStrip->RemoveAll();
        // A zero-width layout is treated as flexible by DuiLib and moves the + button
        // toward the centre of an empty tab bar. Keep a minimal fixed anchor instead.
        m_pTabStrip->SetFixedWidth(DpiScale(1));
    }
}

std::wstring CMainWnd::TabTitleForPath(const std::wstring& path) const
{
    if (IsThisPcPath(path))
        return L"此电脑";
    std::wstring leaf = GetLeafName(path);
    if (leaf.empty()) {
        if (path.size() >= 2 && path[1] == L':')
            return path.substr(0, 2);
        return L"标签";
    }
    return leaf;
}

void CMainWnd::RebuildTabStrip()
{
    if (!m_pTabStrip) return;
    m_updatingTabs = true;
    m_pTabStrip->RemoveAll();
    int tabStripW = 0;
    const int tabIconPx = DpiScale(UiTokens::TabIconPx);
    const int tabIconPad = DpiScale(UiTokens::SpaceSm);
    const int tabTextGap = DpiScale(UiTokens::SpaceXs);
    const int tabTextPadR = DpiScale(UiTokens::SpaceSm);
    const int tabH = DpiScale(UiTokens::HitTabH);

    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        auto* host = new CHorizontalLayoutUI;
        host->SetFixedHeight(tabH);
        host->SetAttribute(_T("padding"), _T("0,0,2,0"));

        CDuiString btnName, closeName;
        btnName.Format(_T("tab_btn_%d"), i);
        closeName.Format(_T("tab_close_%d"), i);

        const std::wstring title = TabTitleForPath(m_tabs[i].path);
        auto* btn = new CButtonUI;
        btn->SetName(btnName);
        btn->SetText(title.c_str());
        btn->SetAttribute(_T("align"), _T("center"));
        btn->SetAttribute(_T("valign"), _T("vcenter"));
        btn->SetAttribute(_T("endellipsis"), _T("true"));
        btn->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
        btn->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
        btn->SetAttribute(_T("bordersize"), _T("0"));
        btn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorHover);
        btn->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorPressed);
        {
            CDuiString tp;
            tp.Format(_T("%d,0,%d,0"),
                tabIconPad + tabIconPx + tabTextGap, tabTextPadR);
            btn->SetAttribute(_T("textpadding"), tp);
        }
        if (i == m_activeTab) {
            btn->SetAttribute(_T("bkcolor"), UiTokens::ColorTabActive);
            btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
            btn->SetAttribute(_T("font"), _T("0"));
            btn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorTabActiveHot);
        } else {
            btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextTabIdle);
        }
        SIZE textSize = { 0, 0 };
        if (m_hWnd) {
            HDC dc = ::GetDC(m_hWnd);
            if (dc) {
                HFONT font = m_PaintManager.GetFont(0);
                HGDIOBJ old = font ? ::SelectObject(dc, font) : nullptr;
                ::GetTextExtentPoint32W(dc, title.c_str(),
                    static_cast<int>(title.size()), &textSize);
                if (old) ::SelectObject(dc, old);
                ::ReleaseDC(m_hWnd, dc);
            }
        }
        if (textSize.cx <= 0)
            textSize.cx = DpiScale(static_cast<int>(title.size()) * 8);
        int w = textSize.cx + tabIconPad + tabIconPx + tabTextGap + tabTextPadR;
        if (w < DpiScale(UiTokens::TabMinW)) w = DpiScale(UiTokens::TabMinW);
        if (w > DpiScale(UiTokens::TabMaxW)) w = DpiScale(UiTokens::TabMaxW);
        btn->SetFixedWidth(w);
        SIZE tabRound = { DpiScale(6), DpiScale(6) };
        btn->SetBorderRound(tabRound);
        std::wstring tabIcon = IsThisPcPath(m_tabs[i].path)
            ? GetStockIconBmp(SIID_DESKTOPPC, tabIconPx)
            : GetShellIconBmp(m_tabs[i].path, true, tabIconPx);
        if (tabIcon.empty())
            tabIcon = GetStockIconBmp(SIID_FOLDER, tabIconPx);
        if (!tabIcon.empty())
            ApplyControlForeIcon(btn, tabIcon, tabIconPx, tabIconPad,
                (tabH - tabIconPx) / 2, false);
        m_tabs[i].button = btn;

        auto* closeBtn = new CButtonUI;
        closeBtn->SetName(closeName);
        closeBtn->SetText(_T("×"));
        closeBtn->SetFixedWidth(DpiScale(UiTokens::TabCloseW));
        closeBtn->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
        closeBtn->SetAttribute(_T("bordersize"), _T("0"));
        closeBtn->SetAttribute(_T("textcolor"), UiTokens::ColorTextMuted);
        closeBtn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorHover);
        closeBtn->SetAttribute(_T("hottextcolor"), UiTokens::ColorDanger);

        host->SetFixedWidth(w + DpiScale(UiTokens::TabCloseW));
        host->Add(btn);
        host->Add(closeBtn);
        m_pTabStrip->Add(host);
        tabStripW += w + DpiScale(UiTokens::TabCloseW);
    }
    // The strip must occupy only its actual content so the static + button stays
    // immediately after the final tab; the following spacer consumes the remainder.
    m_pTabStrip->SetFixedWidth((std::max)(DpiScale(1), tabStripW));
    m_pTabStrip->NeedUpdate();
    m_updatingTabs = false;
}
void CMainWnd::AddTab(const std::wstring& path, bool activate)
{
    TabInfo tab;
    tab.path = path.empty() ? GetDefaultStartPath() : path;
    m_tabs.push_back(tab);
    if (activate)
        ActivateTab(static_cast<int>(m_tabs.size()) - 1);
    else
        RebuildTabStrip();
}

void CMainWnd::CloseTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;
    if (m_tabs.size() <= 1) {
        UpdateStatus(_T("至少保留一个标签"));
        return;
    }
    m_tabs.erase(m_tabs.begin() + index);
    int next = m_activeTab;
    if (index < m_activeTab)
        next = m_activeTab - 1;
    else if (index == m_activeTab)
        next = (std::min)(index, static_cast<int>(m_tabs.size()) - 1);
    m_activeTab = -1;
    ActivateTab(next);
}

void CMainWnd::ActivateTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;
    // persist current tab filter/path
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        m_tabs[m_activeTab].path = m_currentPath;
        m_tabs[m_activeTab].searchFilter = m_searchFilter;
    }
    m_activeTab = index;
    RebuildTabStrip();

    const TabInfo& tab = m_tabs[m_activeTab];
    m_searchFilter = tab.searchFilter;
    if (m_pSearchEdit)
        m_searchPlaceholder = false;
        m_pSearchEdit->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        m_pSearchEdit->SetText(m_searchFilter.c_str());
        if (m_searchFilter.empty())
            SetSearchPlaceholder(true);

    // Navigate without wiping the restored filter
    std::wstring target = tab.path;
    std::wstring savedFilter = m_searchFilter;
    if (IsThisPcPath(target) || target == L"此电脑") {
        m_currentPath = kThisPcPath;
        if (m_pAddressEdit) m_pAddressEdit->SetText(_T("此电脑"));
    } else {
        std::wstring norm = NormalizePath(target);
        if (norm.empty())
            norm = GetDefaultStartPath();
        m_currentPath = norm;
        if (m_pAddressEdit) m_pAddressEdit->SetText(m_currentPath.c_str());
    }
    m_searchFilter = savedFilter;
    if (m_pSearchEdit)
        m_searchPlaceholder = false;
        m_pSearchEdit->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        m_pSearchEdit->SetText(m_searchFilter.c_str());
        if (m_searchFilter.empty())
            SetSearchPlaceholder(true);
    RefreshListing();
    SyncTreeToPath(m_currentPath);
    UpdateFavoritesHighlight();
    UpdateNavButtons();
}

void CMainWnd::UpdateActiveTabPath(const std::wstring& path)
{
    if (m_activeTab < 0 || m_activeTab >= static_cast<int>(m_tabs.size()))
        return;
    m_tabs[m_activeTab].path = path;
    m_tabs[m_activeTab].searchFilter = m_searchFilter;
    // A folder name changes both text and measured width, so rebuild immediately
    // instead of waiting for a later tab activation to refresh the layout.
    RebuildTabStrip();
}


// ---- Session persist -----------------------------------------------------

std::wstring CMainWnd::GetSessionFilePath()
{
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir;
    if (n > 0 && n < MAX_PATH)
        dir = appdata;
    else
        dir = L".";
    dir += L"\\FastFile";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\session.ini";
}


std::wstring CMainWnd::GetFolderViewsFilePath()
{
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir;
    if (n > 0 && n < MAX_PATH)
        dir = appdata;
    else
        dir = L".";
    dir += L"\\FastFile";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\folder_views.ini";
}

std::wstring CMainWnd::NormalizeViewKey(const std::wstring& path)
{
    if (path.empty())
        return {};
    if (IsThisPcPath(path) || path == L"此电脑" || ::_wcsicmp(path.c_str(), L"This PC") == 0)
        return kThisPcPath;
    std::wstring key = NormalizePath(path);
    if (key.empty())
        return {};
    for (auto& ch : key)
        ch = static_cast<wchar_t>(towlower(ch));
    return key;
}

CMainWnd::ViewMode CMainWnd::LoadFolderViewForPath(const std::wstring& path) const
{
    const ViewMode kDefault = ViewMode::Tiles;
    std::wstring key = NormalizeViewKey(path);
    if (key.empty())
        return kDefault;

    std::wstring file = GetFolderViewsFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, file.c_str(), L"rb") != 0 || !fp)
        return kDefault;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 4) { fclose(fp); return kDefault; }
    std::wstring content;
    content.resize(sz / sizeof(wchar_t));
    fread(&content[0], 1, sz, fp);
    fclose(fp);
    if (!content.empty() && content[0] == 0xFEFF)
        content.erase(content.begin());

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = content.size();
        std::wstring line = content.substr(pos, eol - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        pos = eol + 1;
        if (line.empty() || line[0] == L'[' || line[0] == L';') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = line.substr(0, eq);
        std::wstring v = line.substr(eq + 1);
        if (::_wcsicmp(k.c_str(), key.c_str()) != 0) continue;
        int mode = _wtoi(v.c_str());
        if (mode >= 0 && mode <= static_cast<int>(ViewMode::Tiles))
            return static_cast<ViewMode>(mode);
        break;
    }
    return kDefault;
}

void CMainWnd::SaveFolderViewForPath(const std::wstring& path, ViewMode mode) const
{
    std::wstring key = NormalizeViewKey(path);
    if (key.empty())
        return;

    std::wstring file = GetFolderViewsFilePath();
    std::map<std::wstring, int> entries;

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, file.c_str(), L"rb") == 0 && fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz >= 4) {
            std::wstring content;
            content.resize(sz / sizeof(wchar_t));
            fread(&content[0], 1, sz, fp);
            if (!content.empty() && content[0] == 0xFEFF)
                content.erase(content.begin());
            size_t pos = 0;
            while (pos < content.size()) {
                size_t eol = content.find(L'\n', pos);
                if (eol == std::wstring::npos) eol = content.size();
                std::wstring line = content.substr(pos, eol - pos);
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                pos = eol + 1;
                if (line.empty() || line[0] == L'[' || line[0] == L';') continue;
                size_t eq = line.find(L'=');
                if (eq == std::wstring::npos) continue;
                std::wstring k = line.substr(0, eq);
                int v = _wtoi(line.substr(eq + 1).c_str());
                if (!k.empty() && v >= 0 && v <= static_cast<int>(ViewMode::Tiles))
                    entries[k] = v;
            }
        }
        fclose(fp);
    }

    entries[key] = static_cast<int>(mode);

    if (_wfopen_s(&fp, file.c_str(), L"wb") != 0 || !fp)
        return;
    unsigned char bom[2] = { 0xFF, 0xFE };
    fwrite(bom, 1, 2, fp);
    auto writeLine = [&](const std::wstring& s) {
        fwrite(s.c_str(), sizeof(wchar_t), s.size(), fp);
        wchar_t nl = L'\n';
        fwrite(&nl, sizeof(wchar_t), 1, fp);
    };
    writeLine(L"[FolderViews]");
    for (const auto& kv : entries) {
        wchar_t buf[32];
        swprintf_s(buf, L"=%d", kv.second);
        writeLine(kv.first + buf);
    }
    fclose(fp);
}


void CMainWnd::SaveSession() const
{
    SaveLeftNavSplitter();
    // Persist current tab path/filter first
    // (const_cast not needed — write from copies)
    std::wstring path = GetSessionFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || !fp)
        return;
    // UTF-16 LE BOM
    unsigned char bom[2] = { 0xFF, 0xFE };
    fwrite(bom, 1, 2, fp);

    auto writeLine = [&](const std::wstring& s) {
        fwrite(s.c_str(), sizeof(wchar_t), s.size(), fp);
        wchar_t nl = L'\n';
        fwrite(&nl, sizeof(wchar_t), 1, fp);
    };

    writeLine(L"[Session]");
    {
        wchar_t buf[128];
        swprintf_s(buf, L"ActiveTab=%d", m_activeTab);
        writeLine(buf);
        swprintf_s(buf, L"ViewMode=%d", static_cast<int>(m_viewMode));
        writeLine(buf);
        swprintf_s(buf, L"Recursive=%d", IsRecursiveSearch() ? 1 : 0);
        writeLine(buf);
        swprintf_s(buf, L"ColName=%d", m_colWidthName);
        writeLine(buf);
        swprintf_s(buf, L"ColMTime=%d", m_colWidthMTime);
        writeLine(buf);
        swprintf_s(buf, L"ColType=%d", m_colWidthType);
        writeLine(buf);
        swprintf_s(buf, L"ColSize=%d", m_colWidthSize);
        writeLine(buf);
        swprintf_s(buf, L"SortCol=%d", static_cast<int>(m_sortColumn));
        writeLine(buf);
        swprintf_s(buf, L"SortAsc=%d", m_sortAscending ? 1 : 0);
        writeLine(buf);
        swprintf_s(buf, L"Preview=%d", m_previewVisible ? 1 : 0);
        writeLine(buf);
        swprintf_s(buf, L"ShowHidden=%d", m_showHidden ? 1 : 0);
        writeLine(buf);
    }
    writeLine(L"[Tabs]");
    {
        wchar_t buf[64];
        swprintf_s(buf, L"Count=%d", static_cast<int>(m_tabs.size()));
        writeLine(buf);
    }
    for (int i = 0; i < (int)m_tabs.size(); ++i) {
        std::wstring p = m_tabs[i].path;
        if (i == m_activeTab && !m_currentPath.empty())
            p = m_currentPath;
        wchar_t key[64];
        swprintf_s(key, L"Path%d=", i);
        writeLine(std::wstring(key) + p);
        swprintf_s(key, L"Filter%d=", i);
        writeLine(std::wstring(key) + ((i == m_activeTab) ? m_searchFilter : m_tabs[i].searchFilter));
    }
    fclose(fp);
}

bool CMainWnd::LoadSession()
{
    std::wstring path = GetSessionFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp)
        return false;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 4) { fclose(fp); return false; }
    std::wstring content;
    content.resize(sz / sizeof(wchar_t));
    fread(&content[0], 1, sz, fp);
    fclose(fp);
    if (!content.empty() && content[0] == 0xFEFF)
        content.erase(content.begin());

    int active = 0;
    int viewMode = static_cast<int>(ViewMode::Tiles);
    int recursive = 0;
    int count = 0;
    std::map<int, std::wstring> paths;
    std::map<int, std::wstring> filters;

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = content.size();
        std::wstring line = content.substr(pos, eol - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        pos = eol + 1;
        if (line.empty() || line[0] == L'[' || line[0] == L';') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq);
        std::wstring val = line.substr(eq + 1);
        if (key == L"ActiveTab") active = _wtoi(val.c_str());
        else if (key == L"ViewMode") viewMode = _wtoi(val.c_str());
        else if (key == L"Recursive") recursive = _wtoi(val.c_str());
        else if (key == L"ColName") m_colWidthName = (std::max)(60, _wtoi(val.c_str()));
        else if (key == L"ColMTime") m_colWidthMTime = (std::max)(50, _wtoi(val.c_str()));
        else if (key == L"ColType") m_colWidthType = (std::max)(50, _wtoi(val.c_str()));
        else if (key == L"ColSize") m_colWidthSize = (std::max)(50, _wtoi(val.c_str()));
        else if (key == L"SortCol") {
            int sc = _wtoi(val.c_str());
            if (sc >= 0 && sc <= 3) m_sortColumn = static_cast<SortColumn>(sc);
        }
        else if (key == L"SortAsc") m_sortAscending = (_wtoi(val.c_str()) != 0);
        else if (key == L"Preview") m_previewVisible = (_wtoi(val.c_str()) != 0);
        else if (key == L"ShowHidden") m_showHidden = (_wtoi(val.c_str()) != 0);
        else if (key == L"Count") count = _wtoi(val.c_str());
        else if (key.size() > 4 && key.compare(0, 4, L"Path") == 0)
            paths[_wtoi(key.c_str() + 4)] = val;
        else if (key.size() > 6 && key.compare(0, 6, L"Filter") == 0)
            filters[_wtoi(key.c_str() + 6)] = val;
    }

    if (count <= 0 || paths.empty())
        return false;

    m_tabs.clear();
    m_activeTab = -1;
    for (int i = 0; i < count; ++i) {
        auto it = paths.find(i);
        if (it == paths.end() || it->second.empty()) continue;
        TabInfo tab;
        tab.path = it->second;
        auto fit = filters.find(i);
        if (fit != filters.end())
            tab.searchFilter = fit->second;
        m_tabs.push_back(tab);
    }
    if (m_tabs.empty())
        return false;

    if (viewMode >= 0 && viewMode <= static_cast<int>(ViewMode::Tiles))
        m_viewMode = static_cast<ViewMode>(viewMode);
    if (m_pChkRecursive)
        m_pChkRecursive->Selected(recursive != 0);
    ApplyColumnWidths();
    SetPreviewVisible(m_previewVisible);
    UpdateHeaderSortIndicators();

    if (active < 0 || active >= (int)m_tabs.size())
        active = 0;
    ActivateTab(active);
    RebuildBreadcrumb();
    UpdateStatus(_T("已恢复上次会话"));
    return true;
}

// ---- Drag-drop -----------------------------------------------------------

void CMainWnd::InitDragDrop()
{
    if (!m_hWnd) return;
    auto* dt = new FastFileDropTarget(this);
    if (SUCCEEDED(::RegisterDragDrop(m_hWnd, dt))) {
        m_pDropTarget = dt;
    } else {
        dt->Release();
        m_pDropTarget = nullptr;
    }
}

void CMainWnd::UninitDragDrop()
{
    if (m_hWnd && m_pDropTarget) {
        ::RevokeDragDrop(m_hWnd);
        m_pDropTarget->Release();
        m_pDropTarget = nullptr;
    }
}

DWORD CMainWnd::HitTestDropPath(POINT ptScreen, std::wstring& outDir) const
{
    outDir.clear();
    POINT pt = ptScreen;
    ::ScreenToClient(m_hWnd, &pt);

    // C: drop onto favorites bar -> pin (not file copy)
    if (IsOverFavoritesBar(pt)) {
        outDir = kFavoritePinPath;
        return DROPEFFECT_LINK;
    }

    // Tree folder?
    if (m_pDirTree) {
        CControlUI* hit = m_PaintManager.FindControl(pt);
        CControlUI* p = hit;
        while (p) {
            if (p->GetInterface(DUI_CTR_TREENODE)) {
                CTreeNodeUI* node = static_cast<CTreeNodeUI*>(p);
                CDuiString ud = node->GetUserData();
                if (!ud.IsEmpty()) {
                    std::wstring path = ud.GetData();
                    if (IsThisPcPath(path) || path == kPendingMarker)
                        return DROPEFFECT_NONE;
                    DWORD attrs = ::GetFileAttributesW(path.c_str());
                    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                        outDir = NormalizePath(path);
                        return DROPEFFECT_COPY;
                    }
                }
                break;
            }
            p = p->GetParent();
        }
    }

    // List / icon folder item?
    CControlUI* item = nullptr;
    if (IsTileViewMode() && m_pIconTiles) {
        CControlUI* hit = m_PaintManager.FindControl(pt);
        CControlUI* t = hit;
        while (t && t->GetParent() != m_pIconTiles)
            t = t->GetParent();
        if (t && t->GetParent() == m_pIconTiles)
            item = t;
    } else if (m_pFileList) {
        CControlUI* hit = m_PaintManager.FindControl(pt);
        item = FindListItemRoot(hit);
        if (item && item->GetParent() != static_cast<CControlUI*>(m_pFileList)
            && item->GetParent() && item->GetParent()->GetParent() != static_cast<CControlUI*>(m_pFileList)) {
            // still ok if UserData set
        }
    }
    if (item && !item->GetUserData().IsEmpty()) {
        const bool isDir = IsTileViewMode()
            ? ((item->GetTag() & 1) != 0)
            : (item->GetTag() != 0);
        if (isDir) {
            outDir = NormalizePath(item->GetUserData().GetData());
            if (!outDir.empty() && !IsThisPcPath(outDir))
                return DROPEFFECT_COPY;
            outDir.clear();
        }
    }

    // Empty area of list → current folder
    if (!IsThisPcPath(m_currentPath) && !m_currentPath.empty()) {
        outDir = m_currentPath;
        return DROPEFFECT_COPY;
    }
    return DROPEFFECT_NONE;
}

std::wstring CMainWnd::ResolveDropDirectory(POINT ptScreen) const
{
    std::wstring d;
    HitTestDropPath(ptScreen, d);
    return d;
}

bool CMainWnd::TransferWithShell(const std::vector<std::wstring>& srcPaths,
    const std::wstring& destDir, bool move)
{
    if (srcPaths.empty() || destDir.empty() || IsThisPcPath(destDir))
        return false;

    std::wstring from;
    for (const auto& p : srcPaths) {
        // prevent drop into self / child
        std::wstring src = NormalizePath(p);
        std::wstring dst = NormalizePath(destDir);
        if (_wcsicmp(src.c_str(), dst.c_str()) == 0)
            continue;
        std::wstring prefix = src;
        if (!prefix.empty() && prefix.back() != L'\\') prefix.push_back(L'\\');
        if (dst.size() >= prefix.size()
            && _wcsnicmp(dst.c_str(), prefix.c_str(), (int)prefix.size()) == 0)
            continue;
        from += p;
        from.push_back(L'\0');
    }
    if (from.empty()) {
        UpdateStatus(_T("不能拖放到自身或子目录"));
        return false;
    }
    from.push_back(L'\0');

    std::wstring to = destDir;
    if (!to.empty() && to.back() != L'\\') to.push_back(L'\\');
    to.push_back(L'\0');

    SHFILEOPSTRUCTW op = {};
    op.hwnd = m_hWnd;
    op.wFunc = move ? FO_MOVE : FO_COPY;
    op.pFrom = from.c_str();
    op.pTo = to.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMMKDIR;

    int r = ::SHFileOperationW(&op);
    if (r != 0 || op.fAnyOperationsAborted) {
        CDuiString tip;
        tip.Format(_T("%s失败 (%d)"), move ? _T("移动") : _T("复制"), r);
        UpdateStatus(tip.GetData());
        return false;
    }
    CDuiString tip;
    tip.Format(_T("已%s %d 项 → %s"), move ? _T("移动") : _T("复制"),
        (int)srcPaths.size(), destDir.c_str());
    UpdateStatus(tip.GetData());
    RefreshListing();
    return true;
}

bool CMainWnd::PerformDropTransfer(const std::vector<std::wstring>& srcPaths,
    const std::wstring& destDir, DWORD effect)
{
    if (destDir == kFavoritePinPath) {
        int pinned = 0;
        for (const auto& p : srcPaths) {
            DWORD attrs = ::GetFileAttributesW(p.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES) continue;
            if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
            if (PinFavorite(p)) ++pinned;
        }
        if (pinned > 0) {
            CDuiString tip;
            tip.Format(_T("已固定 %d 个文件夹到收藏栏"), pinned);
            UpdateStatus(tip.GetData());
            return true;
        }
        UpdateStatus(_T("只能将文件夹固定到收藏栏"));
        return false;
    }
    const bool move = (effect & DROPEFFECT_MOVE) != 0;
    if (move)
        return TransferWithShell(srcPaths, destDir, true);
    return TransferWithBackgroundCopy(srcPaths, destDir);
}

bool CMainWnd::BeginDragSelectedItems()
{
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.empty())
        return false;

    std::vector<std::wstring> paths;
    paths.reserve(items.size());
    for (const auto& it : items)
        paths.push_back(it.path);

    auto* data = new HDropDataObject(paths);
    auto* src = new DropSource();
    DWORD effect = 0;
    m_inDoDragDrop = true;
    HRESULT hr = ::DoDragDrop(data, src, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    m_inDoDragDrop = false;
    data->Release();
    src->Release();

    if (hr == DRAGDROP_S_DROP) {
        if (effect & DROPEFFECT_LINK) {
            // Favorites pin handled inside PerformDropTransfer
            return true;
        }
        if (effect & DROPEFFECT_MOVE) {
            // External move: refresh source listing
            RefreshListing();
            UpdateStatus(_T("已通过拖拽移动"));
        } else if (effect & DROPEFFECT_COPY) {
            UpdateStatus(_T("已通过拖拽复制"));
        }
        return true;
    }
    return false;
}

CControlUI* CMainWnd::HitTestFileItem(POINT ptClient) const
{
    CControlUI* hit = m_PaintManager.FindControl(ptClient);
    if (!hit) return nullptr;
    if (IsTileViewMode() && m_pIconTiles) {
        CControlUI* t = hit;
        while (t && t->GetParent() != m_pIconTiles)
            t = t->GetParent();
        return (t && t->GetParent() == m_pIconTiles) ? t : nullptr;
    }
    return FindListItemRoot(hit);
}

CTreeNodeUI* CMainWnd::HitTestTreeNode(POINT ptClient) const
{
    CControlUI* hit = m_PaintManager.FindControl(ptClient);
    CControlUI* p = hit;
    while (p) {
        if (p->GetInterface(DUI_CTR_TREENODE))
            return static_cast<CTreeNodeUI*>(p);
        p = p->GetParent();
    }
    return nullptr;
}


// ---- View modes ----------------------------------------------------------

bool CMainWnd::IsTileViewMode() const
{
    return m_viewMode != ViewMode::Details;
}

void CMainWnd::GetViewMetrics(int& tileW, int& tileH, int& iconPx, int& childPad, int& maxLabel) const
{
    // Design metrics @ 96 DPI; scale for Per-Monitor awareness.
    switch (m_viewMode) {
    case ViewMode::ExtraLargeIcons:
        tileW = 200; tileH = 220; iconPx = 128; childPad = UiTokens::TileChildPadXLarge; maxLabel = 22; break;
    case ViewMode::LargeIcons:
        tileW = 128; tileH = 148; iconPx = 96; childPad = UiTokens::TileChildPadLarge; maxLabel = 18; break;
    case ViewMode::MediumIcons:
        tileW = 100; tileH = 108; iconPx = 48; childPad = UiTokens::TileChildPadMedium; maxLabel = 16; break;
    case ViewMode::List:
        tileW = 180; tileH = UiTokens::DetailsRowH; iconPx = UiTokens::DetailsIconPx; childPad = UiTokens::TileChildPadList; maxLabel = 28; break;
    case ViewMode::Tiles:
        // Fits three columns in the normal content area at 150% scaling while
        // retaining an Explorer-like icon and a single readable label line.
        tileW = 190; tileH = 52; iconPx = 40; childPad = UiTokens::TileChildPadMedium; maxLabel = 24; break;
    case ViewMode::Details:
    default:
        tileW = 100; tileH = 108; iconPx = 48; childPad = UiTokens::TileChildPadMedium; maxLabel = 16; break;
    }
    if (IsThisPcPath(m_currentPath) && m_viewMode == ViewMode::Tiles) {
        tileW = 280; tileH = 72; iconPx = 40; maxLabel = 48;
    }
    tileW = DpiScale(tileW);
    tileH = DpiScale(tileH);
    iconPx = DpiScale(iconPx);
    childPad = DpiScale(childPad);
    // maxLabel stays character count (not pixels)
}

void CMainWnd::ApplyTileLayoutMetrics()
{
    if (!m_pIconTiles) return;
    int tileW = 100, tileH = 108, iconPx = 48, childPad = 6, maxLabel = 16;
    GetViewMetrics(tileW, tileH, iconPx, childPad, maxLabel);
    m_iconPx = iconPx;
    SIZE sz = { tileW, tileH };
    m_pIconTiles->SetItemSize(sz);
    {
        CDuiString pad;
        pad.Format(_T("%d"), childPad);
        m_pIconTiles->SetAttribute(_T("childpadding"), pad.GetData());
        m_pIconTiles->SetAttribute(_T("childvpadding"), pad.GetData());
    }
    if (m_pIconScroll) {
        {
            const int p = (m_viewMode == ViewMode::List)
                ? DpiScale(UiTokens::TilePadCompact)
                : DpiScale(UiTokens::TilePadNormal);
            CDuiString pad;
            pad.Format(_T("%d,%d,%d,%d"), p, p, p, p);
            m_pIconScroll->SetAttribute(_T("padding"), pad);
        }
    }
    if (m_pIconTiles)
        m_pIconTiles->EnableScrollBar(true, false);
    ApplyFileViewScrollBars();
}

void CMainWnd::SetViewMode(ViewMode mode)
{
    if (m_viewMode == mode) {
        UpdateViewModeButtons();
        return;
    }
    m_viewMode = mode;
    m_iconAnchor = -1;
    m_lastIconClickTile = nullptr;
    m_lastIconClickTick = 0;
    if (!m_currentPath.empty())
        SaveFolderViewForPath(m_currentPath, mode);
    UpdateViewModeButtons();

    // 步骤2：切视图复用已枚举的 listing，避免重新扫盘
    if (m_hasListingCache
        && PathEquals(m_listingPath, m_currentPath)
        && m_listingFilter == m_searchFilter
        && m_listingRecursive == IsRecursiveSearch()) {
        RebuildCurrentViewFromCache();
        return;
    }
    RefreshListing();
}

void CMainWnd::UpdateViewModeButtons()
{
    struct Pair { LPCTSTR name; ViewMode mode; };
    const Pair buttons[] = {
        { _T("btn_view_xlarge"),  ViewMode::ExtraLargeIcons },
        { _T("btn_view_large"),   ViewMode::LargeIcons },
        { _T("btn_view_medium"),  ViewMode::MediumIcons },
        { _T("btn_view_list"),    ViewMode::List },
        { _T("btn_view_details"), ViewMode::Details },
        { _T("btn_view_tiles"),   ViewMode::Tiles },
    };
    auto styleActive = [](CButtonUI* b) {
        if (!b) return;
        b->SetAttribute(_T("bkcolor"), _T("#FFE8E8E8"));
        b->SetAttribute(_T("textcolor"), _T("#FF1A1A1A"));
        b->SetAttribute(_T("bordercolor"), _T("#FFC8C8C8"));
        b->Invalidate();
    };
    auto styleIdle = [](CButtonUI* b) {
        if (!b) return;
        b->SetAttribute(_T("bkcolor"), _T("#00FFFFFF"));
        b->SetAttribute(_T("textcolor"), _T("#FF3B3B3B"));
        b->SetAttribute(_T("bordercolor"), _T("#00FFFFFF"));
        b->Invalidate();
    };
    for (const auto& b : buttons) {
        auto* btn = static_cast<CButtonUI*>(m_PaintManager.FindControl(b.name));
        if (m_viewMode == b.mode) styleActive(btn);
        else styleIdle(btn);
    }

    const bool tiles = IsTileViewMode();
    if (m_pFileList) m_pFileList->SetVisible(!tiles);
    if (m_pIconScroll) m_pIconScroll->SetVisible(tiles);
}

void CMainWnd::ClearIconView()
{
    CancelThumbJobs();
    if (m_pIconTiles)
        m_pIconTiles->RemoveAll();
    m_iconAnchor = -1;
    m_lastIconClickTile = nullptr;
    m_lastIconClickTick = 0;
    m_virtPoolCount = 0;
    m_virtFirstIndex = 0;
    m_pVirtSpacerBefore = nullptr;
    m_pVirtSpacerAfter = nullptr;
}

void CMainWnd::ClearIconSelection()
{
    if (!m_pIconTiles) return;
    const int n = m_pIconTiles->GetCount();
    for (int i = 0; i < n; ++i) {
        CControlUI* p = m_pIconTiles->GetItemAt(i);
        if (p) SetIconSelected(p, false);
    }
}

void CMainWnd::SetIconSelected(CControlUI* tile, bool selected)
{
    if (!tile) return;
    UINT_PTR tag = tile->GetTag();
    if (selected) tag |= 0x100;
    else tag &= ~static_cast<UINT_PTR>(0x100);
    tile->SetTag(tag);
    ApplyIconSelectionVisual(tile);

    UpdateListingStatusTip();
}

void CMainWnd::ApplyIconSelectionVisual(CControlUI* tile)
{
    if (!tile) return;
    const bool selected = (tile->GetTag() & 0x100) != 0;
    if (selected) {
        tile->SetAttribute(_T("bkcolor"), UiTokens::ColorListSelected);
        tile->SetAttribute(_T("bordercolor"), UiTokens::ColorBorder);
        tile->SetAttribute(_T("bordersize"), _T("1"));
        tile->SetAttribute(_T("hotbkcolor"), UiTokens::ColorListHover);
        tile->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorListSelected);
    } else {
        tile->SetAttribute(_T("bkcolor"), UiTokens::ColorContent);
        tile->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
        tile->SetAttribute(_T("bordersize"), _T("0"));
        tile->SetAttribute(_T("hotbkcolor"), UiTokens::ColorListHover);
        tile->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorListSelected);
    }
    tile->Invalidate();
}

int CMainWnd::FindIconIndex(CControlUI* tile) const
{
    if (!m_pIconTiles || !tile) return -1;
    const int n = m_pIconTiles->GetCount();
    for (int i = 0; i < n; ++i) {
        if (m_pIconTiles->GetItemAt(i) == tile)
            return m_iconVirtMode ? (m_virtFirstIndex + i) : i;
    }
    return -1;
}

void CMainWnd::SelectIconRange(int from, int to)
{
    if (!m_pIconTiles) return;
    if (from > to) std::swap(from, to);
    const int nTiles = m_pIconTiles->GetCount();
    if (m_iconVirtMode) {
        const int total = (int)m_flatListing.size();
        from = (std::max)(0, from);
        to = (std::min)(total - 1, to);
        for (int i = 0; i < nTiles; ++i) {
            const int flat = m_virtFirstIndex + i;
            SetIconSelected(m_pIconTiles->GetItemAt(i), flat >= from && flat <= to);
        }
        return;
    }
    from = (std::max)(0, from);
    to = (std::min)(nTiles - 1, to);
    for (int i = 0; i < nTiles; ++i)
        SetIconSelected(m_pIconTiles->GetItemAt(i), i >= from && i <= to);
}

void CMainWnd::ActivateIconTile(CControlUI* tile)
{
    if (!tile) return;
    CDuiString ud = tile->GetUserData();
    if (ud.IsEmpty()) return;
    const bool isDir = (tile->GetTag() & 1) != 0;
    if (isDir)
        NavigateTo(ud.GetData(), true);
    else {
        ::ShellExecuteW(m_hWnd, L"open", ud.GetData(), nullptr, nullptr, SW_SHOWNORMAL);
        CDuiString tip;
        tip.Format(_T("已打开: %s"), ud.GetData());
        UpdateStatus(tip.GetData());
    }
}

void CMainWnd::OnIconTileClick(CControlUI* tile)
{
    if (!tile || !m_pIconTiles) return;

    const DWORD now = ::GetTickCount();
    const DWORD dbl = ::GetDoubleClickTime();
    if (tile == m_lastIconClickTile && (now - m_lastIconClickTick) <= dbl) {
        m_lastIconClickTick = 0;
        m_lastIconClickTile = nullptr;
        if ((tile->GetTag() & 0x100) == 0) {
            ClearIconSelection();
            SetIconSelected(tile, true);
        }
        ActivateIconTile(tile);
        return;
    }
    m_lastIconClickTick = now;
    m_lastIconClickTile = tile;

    const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const int idx = FindIconIndex(tile);

    if (shift && m_iconAnchor >= 0 && idx >= 0) {
        SelectIconRange(m_iconAnchor, idx);
    } else if (ctrl) {
        const bool on = (tile->GetTag() & 0x100) == 0;
        SetIconSelected(tile, on);
        if (idx >= 0) m_iconAnchor = idx;
    } else {
        ClearIconSelection();
        SetIconSelected(tile, true);
        if (idx >= 0) m_iconAnchor = idx;
    }

    m_PaintManager.SetFocus(tile);

    std::vector<ClipboardItem> sel;
    CollectSelectedItems(sel);
    CDuiString tip;
    if (sel.size() <= 1)
        tip.Format(_T("已选 1 项（Ctrl/Shift 多选，双击打开）"));
    else
        tip.Format(_T("已选 %d 项"), static_cast<int>(sel.size()));
    UpdateStatus(tip.GetData());
}

void CMainWnd::ApplyTileIconImage(CControlUI* tile, const std::wstring& bmp,
    int tileW, int tileH, int iconPx, bool listMode, bool tilesMode)
{
    if (!tile || bmp.empty()) return;
    CDuiString imgAttr;
    if (listMode) {
        const int y = (tileH - iconPx) / 2;
        imgAttr.Format(_T("file='%s' dest='4,%d,%d,%d'"),
            bmp.c_str(), y, 4 + iconPx, y + iconPx);
    } else if (tilesMode) {
        const int y = (tileH - iconPx) / 2;
        imgAttr.Format(_T("file='%s' dest='8,%d,%d,%d'"),
            bmp.c_str(), y, 8 + iconPx, y + iconPx);
    } else {
        const int x0 = (tileW - iconPx) / 2;
        const int y0 = DpiScale(UiTokens::SpaceSm);
        imgAttr.Format(_T("file='%s' dest='%d,%d,%d,%d'"),
            bmp.c_str(), x0, y0, x0 + iconPx, y0 + iconPx);
    }
    tile->SetAttribute(_T("foreimage"), imgAttr.GetData());
    tile->SetAttribute(_T("hotforeimage"), imgAttr.GetData());
}

void CMainWnd::RebuildDetailsView(const std::vector<DirEntry>& dirs,
    const std::vector<DirEntry>& files, bool /*truncated*/)
{
    if (!m_pFileList) return;
    UpdateViewModeButtons();
    StopDetailsFill();
    m_pFileList->SetVisible(false);
    StartDetailsProgressiveFill(dirs, files);
}

bool CMainWnd::TryReuseIconsView(const std::vector<DirEntry>& dirs,
    const std::vector<DirEntry>& files)
{
    if (!m_pIconTiles) return false;
    std::vector<DirEntry> all;
    all.reserve(dirs.size() + files.size());
    all.insert(all.end(), dirs.begin(), dirs.end());
    all.insert(all.end(), files.begin(), files.end());

    const int n = m_pIconTiles->GetCount();
    if (n != static_cast<int>(all.size()) || n <= 0)
        return false;

    // 路径不一致则不能复用控件
    for (int i = 0; i < n; ++i) {
        CControlUI* p = m_pIconTiles->GetItemAt(i);
        if (!p) return false;
        CDuiString ud = p->GetUserData();
        if (ud.IsEmpty() || ::_wcsicmp(ud.GetData(), all[i].fullPath.c_str()) != 0)
            return false;
    }

    CancelThumbJobs();
    ApplyTileLayoutMetrics();

    int tileW = 100, tileH = 108, iconPx = 48, childPad = 6, maxLabel = 16;
    GetViewMetrics(tileW, tileH, iconPx, childPad, maxLabel);
    m_iconPx = iconPx;
    const bool listMode = (m_viewMode == ViewMode::List);
    const bool tilesMode = (m_viewMode == ViewMode::Tiles);
    const UINT gen = m_thumbGeneration.load();

    for (int i = 0; i < n; ++i) {
        auto* tile = static_cast<CButtonUI*>(m_pIconTiles->GetItemAt(i));
        if (!tile) continue;
        const DirEntry& e = all[i];

        tile->SetFixedWidth(tileW);
        tile->SetFixedHeight(tileH);

        if (listMode) {
            tile->SetAttribute(_T("align"), _T("left"));
            tile->SetAttribute(_T("valign"), _T("vcenter"));
            {
            CDuiString tp; tp.Format(_T("%d,%d,%d,%d"), DpiScale(24), DpiScale(0), DpiScale(4), DpiScale(0));
            tile->SetAttribute(_T("textpadding"), tp);
        }
        } else if (tilesMode) {
            tile->SetAttribute(_T("align"), _T("left"));
            tile->SetAttribute(_T("valign"), _T("vcenter"));
            {
            CDuiString tp; tp.Format(_T("%d,%d,%d,%d"), DpiScale(56), DpiScale(4), DpiScale(8), DpiScale(14));
            tile->SetAttribute(_T("textpadding"), tp);
        }
        } else {
            tile->SetAttribute(_T("align"), _T("center"));
            tile->SetAttribute(_T("valign"), _T("bottom"));
            {
            CDuiString tp; tp.Format(_T("%d,%d,%d,%d"), DpiScale(4), DpiScale(4), DpiScale(4), DpiScale(6));
            tile->SetAttribute(_T("textpadding"), tp);
        }
        }
        tile->SetAttribute(_T("endellipsis"), _T("true"));

                std::wstring label = e.name;
        // Tiles/icon: folder label = name only (never append 文件夹).
        if (tilesMode && !e.isDir) {
            std::wstring typeText = L"文件";
            std::wstring sizeText = FormatFileSize(e.size);
            std::wstring line2 = sizeText.empty() ? typeText : (typeText + L"  " + sizeText);
            if (label.size() > static_cast<size_t>(maxLabel))
                label = label.substr(0, maxLabel - 1) + L"…";
            label = label + L"\n" + line2;
        } else {
            if (label.size() > static_cast<size_t>(maxLabel))
                label = label.substr(0, maxLabel - 1) + L"…";
        }
        tile->SetText(label.c_str());

        if (i < kMaxIconThumbs) {
            std::wstring cached = PeekCachedIconBmp(e.fullPath, e.isDir, iconPx);
            if (!cached.empty()) {
                ApplyTileIconImage(tile, cached, tileW, tileH, iconPx, listMode, tilesMode);
            } else {
                // 尺寸变了：先清旧图，后台填新缩略图
                tile->SetAttribute(_T("foreimage"), _T(""));
                tile->SetAttribute(_T("hotforeimage"), _T(""));
                ThumbJob job;
                job.generation = gen;
                job.index = i;
                job.path = e.fullPath;
                job.isDir = e.isDir;
                job.iconPx = iconPx;
                job.tileW = tileW;
                job.tileH = tileH;
                job.listMode = listMode;
                job.tilesMode = tilesMode;
                EnqueueThumbJob(job);
            }
        }

        if (((i + 1) % kUiBatchSize) == 0)
            PumpUiMessages();
    }

    m_pIconTiles->NeedUpdate();
    if (m_pFileList) m_pFileList->SetVisible(false);
    if (m_pIconScroll) m_pIconScroll->SetVisible(true);
    UpdateViewModeButtons();
    return true;
}

void CMainWnd::RebuildIconsView(const std::vector<DirEntry>& dirs,
    const std::vector<DirEntry>& files, bool /*truncated*/)
{
    if (!m_pIconTiles) {
        RebuildDetailsView(dirs, files, false);
        return;
    }
    std::vector<DirEntry> all;
    all.reserve(dirs.size() + files.size());
    all.insert(all.end(), dirs.begin(), dirs.end());
    all.insert(all.end(), files.begin(), files.end());
    if ((int)all.size() >= kVirtThreshold) {
        if (m_hWnd) ::KillTimer(m_hWnd, kTimerVirtSync);
        RebuildIconsViewVirtual(all);
        UpdateViewModeButtons();
        return;
    }
    if (m_hWnd) ::KillTimer(m_hWnd, kTimerVirtSync);
    m_iconVirtMode = false;
    if (TryReuseIconsView(dirs, files))
        return;
    RebuildIconsViewFull(all);
    UpdateViewModeButtons();
}

bool CMainWnd::IsImageExtension(const std::wstring& name)
{
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = name.substr(dot);
    for (auto& ch : ext) ch = static_cast<wchar_t>(towlower(ch));
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg"
        || ext == L".bmp" || ext == L".gif" || ext == L".tif" || ext == L".tiff"
        || ext == L".webp" || ext == L".ico";
}

bool CMainWnd::IsVideoExtension(const std::wstring& name)
{
    const wchar_t* ext = ::PathFindExtensionW(name.c_str());
    if (!ext || !*ext) return false;
    static const wchar_t* kExts[] = {
        L".mp4", L".mkv", L".avi", L".wmv", L".mov", L".m4v",
        L".webm", L".flv", L".mpeg", L".mpg", L".ts", L".m2ts",
        L".3gp", L".asf", L".vob"
    };
    for (auto e : kExts) {
        if (_wcsicmp(ext, e) == 0) return true;
    }
    return false;
}



bool CMainWnd::EnsureGdiplus()
{
    using namespace Gdiplus;
    static bool ready = false;
    static ULONG_PTR token = 0;
    if (ready) return true;
    GdiplusStartupInput input;
    if (GdiplusStartup(&token, &input, nullptr) != Ok)
        return false;
    ready = true;
    return true;
}

bool CMainWnd::GetPngEncoderClsid(CLSID* pClsid)
{
    if (!pClsid) return false;
    using namespace Gdiplus;
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return false;
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<ImageCodecInfo*>(buf.data());
    GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) {
            *pClsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

void CMainWnd::WipeDirectoryFiles(const std::wstring& dirNoSlash)
{
    if (dirNoSlash.empty()) return;
    WIN32_FIND_DATAW fd = {};
    const std::wstring pattern = dirNoSlash + L"\\*";
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0)))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        const std::wstring full = dirNoSlash + L"\\" + fd.cFileName;
        ::DeleteFileW(full.c_str());
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
}

bool CMainWnd::SaveIconToPng(HICON hIcon, const std::wstring& pngPath, int cx, int cy)
{
    if (!hIcon || cx <= 0 || cy <= 0 || pngPath.empty()) return false;
    if (!EnsureGdiplus()) return false;

    HDC hdc = ::GetDC(nullptr);
    HDC mem = ::CreateCompatibleDC(hdc);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cx;
    bi.bmiHeader.biHeight = -cy; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = ::CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) ::DeleteObject(dib);
        ::DeleteDC(mem);
        ::ReleaseDC(nullptr, hdc);
        return false;
    }
    HGDIOBJ old = ::SelectObject(mem, dib);
    // Zero-fill; DrawIconEx writes real per-pixel alpha. Do NOT force A=255.
    ::ZeroMemory(bits, static_cast<size_t>(cx) * static_cast<size_t>(cy) * 4u);
    ::SetBkMode(mem, TRANSPARENT);
    ::DrawIconEx(mem, 0, 0, hIcon, cx, cy, 0, nullptr, DI_NORMAL);

    // Mask-style icons may leave A=0 on every pixel. Promote colored pixels to
    // opaque only; keep transparent holes (A=0). Never force A=255 on all pixels.
    {
        DWORD* px = static_cast<DWORD*>(bits);
        const int n = cx * cy;
        bool anyAlpha = false;
        for (int i = 0; i < n; ++i) {
            const BYTE a = static_cast<BYTE>((px[i] >> 24) & 0xFFu);
            if (a != 0) { anyAlpha = true; break; }
        }
        if (!anyAlpha) {
            for (int i = 0; i < n; ++i) {
                if ((px[i] & 0x00FFFFFFu) != 0)
                    px[i] |= 0xFF000000u;
            }
        }
    }

    ::SelectObject(mem, old);

    using namespace Gdiplus;
    // Bind scan0 so PNG encoder keeps true alpha (DuiLib needs A<255 somewhere for AlphaBlend).
    Bitmap bmp(cx, cy, cx * 4, PixelFormat32bppARGB, static_cast<BYTE*>(bits));
    bool ok = false;
    if (bmp.GetLastStatus() == Ok) {
        CLSID clsidPng = {};
        if (GetPngEncoderClsid(&clsidPng))
            ok = (bmp.Save(pngPath.c_str(), &clsidPng, nullptr) == Ok);
    }

    ::DeleteObject(dib);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, hdc);
    return ok;
}

bool CMainWnd::SaveImageThumbnailPng(const std::wstring& srcPath, const std::wstring& pngPath, int cx, int cy)
{
    using namespace Gdiplus;
    if (!EnsureGdiplus()) return false;

    Bitmap src(srcPath.c_str());
    if (src.GetLastStatus() != Ok)
        return false;

    Bitmap dst(cx, cy, PixelFormat32bppARGB);
    Graphics g(&dst);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    // Transparent letterbox (PNG true alpha) — DuiLib AlphaBlend; no white halo / black pocket.
    g.Clear(Color(0, 0, 0, 0));

    const int sw = src.GetWidth();
    const int sh = src.GetHeight();
    if (sw <= 0 || sh <= 0) return false;
    double scale = (std::min)(static_cast<double>(cx) / sw, static_cast<double>(cy) / sh);
    int dw = static_cast<int>(sw * scale);
    int dh = static_cast<int>(sh * scale);
    int ox = (cx - dw) / 2;
    int oy = (cy - dh) / 2;
    g.DrawImage(&src, ox, oy, dw, dh);

    CLSID clsidPng = {};
    if (!GetPngEncoderClsid(&clsidPng)) return false;
    return dst.Save(pngPath.c_str(), &clsidPng, nullptr) == Ok;
}

std::wstring CMainWnd::PeekCachedIconBmp(const std::wstring& path, bool isDir, int cx)
{
    if (cx < 16) cx = 16;
    if (cx > 256) cx = 256;
    wchar_t cacheKey[32] = {};
    swprintf_s(cacheKey, L"@%d", cx);
    std::wstring key = path + cacheKey;

    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }

    size_t h = std::hash<std::wstring>{}(key);
    wchar_t name[80] = {};
    swprintf_s(name, L"%08X_%s_%d_v6.png", static_cast<unsigned>(h & 0xFFFFFFFF), isDir ? L"d" : L"f", cx);
    std::wstring bmpPath = m_iconCacheDir + name;
    if (::PathFileExistsW(bmpPath.c_str())) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    return {};
}

std::wstring CMainWnd::GetShellIconBmp(const std::wstring& path, bool isDir, int cx)
{
    if (cx < 16) cx = 16;
    if (cx > 256) cx = 256;

    wchar_t cacheKey[32] = {};
    swprintf_s(cacheKey, L"@%d", cx);
    std::wstring key = path + cacheKey;

    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }

    size_t h = std::hash<std::wstring>{}(key);
    wchar_t name[80] = {};
    swprintf_s(name, L"%08X_%s_%d_v6.png", static_cast<unsigned>(h & 0xFFFFFFFF), isDir ? L"d" : L"f", cx);
    std::wstring bmpPath = m_iconCacheDir + name;

    if (::PathFileExistsW(bmpPath.c_str())) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }

    // Content thumbs (transparent letterbox PNG) only for real image/video files.
    // Chrome/tree/details/folder/drive: HICON only — never SIIGBF_ICONONLY (black pocket).
    const bool wantContentThumb = (cx > 32) && !isDir
        && (IsImageExtension(path) || IsVideoExtension(path));
    if (wantContentThumb && ::PathFileExistsW(path.c_str())
        && ExtractShellItemImage(path, cx, cx, bmpPath)) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }

    if (ExtractShellIconSized(path, isDir, cx, bmpPath)) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }

    if (wantContentThumb && IsImageExtension(path)) {
        if (SaveImageThumbnailPng(path, bmpPath, cx, cx)) {
            std::lock_guard<std::mutex> lock(m_iconCacheMutex);
            m_iconCache[key] = bmpPath;
            return bmpPath;
        }
    }
    return {};
}

std::wstring CMainWnd::GetShellFileIconBmp(const std::wstring& path, bool isDir, int cx)
{
    if (cx < 16) cx = 16;
    if (cx > 256) cx = 256;

    wchar_t cacheKey[32] = {};
    swprintf_s(cacheKey, L"@%d", cx);
    std::wstring key = path + L"#ico" + cacheKey;

    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }

    size_t h = std::hash<std::wstring>{}(key);
    wchar_t name[80] = {};
    swprintf_s(name, L"%08X_%s_%d_ico_v6.png", static_cast<unsigned>(h & 0xFFFFFFFF), isDir ? L"d" : L"f", cx);
    std::wstring bmpPath = m_iconCacheDir + name;

    if (::PathFileExistsW(bmpPath.c_str())) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }

    if (ExtractShellIconSized(path, isDir, cx, bmpPath)) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    return {};
}



bool CMainWnd::SaveHBitmapToPng(HBITMAP hbm, const std::wstring& pngPath)
{
    if (!hbm || pngPath.empty()) return false;
    using namespace Gdiplus;
    if (!EnsureGdiplus()) return false;
    Bitmap bmp(hbm, nullptr);
    if (bmp.GetLastStatus() != Ok) return false;
    CLSID clsidPng = {};
    if (!GetPngEncoderClsid(&clsidPng)) return false;
    return bmp.Save(pngPath.c_str(), &clsidPng, nullptr) == Ok;
}


bool CMainWnd::LetterboxHBitmapToPng(HBITMAP hbm, int cx, int cy, const std::wstring& pngPath)
{
    if (!hbm || cx <= 0 || cy <= 0 || pngPath.empty()) return false;
    using namespace Gdiplus;
    if (!EnsureGdiplus()) return false;

    Bitmap src(hbm, nullptr);
    if (src.GetLastStatus() != Ok) return false;
    const int sw = src.GetWidth();
    const int sh = src.GetHeight();
    if (sw <= 0 || sh <= 0) return false;

    Bitmap dst(cx, cy, PixelFormat32bppARGB);
    Graphics g(&dst);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    // Transparent letterbox (or ColorContent white) — PNG true alpha for AlphaBlend.
    g.Clear(Color(0, 0, 0, 0));
    const double scale = (std::min)(static_cast<double>(cx) / sw, static_cast<double>(cy) / sh);
    const int dw = (std::max)(1, static_cast<int>(sw * scale));
    const int dh = (std::max)(1, static_cast<int>(sh * scale));
    const int ox = (cx - dw) / 2;
    const int oy = (cy - dh) / 2;
    g.DrawImage(&src, ox, oy, dw, dh);

    CLSID clsidPng = {};
    if (!GetPngEncoderClsid(&clsidPng)) return false;
    return dst.Save(pngPath.c_str(), &clsidPng, nullptr) == Ok;
}

bool CMainWnd::ExtractShellItemImage(const std::wstring& path, int cx, int cy, const std::wstring& pngPath)
{
    // Content thumbs only (image/video). Never SIIGBF_ICONONLY — folders/drives use HICON.
    if (path.empty() || cx <= 0 || cy <= 0 || pngPath.empty()) return false;
    IShellItem* psi = nullptr;
    HRESULT hr = ::SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&psi));
    if (FAILED(hr) || !psi) return false;

    IShellItemImageFactory* pFactory = nullptr;
    hr = psi->QueryInterface(IID_PPV_ARGS(&pFactory));
    psi->Release();
    if (FAILED(hr) || !pFactory) return false;

    SIZE sz = { cx, cy };
    HBITMAP hbm = nullptr;
    hr = pFactory->GetImage(sz, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK, &hbm);
    pFactory->Release();
    if (FAILED(hr) || !hbm) return false;

    const bool ok = LetterboxHBitmapToPng(hbm, cx, cy, pngPath);
    ::DeleteObject(hbm);
    return ok;
}

bool CMainWnd::ExtractShellIconSized(const std::wstring& path, bool isDir, int cx, const std::wstring& bmpPath)
{
    int shil = SHIL_LARGE;
    if (cx <= 16) shil = SHIL_SMALL;
    else if (cx <= 32) shil = SHIL_LARGE;
    else if (cx <= 48) shil = SHIL_EXTRALARGE;
    else shil = SHIL_JUMBO;

    SHFILEINFOW sfi = {};
    // Prefer real path lookup so Known Folders (Desktop/Documents/Downloads)
    // keep their special Shell icons. USEFILEATTRIBUTES only as fallback.
    UINT flags = SHGFI_SYSICONINDEX;
    DWORD attrs = 0;
    DWORD_PTR ok = 0;
    if (::PathFileExistsW(path.c_str())) {
        ok = ::SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), flags);
    }
    if (!ok) {
        flags = SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES;
        attrs = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        ok = ::SHGetFileInfoW(path.c_str(), attrs, &sfi, sizeof(sfi), flags);
    }

    IImageList* piml = nullptr;
    HRESULT hr = ::SHGetImageList(shil, IID_IImageList, reinterpret_cast<void**>(&piml));
    if (SUCCEEDED(hr) && piml) {
        HICON hIcon = nullptr;
        hr = piml->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hIcon);
        piml->Release();
        if (SUCCEEDED(hr) && hIcon) {
            bool saved = SaveIconToPng(hIcon, bmpPath, cx, cx);
            ::DestroyIcon(hIcon);
            if (saved) return true;
        }
    }

    sfi = {};
    flags = SHGFI_ICON | ((cx <= 16) ? SHGFI_SMALLICON : SHGFI_LARGEICON);
    if (isDir) flags |= SHGFI_USEFILEATTRIBUTES;
    ok = ::SHGetFileInfoW(path.c_str(), isDir ? FILE_ATTRIBUTE_DIRECTORY : 0,
        &sfi, sizeof(sfi), flags);
    if (!ok || !sfi.hIcon)
        return false;
    bool saved = SaveIconToPng(sfi.hIcon, bmpPath, cx, cx);
    ::DestroyIcon(sfi.hIcon);
    return saved;
}



void CMainWnd::ApplyControlForeIcon(CControlUI* ctrl, const std::wstring& bmp,
    int iconPx, int destX, int destY, bool clearText)
{
    if (!ctrl || bmp.empty() || iconPx <= 0) return;
    CDuiString imgAttr;
    imgAttr.Format(_T("file='%s' dest='%d,%d,%d,%d'"),
        bmp.c_str(), destX, destY, destX + iconPx, destY + iconPx);
    ctrl->SetAttribute(_T("foreimage"), imgAttr.GetData());
    ctrl->SetAttribute(_T("hotforeimage"), imgAttr.GetData());
    if (clearText)
        ctrl->SetText(_T(""));
}

bool CMainWnd::ExtractStockIconSized(int siid, int cx, const std::wstring& bmpPath)
{
    if (cx < 16) cx = 16;
    SHSTOCKICONINFO sii = {};
    sii.cbSize = sizeof(sii);
    // Prefer ICONLOCATION + sized extract for DPI-correct glyphs
    if (SUCCEEDED(::SHGetStockIconInfo(static_cast<SHSTOCKICONID>(siid), SHGSI_ICONLOCATION, &sii))
        && sii.szPath[0] != L'\0') {
        HICON hIcon = nullptr;
        if (SUCCEEDED(::SHDefExtractIconW(sii.szPath, sii.iIcon, 0, &hIcon, nullptr, static_cast<UINT>(cx)))
            && hIcon) {
            const bool saved = SaveIconToPng(hIcon, bmpPath, cx, cx);
            ::DestroyIcon(hIcon);
            if (saved) return true;
        }
    }
    sii = {};
    sii.cbSize = sizeof(sii);
    const UINT fl = SHGSI_ICON | ((cx <= 16) ? SHGSI_SMALLICON : SHGSI_LARGEICON);
    if (FAILED(::SHGetStockIconInfo(static_cast<SHSTOCKICONID>(siid), fl, &sii)) || !sii.hIcon)
        return false;
    const bool saved = SaveIconToPng(sii.hIcon, bmpPath, cx, cx);
    ::DestroyIcon(sii.hIcon);
    return saved;
}

bool CMainWnd::ExtractModuleIconSized(const wchar_t* moduleFile, int index, int cx, const std::wstring& bmpPath)
{
    if (!moduleFile || index < 0 || cx <= 0) return false;
    wchar_t sys[MAX_PATH] = {};
    UINT n = ::GetSystemDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    std::wstring full = sys;
    if (!full.empty() && full.back() != L'\\') full.push_back(L'\\');
    full += moduleFile;

    HICON hIcon = nullptr;
    UINT got = ::PrivateExtractIconsW(full.c_str(), index, cx, cx, &hIcon, nullptr, 1, LR_DEFAULTCOLOR);
    if (got == 0 || !hIcon) {
        // Fallback via ExtractIconEx (avoid identifier "small" — Windows headers macro it).
        HICON hLarge = nullptr;
        HICON hSmall = nullptr;
        if (::ExtractIconExW(full.c_str(), index, &hLarge, &hSmall, 1) > 0) {
            if (cx <= 16) {
                hIcon = hSmall ? hSmall : hLarge;
            } else {
                hIcon = hLarge ? hLarge : hSmall;
            }
            if (hSmall && hSmall != hIcon) ::DestroyIcon(hSmall);
            if (hLarge && hLarge != hIcon) ::DestroyIcon(hLarge);
        }
    }
    if (!hIcon) return false;
    const bool saved = SaveIconToPng(hIcon, bmpPath, cx, cx);
    ::DestroyIcon(hIcon);
    return saved;
}

std::wstring CMainWnd::GetStockIconBmp(int siid, int cx)
{
    if (cx < 16) cx = 16;
    if (cx > 256) cx = 256;
    wchar_t keybuf[64] = {};
    swprintf_s(keybuf, L"stock:%d@%d", siid, cx);
    const std::wstring key = keybuf;

    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }

    size_t h = std::hash<std::wstring>{}(key);
    wchar_t name[80] = {};
    swprintf_s(name, L"stk_%08X_%d_%d_v6.png", static_cast<unsigned>(h & 0xFFFFFFFFu), siid, cx);
    std::wstring bmpPath = m_iconCacheDir + name;
    if (::PathFileExistsW(bmpPath.c_str())) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    if (ExtractStockIconSized(siid, cx, bmpPath)) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    return {};
}

std::wstring CMainWnd::GetModuleIconBmp(const wchar_t* moduleFile, int index, int cx)
{
    if (!moduleFile || index < 0) return {};
    if (cx < 16) cx = 16;
    if (cx > 256) cx = 256;
    wchar_t keybuf[128] = {};
    swprintf_s(keybuf, L"mod:%s#%d@%d", moduleFile, index, cx);
    const std::wstring key = keybuf;

    {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end() && ::PathFileExistsW(it->second.c_str()))
            return it->second;
    }

    size_t h = std::hash<std::wstring>{}(key);
    wchar_t name[96] = {};
    swprintf_s(name, L"mod_%08X_%d_%d_v6.png", static_cast<unsigned>(h & 0xFFFFFFFFu), index, cx);
    std::wstring bmpPath = m_iconCacheDir + name;
    if (::PathFileExistsW(bmpPath.c_str())) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    if (ExtractModuleIconSized(moduleFile, index, cx, bmpPath)) {
        std::lock_guard<std::mutex> lock(m_iconCacheMutex);
        m_iconCache[key] = bmpPath;
        return bmpPath;
    }
    return {};
}

void CMainWnd::ApplyChromeShellIcons()
{
    // Toolbar glyphs: UiTokens::ToolbarIconPx (Win11 command-bar density).
    const int iconPx = DpiScale(UiTokens::ToolbarIconPx);
    const int navIconPx = DpiScale(UiTokens::NavIconPx); // favorites / tree stay 16
    if (iconPx <= 0) return;

    auto applyBtn = [&](LPCTSTR name, const std::wstring& bmp, bool clearText) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c || bmp.empty()) return;
        int bw = c->GetFixedWidth();
        int bh = c->GetFixedHeight();
        if (bw <= 0) bw = DpiScale(UiTokens::ToolbarBtnW);
        if (bh <= 0) bh = DpiScale(UiTokens::ToolbarBtnH);
        int x = (bw - iconPx) / 2;
        int y = (bh - iconPx) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        ApplyControlForeIcon(c, bmp, iconPx, x, y, clearText);
        c->Invalidate();
    };

    auto applyFav = [&](LPCTSTR name, const std::wstring& bmp) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c || bmp.empty()) return;
        const int pad = DpiScale(UiTokens::NavIconPad);
        int bh = c->GetFixedHeight();
        if (bh <= 0) bh = DpiScale(UiTokens::NavRowH);
        int y = (bh - navIconPx) / 2;
        if (y < 0) y = 0;
        ApplyControlForeIcon(c, bmp, navIconPx, pad, y, false);
        CDuiString tp;
        tp.Format(_T("%d,0,%d,0"), pad + navIconPx + DpiScale(UiTokens::NavIconTextGap), DpiScale(UiTokens::NavTextPadR));
        c->SetAttribute(_T("textpadding"), tp.GetData());
        c->Invalidate();
    };

    auto applyFluent = [&](LPCTSTR name, wchar_t glyph) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) return;
        wchar_t text[2] = { glyph, L'\0' };
        c->SetAttribute(_T("foreimage"), _T(""));
        c->SetAttribute(_T("hotforeimage"), _T(""));
        c->SetAttribute(_T("font"), _T("6"));
        c->SetAttribute(_T("textpadding"), _T("0,0,0,0"));
        c->SetText(text);
        c->Invalidate();
    };

    // Windows built-in Segoe MDL2 glyphs keep the command bar visually aligned
    // with Explorer without copying icons or using legacy coloured shell32 art.
    applyFluent(_T("btn_back"), 0xE0A6);
    applyFluent(_T("btn_forward"), 0xE0AB);
    applyFluent(_T("btn_up"), 0xE74A);
    applyFluent(_T("btn_refresh"), 0xE72C);
    applyFluent(_T("btn_new_glyph"), 0xE710);
    applyFluent(_T("btn_cut"), 0xE8C6);
    applyFluent(_T("btn_copy"), 0xE8C8);
    applyFluent(_T("btn_paste"), 0xE77F);
    applyFluent(_T("btn_rename"), 0xE8AC);
    applyFluent(_T("btn_share"), 0xE72D);
    applyFluent(_T("btn_delete"), 0xE74D);
    applyFluent(_T("btn_sort_glyph"), 0xE8CB);
    applyFluent(_T("btn_view_glyph"), 0xE80D);
    applyFluent(_T("btn_more"), 0xE712);
    applyFluent(_T("btn_toggle_preview"), 0xE7F4);
    applyFluent(_T("btn_newfolder"), 0xE710);

    // Keep hidden legacy view buttons iconized for UpdateViewModeButtons
    applyBtn(_T("btn_view_xlarge"), GetModuleIconBmp(L"shell32.dll", 257, iconPx), true);
    applyBtn(_T("btn_view_large"), GetModuleIconBmp(L"shell32.dll", 257, iconPx), true);
    applyBtn(_T("btn_view_medium"), GetStockIconBmp(SIID_IMAGEFILES, iconPx), true);
    applyBtn(_T("btn_view_list"), GetModuleIconBmp(L"shell32.dll", 253, iconPx), true);
    applyBtn(_T("btn_view_details"), GetModuleIconBmp(L"shell32.dll", 253, iconPx), true);
    applyBtn(_T("btn_view_tiles"), GetStockIconBmp(SIID_STACK, iconPx), true);

    // --- Quick Access favorites: real known-folder / stock This PC icons ---
    applyFav(_T("fav_thispc"), GetStockIconBmp(SIID_DESKTOPPC, navIconPx));
    {
        std::wstring docs = GetKnownFolderPath(CSIDL_PERSONAL);
        applyFav(_T("fav_documents"), docs.empty()
            ? GetStockIconBmp(SIID_FOLDER, navIconPx)
            : GetShellIconBmp(docs, true, navIconPx));
    }
    {
        std::wstring desk = GetKnownFolderPath(CSIDL_DESKTOPDIRECTORY);
        applyFav(_T("fav_desktop"), desk.empty()
            ? GetStockIconBmp(SIID_DESKTOPPC, navIconPx)
            : GetShellIconBmp(desk, true, navIconPx));
    }
    {
        std::wstring down = GetDownloadsPath();
        applyFav(_T("fav_downloads"), down.empty()
            ? GetStockIconBmp(SIID_FOLDER, navIconPx)
            : GetShellIconBmp(down, true, navIconPx));
    }

    m_PaintManager.NeedUpdate();
}

void CMainWnd::ApplyTreeNodeIcon(CTreeNodeUI* node, const std::wstring& path)
{
    if (!node) return;
    COptionUI* item = node->GetItemButton();
    if (!item) return;

    const int iconPx = DpiScale(16);
    std::wstring bmp;
    if (IsThisPcPath(path)) {
        bmp = GetStockIconBmp(SIID_DESKTOPPC, iconPx);
    } else if (path.size() >= 2 && path[1] == L':' && (path.size() == 2
        || (path.size() <= 3 && (path.back() == L'\\' || path.back() == L'/')))) {
        // Drive root eg. C: or C:/
        std::wstring drive = path;
        if (drive.back() != L'\\' && drive.back() != L'/')
            drive.push_back(L'\\');
        bmp = GetShellIconBmp(drive, false, iconPx);
        if (bmp.empty())
            bmp = GetStockIconBmp(SIID_DRIVEFIXED, iconPx);
    } else if (!path.empty() && path != kPendingMarker) {
        bmp = GetShellIconBmp(path, true, iconPx);
        if (bmp.empty())
            bmp = GetStockIconBmp(SIID_FOLDER, iconPx);
    }
    if (bmp.empty()) return;

    const int pad = DpiScale(UiTokens::NavIconPad);
    int bh = node->GetFixedHeight();
    if (bh <= 0) bh = DpiScale(UiTokens::TreeRowH);
    int y = (bh - iconPx) / 2;
    if (y < 0) y = 0;
    ApplyControlForeIcon(item, bmp, iconPx, pad, y, false);
    CDuiString tp;
    tp.Format(_T("%d,0,%d,0"), pad + iconPx + DpiScale(UiTokens::NavIconTextGap), DpiScale(UiTokens::NavTextPadR));
    item->SetAttribute(_T("textpadding"), tp.GetData());
}

void CMainWnd::RefreshTreeShellIcons()
{
    if (!m_pDirTree) return;
    std::vector<CTreeNodeUI*> stack;
    const int n = m_pDirTree->GetCount();
    for (int i = 0; i < n; ++i) {
        CControlUI* p = m_pDirTree->GetItemAt(i);
        if (p && p->GetInterface(DUI_CTR_TREENODE))
            stack.push_back(static_cast<CTreeNodeUI*>(p));
    }
    while (!stack.empty()) {
        CTreeNodeUI* node = stack.back();
        stack.pop_back();
        if (!node) continue;
        CDuiString ud = node->GetUserData();
        if (!ud.IsEmpty() && ud != CDuiString(kPendingMarker))
            ApplyTreeNodeIcon(node, ud.GetData());
        const int cc = node->GetCountChild();
        for (int i = 0; i < cc; ++i) {
            CTreeNodeUI* c = node->GetChildNode(i);
            if (c) stack.push_back(c);
        }
    }
}


void CMainWnd::StartThumbWorker()
{
    if (m_thumbThread.joinable()) return;
    m_thumbStop.store(false);
    m_thumbThread = std::thread(&CMainWnd::ThumbWorkerMain, this);
}

void CMainWnd::StopThumbWorker()
{
    m_thumbStop.store(true);
    m_thumbCv.notify_all();
    if (m_thumbThread.joinable())
        m_thumbThread.join();
}

void CMainWnd::CancelThumbJobs()
{
    m_thumbGeneration.fetch_add(1);
    std::lock_guard<std::mutex> lock(m_thumbMutex);
    m_thumbQueue.clear();
}

void CMainWnd::EnqueueThumbJob(const ThumbJob& job)
{
    {
        std::lock_guard<std::mutex> lock(m_thumbMutex);
        m_thumbQueue.push_back(job);
    }
    m_thumbCv.notify_one();
}

void CMainWnd::OnThumbReadyMessage(LPARAM lParam)
{
    auto* payload = reinterpret_cast<ThumbReadyPayload*>(lParam);
    if (!payload) return;

    const bool okGen = (payload->generation == m_thumbGeneration.load());
    CControlUI* tile = nullptr;
    if (okGen && m_pIconTiles && payload->index >= 0) {
        if (m_iconVirtMode) {
            const int local = payload->index - m_virtFirstIndex;
            if (local >= 0 && local < m_pIconTiles->GetCount())
                tile = m_pIconTiles->GetItemAt(local);
        } else if (payload->index < m_pIconTiles->GetCount()) {
            tile = m_pIconTiles->GetItemAt(payload->index);
        }
    }
    if (tile) {
        ApplyTileIconImage(tile, payload->bmpPath,
            payload->tileW, payload->tileH, payload->iconPx,
            payload->listMode, payload->tilesMode);
        tile->Invalidate();
    }
    delete payload;
}

void CMainWnd::ThumbWorkerMain(CMainWnd* self)
{
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    while (true) {
        ThumbJob job;
        {
            std::unique_lock<std::mutex> lock(self->m_thumbMutex);
            self->m_thumbCv.wait(lock, [&] {
                return self->m_thumbStop.load() || !self->m_thumbQueue.empty();
            });
            if (self->m_thumbStop.load() && self->m_thumbQueue.empty())
                break;
            if (self->m_thumbQueue.empty())
                continue;
            job = std::move(self->m_thumbQueue.front());
            self->m_thumbQueue.pop_front();
        }

        if (job.generation != self->m_thumbGeneration.load())
            continue;

        std::wstring bmp = self->GetShellIconBmp(job.path, job.isDir, job.iconPx);
        if (bmp.empty())
            continue;
        if (job.generation != self->m_thumbGeneration.load())
            continue;
        if (!self->m_hWnd || !::IsWindow(self->m_hWnd))
            break;

        auto* payload = new (std::nothrow) ThumbReadyPayload();
        if (!payload) continue;
        payload->generation = job.generation;
        payload->index = job.index;
        payload->bmpPath = std::move(bmp);
        payload->iconPx = job.iconPx;
        payload->tileW = job.tileW;
        payload->tileH = job.tileH;
        payload->listMode = job.listMode;
        payload->tilesMode = job.tilesMode;

        if (!::PostMessageW(self->m_hWnd, kMsgThumbReady, 0, reinterpret_cast<LPARAM>(payload)))
            delete payload;
    }
    ::CoUninitialize();
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

// ===== ABDE extensions (A drop->bg copy, B preview, D polish, E virtualize) =====

bool CMainWnd::IsTextExtension(const std::wstring& name)
{
    const wchar_t* ext = PathFindExtensionW(name.c_str());
    if (!ext || !*ext) return false;
    static const wchar_t* kExts[] = {
        L".txt", L".log", L".md", L".csv", L".json", L".xml", L".ini", L".cfg",
        L".c", L".cpp", L".h", L".hpp", L".cs", L".py", L".js", L".ts", L".html",
        L".htm", L".css", L".bat", L".cmd", L".ps1", L".yml", L".yaml", L".toml",
        L".cmake", L".gitignore", L".dockerignore", L".sql", L".rs", L".go", L".java"
    };
    for (auto e : kExts) {
        if (_wcsicmp(ext, e) == 0) return true;
    }
    return false;
}

void CMainWnd::FlattenListing(std::vector<DirEntry>& out) const
{
    out.clear();
    out.reserve(m_listingDirs.size() + m_listingFiles.size());
    out.insert(out.end(), m_listingDirs.begin(), m_listingDirs.end());
    out.insert(out.end(), m_listingFiles.begin(), m_listingFiles.end());
}

void CMainWnd::SortListingCache()
{
    auto cmp = [this](const DirEntry& a, const DirEntry& b) {
        if (IsThisPcPath(m_currentPath)) {
            const wchar_t da = a.fullPath.empty() ? L'Z' : static_cast<wchar_t>(::towupper(a.fullPath[0]));
            const wchar_t db = b.fullPath.empty() ? L'Z' : static_cast<wchar_t>(::towupper(b.fullPath[0]));
            if (da == L'C') return db != L'C';
            if (db == L'C') return false;
            return da < db;
        }
        int r = 0;
        switch (m_sortColumn) {
        case SortColumn::Size:
            if (a.isDir != b.isDir) return a.isDir && !b.isDir;
            if (a.size < b.size) r = -1;
            else if (a.size > b.size) r = 1;
            else r = ::_wcsicmp(a.name.c_str(), b.name.c_str());
            break;
        case SortColumn::Modified:
            if (a.isDir != b.isDir) return a.isDir && !b.isDir;
            if (a.mtime < b.mtime) r = -1;
            else if (a.mtime > b.mtime) r = 1;
            else r = ::_wcsicmp(a.name.c_str(), b.name.c_str());
            break;
        case SortColumn::Type: {
            if (a.isDir != b.isDir) return a.isDir && !b.isDir;
            const wchar_t* ea = PathFindExtensionW(a.name.c_str());
            const wchar_t* eb = PathFindExtensionW(b.name.c_str());
            r = ::_wcsicmp(ea ? ea : L"", eb ? eb : L"");
            if (r == 0) r = ::_wcsicmp(a.name.c_str(), b.name.c_str());
            break;
        }
        case SortColumn::Name:
        default:
            if (a.isDir != b.isDir) return a.isDir && !b.isDir;
            r = ::_wcsicmp(a.name.c_str(), b.name.c_str());
            break;
        }
        return m_sortAscending ? (r < 0) : (r > 0);
    };
    // Keep dirs/files grouping for Name default; for Size/Type still dirs first via cmp
    std::sort(m_listingDirs.begin(), m_listingDirs.end(), cmp);
    std::sort(m_listingFiles.begin(), m_listingFiles.end(), cmp);
}

void CMainWnd::UpdateHeaderSortIndicators()
{
    if (!m_pFileList) return;
    CListHeaderUI* hdr = m_pFileList->GetHeader();
    if (!hdr) return;
    const wchar_t* arrowsAsc = L" ▲";
    const wchar_t* arrowsDesc = L" ▼";
    const wchar_t* bases[4] = { L"名称", L"修改日期", L"类型", L"大小" };
    for (int i = 0; i < hdr->GetCount() && i < 4; ++i) {
        CControlUI* c = hdr->GetItemAt(i);
        if (!c) continue;
        std::wstring t = bases[i];
        if (static_cast<int>(m_sortColumn) == i)
            t += m_sortAscending ? arrowsAsc : arrowsDesc;
        c->SetText(t.c_str());
    }
}

void CMainWnd::OnHeaderColumnClick(CControlUI* pHeaderItem)
{
    if (!pHeaderItem || !m_pFileList) return;
    CListHeaderUI* hdr = m_pFileList->GetHeader();
    if (!hdr) return;
    int idx = -1;
    for (int i = 0; i < hdr->GetCount(); ++i) {
        if (hdr->GetItemAt(i) == pHeaderItem) { idx = i; break; }
    }
    if (idx < 0 || idx > 3) return;
    auto col = static_cast<SortColumn>(idx);
    if (m_sortColumn == col) m_sortAscending = !m_sortAscending;
    else { m_sortColumn = col; m_sortAscending = true; }
    if (!m_hasListingCache) return;
    SortListingCache();
    UpdateHeaderSortIndicators();
    RebuildCurrentViewFromCache();
}

void CMainWnd::ApplyColumnWidths()
{
    if (!m_pFileList) return;
    CListHeaderUI* hdr = m_pFileList->GetHeader();
    if (!hdr || hdr->GetCount() < 4) return;
    // Order: 名称, 修改日期, 类型, 大小
    if (auto* c = hdr->GetItemAt(0)) c->SetFixedWidth(m_colWidthName);
    if (auto* c = hdr->GetItemAt(1)) c->SetFixedWidth(m_colWidthMTime);
    if (auto* c = hdr->GetItemAt(2)) c->SetFixedWidth(m_colWidthType);
    if (auto* c = hdr->GetItemAt(3)) c->SetFixedWidth(m_colWidthSize);
}

void CMainWnd::CaptureColumnWidths()
{
    if (!m_pFileList) return;
    CListHeaderUI* hdr = m_pFileList->GetHeader();
    if (!hdr || hdr->GetCount() < 4) return;
    if (auto* c = hdr->GetItemAt(0)) {
        int w = c->GetFixedWidth();
        if (w > 40) m_colWidthName = w;
    }
    if (auto* c = hdr->GetItemAt(1)) {
        int w = c->GetFixedWidth();
        if (w > 40) m_colWidthMTime = w;
    }
    if (auto* c = hdr->GetItemAt(2)) {
        int w = c->GetFixedWidth();
        if (w > 40) m_colWidthType = w;
    }
    if (auto* c = hdr->GetItemAt(3)) {
        int w = c->GetFixedWidth();
        if (w > 40) m_colWidthSize = w;
    }
}


void CMainWnd::SyncAddressEditFromPath()
{
    if (!m_pAddressEdit) return;
    if (IsThisPcPath(m_currentPath) || m_currentPath.empty())
        m_pAddressEdit->SetText(_T("此电脑"));
    else
        m_pAddressEdit->SetText(m_currentPath.c_str());
}

void CMainWnd::EnterAddressEditMode()
{
    if (m_addressEditMode) {
        if (m_pAddressEdit)
            m_PaintManager.SetFocus(m_pAddressEdit);
        return;
    }
    m_addressEditMode = true;
    SyncAddressEditFromPath();
    if (m_pBreadcrumb)
        m_pBreadcrumb->SetVisible(false);
    if (m_pAddressEditHost)
        m_pAddressEditHost->SetVisible(true);
    if (m_pPathHost)
        m_pPathHost->NeedUpdate();
    if (m_pAddressEdit) {
        m_PaintManager.SetFocus(m_pAddressEdit);
        // Select-all when native edit window appears
        m_pAddressEdit->SetSelAll();
    }
}

void CMainWnd::ExitAddressEditMode(bool commitNavigate)
{
    if (!m_addressEditMode && !commitNavigate)
        return;
    std::wstring typed;
    if (m_pAddressEdit)
        typed = m_pAddressEdit->GetText().GetData();

    m_addressEditMode = false;
    if (m_pAddressEditHost)
        m_pAddressEditHost->SetVisible(false);
    if (m_pBreadcrumb)
        m_pBreadcrumb->SetVisible(true);

    if (commitNavigate) {
        if (!typed.empty())
            NavigateTo(typed, true);
        else
            RebuildBreadcrumb();
    } else {
        SyncAddressEditFromPath();
        RebuildBreadcrumb();
    }
    if (m_pPathHost)
        m_pPathHost->NeedUpdate();
}

void CMainWnd::RebuildBreadcrumb()
{
    if (!m_pBreadcrumb) return;
    m_pBreadcrumb->RemoveAll();

    auto addSeg = [&](const std::wstring& label, const std::wstring& path, bool isLast) {
        auto* btn = new CButtonUI;
        btn->SetText(label.c_str());
        btn->SetUserData(path.c_str());
        btn->SetName(_T("bc_seg"));
        btn->SetFixedHeight(DpiScale(UiTokens::HitBreadcrumbH));
        btn->SetAttribute(_T("padding"), _T("0,0,0,0"));
        {
            CDuiString tp;
            const int padX = DpiScale(UiTokens::BreadcrumbSegPadX);
            tp.Format(_T("%d,0,%d,0"), padX, padX);
            btn->SetAttribute(_T("textpadding"), tp);
        }
        btn->SetAttribute(_T("align"), _T("center"));
        btn->SetAttribute(_T("valign"), _T("vcenter"));
        btn->SetAttribute(_T("font"), _T("0"));
        btn->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
        btn->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
        btn->SetAttribute(_T("bordersize"), _T("0"));
        btn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorHover);
        btn->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorPressed);
        btn->SetAttribute(_T("textcolor"),
            isLast ? UiTokens::ColorTextPrimary : UiTokens::ColorTextTabIdle);
        const int padX = DpiScale(UiTokens::BreadcrumbSegPadX);
        SIZE labelSize = { 0, 0 };
        if (m_hWnd) {
            HDC dc = ::GetDC(m_hWnd);
            if (dc) {
                HFONT font = m_PaintManager.GetFont(0);
                HGDIOBJ oldFont = font ? ::SelectObject(dc, font) : NULL;
                ::GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &labelSize);
                if (oldFont)
                    ::SelectObject(dc, oldFont);
                ::ReleaseDC(m_hWnd, dc);
            }
        }
        if (labelSize.cx <= 0)
            labelSize.cx = DpiScale(static_cast<int>(label.size()) * 8);
        int w = labelSize.cx + padX * 2 + DpiScale(4);
        if (w < DpiScale(36)) w = DpiScale(36);
        if (w > DpiScale(220)) w = DpiScale(220);
        btn->SetFixedWidth(w);
        m_pBreadcrumb->Add(btn);
        if (!isLast) {
            auto* sep = new CLabelUI;
            sep->SetText(_T(" › "));
            sep->SetFixedWidth(DpiScale(UiTokens::BreadcrumbSepW));
            sep->SetFixedHeight(DpiScale(UiTokens::HitBreadcrumbH));
            sep->SetAttribute(_T("textcolor"), UiTokens::ColorTextMuted);
            sep->SetAttribute(_T("font"), _T("0"));
            sep->SetAttribute(_T("align"), _T("center"));
            sep->SetAttribute(_T("valign"), _T("vcenter"));
            m_pBreadcrumb->Add(sep);
        }
    };

    if (IsThisPcPath(m_currentPath) || m_currentPath.empty()) {
        addSeg(L"此电脑", kThisPcPath, true);
        
    // Click empty trailing area -> editable address (Explorer-style)
    if (!m_addressEditMode) {
        auto* filler = new CButtonUI;
        filler->SetName(_T("bc_edit"));
        filler->SetText(_T(""));
        filler->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
        filler->SetAttribute(_T("hotbkcolor"), UiTokens::ColorTransparent);
        filler->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorTransparent);
        filler->SetAttribute(_T("bordersize"), _T("0"));
        filler->SetFixedHeight(DpiScale(UiTokens::HitBreadcrumbH));
        m_pBreadcrumb->Add(filler);
    }

        m_pBreadcrumb->NeedUpdate();
        return;
    }

    std::wstring path = NormalizePath(m_currentPath);
    std::vector<std::pair<std::wstring, std::wstring>> segs;
    // Drive root
    if (path.size() >= 2 && path[1] == L':') {
        std::wstring drive = path.substr(0, 2) + L"\\";
        segs.push_back({ FormatDriveDisplayName(drive), NormalizePath(drive) });
        std::wstring rest = path.size() > 3 ? path.substr(3) : L"";
        std::wstring acc = NormalizePath(drive);
        size_t start = 0;
        while (start < rest.size()) {
            size_t slash = rest.find(L'\\', start);
            std::wstring part = (slash == std::wstring::npos)
                ? rest.substr(start) : rest.substr(start, slash - start);
            if (!part.empty()) {
                if (!acc.empty() && acc.back() != L'\\') acc.push_back(L'\\');
                acc += part;
                segs.push_back({ part, NormalizePath(acc) });
            }
            if (slash == std::wstring::npos) break;
            start = slash + 1;
        }
    } else {
        segs.push_back({ GetLeafName(path), path });
    }

    // Prefix 此电脑
    addSeg(L"此电脑", kThisPcPath, false);
    for (size_t i = 0; i < segs.size(); ++i)
        addSeg(segs[i].first, segs[i].second, i + 1 == segs.size());
    
    // Click empty trailing area -> editable address (Explorer-style)
    if (!m_addressEditMode) {
        auto* filler = new CButtonUI;
        filler->SetName(_T("bc_edit"));
        filler->SetText(_T(""));
        filler->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
        filler->SetAttribute(_T("hotbkcolor"), UiTokens::ColorTransparent);
        filler->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorTransparent);
        filler->SetAttribute(_T("bordersize"), _T("0"));
        filler->SetFixedHeight(DpiScale(UiTokens::HitBreadcrumbH));
        m_pBreadcrumb->Add(filler);
    }

    m_pBreadcrumb->NeedUpdate();
}

void CMainWnd::OnBreadcrumbSegmentClick(CControlUI* btn)
{
    if (!btn) return;
    CDuiString ud = btn->GetUserData();
    if (ud.IsEmpty()) return;
    NavigateTo(ud.GetData(), true);
}

void CMainWnd::SetPreviewVisible(bool visible)
{
    m_previewVisible = visible;
    // The pane is persistent while enabled. Empty selection shows the current
    // folder summary rather than collapsing the whole layout.
    if (m_pPreviewPane)
        m_pPreviewPane->SetVisible(visible);
    if (m_pBtnTogglePreview) {
        if (visible) {
            m_pBtnTogglePreview->SetAttribute(_T("bkcolor"), UiTokens::ColorAccentSoft);
            m_pBtnTogglePreview->SetAttribute(_T("textcolor"), UiTokens::ColorAccentText);
        } else {
            m_pBtnTogglePreview->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
            m_pBtnTogglePreview->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        }
    }
    if (visible)
        UpdatePreviewForSelection();
}


void CMainWnd::ShowPreviewDetails(bool showMeta, bool showImage, bool showActions)
{
    for (LPCTSTR rowName : {
        _T("preview_row_type"), _T("preview_row_size"),
        _T("preview_row_mtime"), _T("preview_row_ctime")
    }) {
        if (CControlUI* row = m_PaintManager.FindControl(rowName))
            row->SetVisible(showMeta);
    }
    if (CControlUI* row = m_PaintManager.FindControl(_T("preview_row_location")))
        row->SetVisible(showMeta && m_pPreviewLocation && !m_pPreviewLocation->GetText().IsEmpty());
    const struct { LPCTSTR row; CLabelUI* value; } optionalRows[] = {
        { _T("preview_row_dimensions"), m_pPreviewDimensions },
        { _T("preview_row_duration"), m_pPreviewDuration },
        { _T("preview_row_framerate"), m_pPreviewFrameRate },
        { _T("preview_row_bitrate"), m_pPreviewBitRate },
        { _T("preview_row_totalbitrate"), m_pPreviewTotalBitRate },
    };
    for (const auto& item : optionalRows) {
        if (CControlUI* row = m_PaintManager.FindControl(item.row))
            row->SetVisible(showMeta && item.value && !item.value->GetText().IsEmpty());
    }
    if (CControlUI* gap = m_PaintManager.FindControl(_T("preview_gap_title")))
        gap->SetVisible(showImage);
    if (m_pPreviewImage)
        m_pPreviewImage->SetVisible(showImage);
    if (CControlUI* gap = m_PaintManager.FindControl(_T("preview_gap_action")))
        gap->SetVisible(showActions);
    if (CControlUI* share = m_PaintManager.FindControl(_T("btn_preview_share")))
        share->SetVisible(showActions);
    if (CControlUI* gap = m_PaintManager.FindControl(_T("preview_gap_image")))
        gap->SetVisible(showMeta);
    if (CControlUI* title = m_PaintManager.FindControl(_T("preview_details_title")))
        title->SetVisible(showMeta);
    if (CControlUI* gap = m_PaintManager.FindControl(_T("preview_gap_text")))
        gap->SetVisible(false);
    if (m_pPreviewPane)
        m_pPreviewPane->NeedUpdate();
}

void CMainWnd::ClearPreviewMeta()
{
    SetPreviewMeta(L"", L"", L"", L"");
    ClearPreviewExtraMeta();
}

void CMainWnd::SetPreviewMeta(const std::wstring& typeName,
    const std::wstring& sizeText,
    const std::wstring& mtimeText,
    const std::wstring& ctimeText)
{
    if (m_pPreviewType) m_pPreviewType->SetText(typeName.c_str());
    if (m_pPreviewSize) m_pPreviewSize->SetText(sizeText.c_str());
    if (m_pPreviewMTime) m_pPreviewMTime->SetText(mtimeText.c_str());
    if (m_pPreviewCTime) m_pPreviewCTime->SetText(ctimeText.c_str());
}

void CMainWnd::ClearPreviewExtraMeta()
{
    SetPreviewExtraMeta(L"", L"", L"", L"", L"", L"");
}

void CMainWnd::SetPreviewExtraMeta(const std::wstring& location,
    const std::wstring& dimensions,
    const std::wstring& duration,
    const std::wstring& frameRate,
    const std::wstring& bitRate,
    const std::wstring& totalBitRate)
{
    if (m_pPreviewLocation) m_pPreviewLocation->SetText(location.c_str());
    if (m_pPreviewDimensions) m_pPreviewDimensions->SetText(dimensions.c_str());
    if (m_pPreviewDuration) m_pPreviewDuration->SetText(duration.c_str());
    if (m_pPreviewFrameRate) m_pPreviewFrameRate->SetText(frameRate.c_str());
    if (m_pPreviewBitRate) m_pPreviewBitRate->SetText(bitRate.c_str());
    if (m_pPreviewTotalBitRate) m_pPreviewTotalBitRate->SetText(totalBitRate.c_str());
}

std::wstring CMainWnd::FormatFileTimeLocal(const FILETIME& ft)
{
    if (ft.dwHighDateTime == 0 && ft.dwLowDateTime == 0)
        return L"—";
    FILETIME local = {};
    if (!::FileTimeToLocalFileTime(&ft, &local))
        return L"—";
    SYSTEMTIME st = {};
    if (!::FileTimeToSystemTime(&local, &st))
        return L"—";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04u/%02u/%02u %02u:%02u",
        (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
        (unsigned)st.wHour, (unsigned)st.wMinute);
    return buf;
}

std::wstring CMainWnd::QueryShellTypeName(const std::wstring& path, bool isDir)
{
    SHFILEINFOW sfi = {};
    UINT flags = SHGFI_TYPENAME;
    DWORD attrs = 0;
    if (isDir) {
        flags |= SHGFI_USEFILEATTRIBUTES;
        attrs = FILE_ATTRIBUTE_DIRECTORY;
    }
    if (::SHGetFileInfoW(path.c_str(), attrs, &sfi, sizeof(sfi), flags) && sfi.szTypeName[0])
        return sfi.szTypeName;
    return isDir ? L"文件夹" : L"文件";
}

void CMainWnd::FillPreviewMetaFromPath(const std::wstring& path, bool isDir)
{
    std::wstring typeName = QueryShellTypeName(path, isDir);
    std::wstring sizeText = L"—";
    std::wstring mtimeText = L"—";
    std::wstring ctimeText = L"—";

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        const bool dirAttr = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (dirAttr || isDir) {
            sizeText = L"—";
        } else {
            const ULONGLONG bytes =
                (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
            sizeText = FormatFileSize(bytes);
        }
        mtimeText = FormatFileTimeLocal(fad.ftLastWriteTime);
        ctimeText = FormatFileTimeLocal(fad.ftCreationTime);
    }
    SetPreviewMeta(typeName, sizeText, mtimeText, ctimeText);
    SetPreviewExtraMeta(path, L"", L"", L"", L"", L"");
}

std::wstring CMainWnd::QueryImageDimensions(const std::wstring& path)
{
    if (!EnsureGdiplus()) return L"";
    Gdiplus::Bitmap bitmap(path.c_str(), FALSE);
    if (bitmap.GetLastStatus() != Gdiplus::Ok || bitmap.GetWidth() == 0 || bitmap.GetHeight() == 0)
        return L"";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%u x %u", bitmap.GetWidth(), bitmap.GetHeight());
    return buf;
}

void CMainWnd::FillVideoPreviewMeta(const std::wstring& path)
{
    VideoPropertyInfo info = ReadVideoProperties(path);
    std::wstring dimensions;
    if (info.width && info.height) {
        wchar_t buf[64] = {};
        swprintf_s(buf, L"%llu x %llu", info.width, info.height);
        dimensions = buf;
    }
    std::wstring frameRate;
    if (info.frameRateMilli) {
        wchar_t buf[48] = {};
        const double fps = info.frameRateMilli >= 1000
            ? static_cast<double>(info.frameRateMilli) / 1000.0
            : static_cast<double>(info.frameRateMilli);
        swprintf_s(buf, L"%.2f 帧/秒", fps);
        frameRate = buf;
    }

    ULONGLONG bytes = 0;
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        bytes = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    ULONGLONG totalRate = 0;
    if (bytes && info.duration100ns)
        totalRate = (bytes * 8ULL * 10000000ULL) / info.duration100ns;
    const ULONGLONG videoRate = totalRate > info.audioBitRate ? totalRate - info.audioBitRate : totalRate;
    SetPreviewExtraMeta(path, dimensions, FormatMediaDuration(info.duration100ns), frameRate,
        FormatBitRate(videoRate), FormatBitRate(totalRate));
}

void CMainWnd::ClearPreview()
{
    m_previewPath.clear();
    if (!m_previewBmp.empty()) {
        m_PaintManager.RemoveImage(m_previewBmp.c_str());
        ::DeleteFileW(m_previewBmp.c_str());
        m_previewBmp.clear();
    }
    if (!m_previewVisible) {
        if (m_pPreviewPane)
            m_pPreviewPane->SetVisible(false);
        if (m_pPreviewTitle) m_pPreviewTitle->SetText(_T("预览"));
        ClearPreviewMeta();
        if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
        ShowPreviewDetails(false, false, false);
        return;
    }
    if (m_pPreviewPane)
        m_pPreviewPane->SetVisible(true);
    if (m_pPreviewTitle) m_pPreviewTitle->SetText(_T("预览"));
    if (m_pPreviewImage) {
        m_pPreviewImage->SetAttribute(_T("bkimage"), _T(""));
        m_pPreviewImage->SetBkImage(_T(""));
        m_pPreviewImage->Invalidate();
    }
    ClearPreviewMeta();
    if (m_pPreviewText)
        m_pPreviewText->SetText(_T(""));
    UpdatePreviewForCurrentFolder();
}

void CMainWnd::UpdatePreviewForCurrentFolder()
{
    if (!m_previewVisible) return;
    if (m_pPreviewPane)
        m_pPreviewPane->SetVisible(true);

    const int dirCount = static_cast<int>(m_listingDirs.size());
    const int fileCount = static_cast<int>(m_listingFiles.size());
    wchar_t summary[128] = {};
    swprintf_s(summary, L"%d 个文件夹 · %d 个文件", dirCount, fileCount);
    std::wstring sizeSummary = summary;
    if (m_listingTruncated)
        sizeSummary += L"（仅显示部分）";

    if (m_currentPath.empty()) {
        if (m_pPreviewTitle) m_pPreviewTitle->SetText(_T("当前目录"));
        SetPreviewMeta(L"文件夹", L"尚未打开目录", L"—", L"—");
        if (m_pPreviewText)
            m_pPreviewText->SetText(_T("选择文件或文件夹可查看详细信息"));
        ShowPreviewDetails(true, false, false);
        return;
    }

    m_previewPath = m_currentPath;
    std::wstring title = IsThisPcPath(m_currentPath) ? L"此电脑" : GetLeafName(m_currentPath);
    if (title.empty()) title = m_currentPath;
    if (m_pPreviewTitle) m_pPreviewTitle->SetText(title.c_str());

    if (IsThisPcPath(m_currentPath)) {
        SetPreviewMeta(L"此电脑", sizeSummary, L"—", L"—");
    } else {
        FillPreviewMetaFromPath(m_currentPath, true);
        if (m_pPreviewSize) m_pPreviewSize->SetText(sizeSummary.c_str());
    }
    if (m_pPreviewText)
        m_pPreviewText->SetText(_T("当前目录概览\n未选择项目"));

    bool showImage = false;
    if (m_pPreviewImage) {
        m_pPreviewImage->SetBkImage(_T(""));
        showImage = LoadPreviewShellIcon(m_currentPath, true,
            DpiScale(UiTokens::PreviewIconPx));
    }
    ShowPreviewDetails(true, showImage, false);
}
void CMainWnd::UpdatePreviewForSelection()
{
    if (!m_previewVisible) return;
    std::vector<ClipboardItem> items;
    CollectSelectedItems(items);
    if (items.size() == 1) {
        UpdatePreviewPath(items[0].path, items[0].isDir);
        return;
    }
    if (items.empty()) {
        ClearPreview();
        return;
    }

    // Multi-selection has a useful summary, so reveal the pane for it as well.
    if (m_pPreviewPane)
        m_pPreviewPane->SetVisible(true);

    // Multi-select: drop cached path so a later single-select reloads.
    m_previewPath.clear();
    if (!m_previewBmp.empty()) {
        m_PaintManager.RemoveImage(m_previewBmp.c_str());
        ::DeleteFileW(m_previewBmp.c_str());
        m_previewBmp.clear();
    }
    if (m_pPreviewImage) m_pPreviewImage->SetBkImage(_T(""));

    CDuiString title;
    title.Format(_T("已选择 %d 个项目"), (int)items.size());
    if (m_pPreviewTitle) m_pPreviewTitle->SetText(title.GetData());

    // Fast total: sum selected files only (skip folder recursion for UI snappiness).
    ULONGLONG total = 0;
    bool anyFile = false;
    for (const auto& it : items) {
        if (it.isDir) continue;
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (::GetFileAttributesExW(it.path.c_str(), GetFileExInfoStandard, &fad)) {
            total += (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
            anyFile = true;
        }
    }
    SetPreviewMeta(L"—",
        anyFile ? FormatFileSize(total) : L"—",
        L"—", L"—");
    if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
    ClearPreviewExtraMeta();
    ShowPreviewDetails(true, false, false);  // meta only — no empty thumb frame
}

void CMainWnd::UpdatePreviewPath(const std::wstring& path, bool isDir)
{
    if (!m_previewVisible) return;
    if (path.empty()) { ClearPreview(); return; }
    if (m_pPreviewPane)
        m_pPreviewPane->SetVisible(true);
    if (m_previewPath == path) return;
    m_previewPath = path;

    std::wstring leaf = GetLeafName(path);
    if (m_pPreviewTitle) m_pPreviewTitle->SetText(leaf.c_str());
    FillPreviewMetaFromPath(path, isDir);
    if (isDir)
        ClearPreviewExtraMeta();
    else if (IsImageExtension(leaf))
        SetPreviewExtraMeta(path, QueryImageDimensions(path), L"", L"", L"", L"");
    else if (IsVideoExtension(leaf))
        FillVideoPreviewMeta(path);
    ShowPreviewDetails(true, true, true);

    if (isDir) {
        // Folders: HICON->PNG true alpha (never SIIGBF black pocket).
        const int ip = DpiScale(UiTokens::PreviewIconPx);
        if (LoadPreviewShellIcon(path, true, ip)) {
            if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
            return;
        }
        if (m_pPreviewImage) m_pPreviewImage->SetBkImage(_T(""));
        if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
        return;
    }
    if (IsImageExtension(leaf)) {
        if (LoadPreviewImage(path)) {
            if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
            return;
        }
    }
    if (IsVideoExtension(leaf)) {
        if (LoadPreviewShellThumbnail(path,
                DpiScale(UiTokens::PreviewThumbW), DpiScale(UiTokens::PreviewThumbH))) {
            if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
            return;
        }
    }
    if (IsTextExtension(leaf)) {
        if (m_pPreviewImage) {
            m_pPreviewImage->SetBkImage(_T(""));
            m_pPreviewImage->SetVisible(false);
        }
        if (CControlUI* gap = m_PaintManager.FindControl(_T("preview_gap_image")))
            gap->SetVisible(false);
        if (LoadPreviewText(path)) return;
    }
    // Generic files: compact HICON (true alpha PNG)
    {
        const int ip = DpiScale(UiTokens::PreviewIconPx);
        if (LoadPreviewShellIcon(path, false, ip)) {
            if (m_pPreviewText) m_pPreviewText->SetText(_T(""));
            return;
        }
    }
    if (m_pPreviewImage) m_pPreviewImage->SetBkImage(_T(""));
    if (m_pPreviewText) m_pPreviewText->SetText(_T("暂不支持该类型预览"));
}


void CMainWnd::ApplyPreviewImageBk(const std::wstring& pngPath, int imgPxW, int imgPxH, int frameDesignH)
{
    if (!m_pPreviewImage || pngPath.empty() || imgPxW <= 0 || imgPxH <= 0)
        return;

    const int frameH = DpiScale((std::max)(frameDesignH, 1));
    m_pPreviewImage->SetFixedHeight(frameH);

    // Prefer live layout width; fall back to design thumb width.
    int ctrlW = m_pPreviewImage->GetWidth();
    if (ctrlW <= 8)
        ctrlW = DpiScale(UiTokens::PreviewThumbW);
    const int ctrlH = frameH;

    // Center Fit: scale image to fit control, then center (not top-left).
    double sx = static_cast<double>(ctrlW) / static_cast<double>(imgPxW);
    double sy = static_cast<double>(ctrlH) / static_cast<double>(imgPxH);
    double scale = (std::min)(sx, sy);
    if (scale <= 0.0) scale = 1.0;
    int dw = (std::max)(1, static_cast<int>(imgPxW * scale));
    int dh = (std::max)(1, static_cast<int>(imgPxH * scale));
    int ox = (ctrlW - dw) / 2;
    int oy = (ctrlH - dh) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    CDuiString img;
    img.Format(_T("file='%s' dest='%d,%d,%d,%d' source='0,0,%d,%d'"),
        pngPath.c_str(), ox, oy, ox + dw, oy + dh, imgPxW, imgPxH);
    m_pPreviewImage->SetBkImage(img.GetData());
    m_pPreviewImage->Invalidate();
    if (m_pPreviewPane)
        m_pPreviewPane->NeedUpdate();
}

bool CMainWnd::LoadPreviewImage(const std::wstring& path)
{
    if (!m_pPreviewImage) return false;

    // DuiLib caches bitmaps by file path; SetBkImage no-ops when draw-string
    // is unchanged. Reusing one preview bmp left a stale thumb after select.
    if (!m_previewBmp.empty()) {
        m_PaintManager.RemoveImage(m_previewBmp.c_str());
        ::DeleteFileW(m_previewBmp.c_str());
        m_previewBmp.clear();
    }
    m_pPreviewImage->SetBkImage(_T(""));

    const int thumbW = DpiScale(UiTokens::PreviewThumbW);
    const int thumbH = DpiScale(UiTokens::PreviewThumbH);

    ++m_previewSerial;
    wchar_t leaf[64] = {};
    swprintf_s(leaf, L"preview_%u.png", m_previewSerial);
    m_previewBmp = m_iconCacheDir + leaf;
    if (!SaveImageThumbnailPng(path, m_previewBmp, thumbW, thumbH)) {
        m_previewBmp.clear();
        return false;
    }
    m_PaintManager.RemoveImage(m_previewBmp.c_str());

    // Adaptive frame height from letterboxed PNG (transparent letterbox already centered).
    int frameDesignH = UiTokens::PreviewImageH;
    {
        using namespace Gdiplus;
        if (EnsureGdiplus()) {
            Bitmap bmp(m_previewBmp.c_str());
            if (bmp.GetLastStatus() == Ok) {
                const int bw = bmp.GetWidth();
                const int bh = bmp.GetHeight();
                // Content bbox approx: use full PNG size; height scales with AR vs pane width.
                if (bw > 0 && bh > 0) {
                    const int paneW = DpiScale(UiTokens::PreviewThumbW);
                    const double sc = (std::min)(1.0,
                        (std::min)(static_cast<double>(paneW) / bw,
                                   static_cast<double>(DpiScale(UiTokens::PreviewImageH)) / bh));
                    const int fittedH = (std::max)(DpiScale(48), static_cast<int>(bh * sc));
                    frameDesignH = ::MulDiv(fittedH, 96, (int)m_dpi);
                    if (frameDesignH < 48) frameDesignH = 48;
                    if (frameDesignH > UiTokens::PreviewImageH)
                        frameDesignH = UiTokens::PreviewImageH;
                    ApplyPreviewImageBk(m_previewBmp, bw, bh, frameDesignH);
                    return true;
                }
            }
        }
    }
    ApplyPreviewImageBk(m_previewBmp, thumbW, thumbH, frameDesignH);
    return true;
}


bool CMainWnd::LoadPreviewShellIcon(const std::wstring& path, bool isDir, int iconPx)
{
    if (!m_pPreviewImage || path.empty()) return false;
    if (!m_previewBmp.empty()) {
        m_PaintManager.RemoveImage(m_previewBmp.c_str());
        ::DeleteFileW(m_previewBmp.c_str());
        m_previewBmp.clear();
    }
    m_pPreviewImage->SetBkImage(_T(""));

    int ip = iconPx;
    if (ip < 16) ip = 16;
    if (ip > 256) ip = 256;

    ++m_previewSerial;
    wchar_t leaf[64] = {};
    swprintf_s(leaf, L"preview_icon_%u.png", m_previewSerial);
    m_previewBmp = m_iconCacheDir + leaf;

    bool ok = ExtractShellIconSized(path, isDir, ip, m_previewBmp);
    if (!ok && isDir)
        ok = ExtractShellIconSized(path, true, ip, m_previewBmp);
    if (!ok) {
        // Stock folder / document fallback via sized extract of known path fails:
        // use GetStockIconBmp then copy into preview slot.
        std::wstring stock = isDir
            ? GetStockIconBmp(SIID_FOLDER, ip)
            : GetStockIconBmp(SIID_DOCNOASSOC, ip);
        if (!stock.empty() && ::CopyFileW(stock.c_str(), m_previewBmp.c_str(), FALSE))
            ok = true;
    }
    if (!ok) {
        m_previewBmp.clear();
        return false;
    }
    m_PaintManager.RemoveImage(m_previewBmp.c_str());
    ApplyPreviewImageBk(m_previewBmp, ip, ip, UiTokens::PreviewIconCompactH);
    return true;
}

bool CMainWnd::LoadPreviewShellThumbnail(const std::wstring& path, int cx, int cy)
{
    if (!m_pPreviewImage || path.empty()) return false;
    if (!m_previewBmp.empty()) {
        m_PaintManager.RemoveImage(m_previewBmp.c_str());
        ::DeleteFileW(m_previewBmp.c_str());
        m_previewBmp.clear();
    }
    m_pPreviewImage->SetBkImage(_T(""));

    // Physical pixel request (callers pass design or already-scaled; normalize).
    int reqW = cx;
    int reqH = cy;
    if (reqW < 16) reqW = 16;
    if (reqH < 16) reqH = 16;

    ++m_previewSerial;
    wchar_t leaf[64] = {};
    swprintf_s(leaf, L"preview_shell_%u.png", m_previewSerial);
    m_previewBmp = m_iconCacheDir + leaf;
    if (!ExtractShellItemImage(path, reqW, reqH, m_previewBmp)) {
        m_previewBmp.clear();
        return false;
    }
    m_PaintManager.RemoveImage(m_previewBmp.c_str());

    int imgW = reqW, imgH = reqH;
    int frameDesignH = UiTokens::PreviewImageH;
    // Compact frame when request is icon-sized (folders / generic).
    if (reqW <= DpiScale(UiTokens::PreviewIconPx) + 8
        && reqH <= DpiScale(UiTokens::PreviewIconPx) + 8) {
        frameDesignH = UiTokens::PreviewIconCompactH;
    }
    {
        using namespace Gdiplus;
        if (EnsureGdiplus()) {
            Bitmap bmp(m_previewBmp.c_str());
            if (bmp.GetLastStatus() == Ok && bmp.GetWidth() > 0 && bmp.GetHeight() > 0) {
                imgW = bmp.GetWidth();
                imgH = bmp.GetHeight();
            }
        }
    }
    ApplyPreviewImageBk(m_previewBmp, imgW, imgH, frameDesignH);
    return true;
}

bool CMainWnd::LoadPreviewText(const std::wstring& path)
{
    if (!m_pPreviewText) return false;
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li = {};
    ::GetFileSizeEx(h, &li);
    DWORD toRead = (DWORD)(std::min<LONGLONG>(li.QuadPart, kPreviewMaxTextBytes));
    std::string raw(toRead, '\0');
    DWORD got = 0;
    BOOL ok = ::ReadFile(h, raw.data(), toRead, &got, nullptr);
    ::CloseHandle(h);
    if (!ok) return false;
    raw.resize(got);

    std::wstring text;
    if (got >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        text.assign(reinterpret_cast<const wchar_t*>(raw.data() + 2), (got - 2) / 2);
    } else if (got >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        int n = ::MultiByteToWideChar(CP_UTF8, 0, raw.data() + 3, (int)got - 3, nullptr, 0);
        text.resize(n);
        ::MultiByteToWideChar(CP_UTF8, 0, raw.data() + 3, (int)got - 3, &text[0], n);
    } else {
        int n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), (int)got, nullptr, 0);
        if (n > 0) {
            text.resize(n);
            ::MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)got, &text[0], n);
        } else {
            n = ::MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)got, nullptr, 0);
            text.resize(n);
            ::MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)got, &text[0], n);
        }
    }
    // Soft-limit UI string length
    if (text.size() > 8000) {
        text.resize(8000);
        text += L"\n…";
    }
    if (li.QuadPart > kPreviewMaxTextBytes)
        text += L"\n\n文件已截断预览。";
    m_pPreviewText->SetText(text.c_str());
    return true;
}

bool CMainWnd::TransferWithBackgroundCopy(const std::vector<std::wstring>& srcPaths,
    const std::wstring& destDir)
{
    if (srcPaths.empty() || destDir.empty() || IsThisPcPath(destDir))
        return false;
    if (m_copyRunning.load()) {
        UpdateStatus(_T("已有复制任务进行中，请稍候或取消后再试"));
        return false;
    }

    std::vector<ClipboardItem> items;
    items.reserve(srcPaths.size());
    std::wstring dst = NormalizePath(destDir);
    for (const auto& p : srcPaths) {
        std::wstring src = NormalizePath(p);
        if (_wcsicmp(src.c_str(), dst.c_str()) == 0) continue;
        std::wstring prefix = src;
        if (!prefix.empty() && prefix.back() != L'\\') prefix.push_back(L'\\');
        if (dst.size() >= prefix.size()
            && _wcsnicmp(dst.c_str(), prefix.c_str(), (int)prefix.size()) == 0)
            continue;
        DWORD attrs = ::GetFileAttributesW(p.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) continue;
        ClipboardItem it;
        it.path = p;
        it.isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        items.push_back(std::move(it));
    }
    if (items.empty()) {
        UpdateStatus(_T("没有可放下的有效源（或目标不合法）"));
        return false;
    }
    m_lastCopyDest = destDir;
    StartCopyJob(std::move(items), destDir);
    return true;
}

CListContainerElementUI* CMainWnd::CreateDetailsRow(const DirEntry& e)
{
    // ListHBoxElement: children map 1:1 to ListHeader columns (Name/MTime/Type/Size).
    auto* pItem = new CListHBoxElementUI;
    pItem->SetFixedHeight(DpiScale(UiTokens::DetailsRowH));
    pItem->SetUserData(e.fullPath.c_str());
    pItem->SetTag(e.isDir ? 1 : 0);

    const int cellPad = DpiScale(UiTokens::DetailsCellPadL);
    const int iconPx = DpiScale(UiTokens::DetailsIconPx); // SHIL_SMALL ~16
    const int iconPadL = DpiScale(UiTokens::DetailsIconPadL);
    const int iconGap = DpiScale(UiTokens::DetailsIconTextGap);
    const int rowH = DpiScale(UiTokens::DetailsRowH);

    // Name column: Shell small icon + gap + name
    // IMPORTANT: use bkimage — CControlUI ignores foreimage (Button/Option only).
    auto* nameCol = new CHorizontalLayoutUI;
    nameCol->SetMouseEnabled(true);
    auto* iconCtrl = new CControlUI;
    iconCtrl->SetFixedWidth(iconPadL + iconPx + iconGap);
    iconCtrl->SetFixedHeight(rowH);
    iconCtrl->SetMouseEnabled(false);
    std::wstring iconBmp = PeekCachedIconBmp(e.fullPath, e.isDir, iconPx);
    if (iconBmp.empty())
        iconBmp = GetShellFileIconBmp(e.fullPath, e.isDir, iconPx);
    if (!iconBmp.empty()) {
        const int oy = (std::max)(0, (rowH - iconPx) / 2);
        CDuiString imgAttr;
        imgAttr.Format(_T("file='%s' dest='%d,%d,%d,%d'"),
            iconBmp.c_str(), iconPadL, oy, iconPadL + iconPx, oy + iconPx);
        iconCtrl->SetAttribute(_T("bkimage"), imgAttr.GetData());
    }
    nameCol->Add(iconCtrl);
    nameCol->Add(MakeCell(e.name.c_str(), 0, 0));
    pItem->Add(nameCol);

    std::wstring mtimeText = FormatModifiedTime(e.mtime);
    pItem->Add(MakeCell(mtimeText.c_str(), 0, cellPad));
    LPCTSTR typeText = e.isDir
        ? (IsThisPcPath(m_currentPath) ? _T("驱动器") : _T("文件夹"))
        : _T("文件");
    pItem->Add(MakeCell(typeText, 0, cellPad));
    std::wstring sizeText = e.isDir ? L"" : FormatFileSize(e.size);
    if (IsThisPcPath(m_currentPath) && e.capacity > 0)
        sizeText = FormatFileSize(e.size) + L" 可用 / " + FormatFileSize(e.capacity);
    pItem->Add(MakeCell(sizeText.c_str(), 0, cellPad));
    return pItem;
}

void CMainWnd::StopDetailsFill()
{
    m_detailsFilling = false;
    m_detailsFillQueue.clear();
    m_detailsFillNext = 0;
}

void CMainWnd::StartDetailsProgressiveFill(const std::vector<DirEntry>& dirs,
    const std::vector<DirEntry>& files)
{
    StopDetailsFill();
    if (!m_pFileList) return;
    m_pFileList->RemoveAll();
    ApplyColumnWidths();
    UpdateHeaderSortIndicators();

    m_detailsFillQueue.clear();
    m_detailsFillQueue.reserve(dirs.size() + files.size());
    m_detailsFillQueue.insert(m_detailsFillQueue.end(), dirs.begin(), dirs.end());
    m_detailsFillQueue.insert(m_detailsFillQueue.end(), files.begin(), files.end());
    m_detailsFillNext = 0;
    m_detailsFilling = true;

    const int first = (std::min)(kDetailsFirstBatch, (int)m_detailsFillQueue.size());
    for (int i = 0; i < first; ++i)
        m_pFileList->Add(CreateDetailsRow(m_detailsFillQueue[i]));
    m_detailsFillNext = first;

    m_pFileList->SetVisible(!IsTileViewMode());
    m_pFileList->EnableScrollBar(true, false);
    m_pFileList->NeedUpdate();
    if (m_pIconScroll)
        m_pIconScroll->SetVisible(IsTileViewMode());
    if (IsTileViewMode() && m_pIconTiles)
        m_pIconTiles->EnableScrollBar(true, false);
    ApplyFileViewScrollBars();

    if (m_detailsFillNext < (int)m_detailsFillQueue.size() && m_hWnd)
        ::PostMessageW(m_hWnd, kMsgDetailsFill, 0, 0);
    else
        m_detailsFilling = false;
}

void CMainWnd::OnDetailsFillTick()
{
    if (!m_detailsFilling) return;
    const int n = (int)m_detailsFillQueue.size();
    if (n <= 0) { m_detailsFilling = false; return; }

    // Icon progressive path
    if (IsTileViewMode() && m_pIconTiles) {
        int tileW = 100, tileH = 108, iconPx = 48, childPad = 6, maxLabel = 16;
        GetViewMetrics(tileW, tileH, iconPx, childPad, maxLabel);
        const bool listMode = (m_viewMode == ViewMode::List);
        const bool tilesMode = (m_viewMode == ViewMode::Tiles);
        const UINT gen = m_thumbGeneration.load();
        int added = 0;
        while (m_detailsFillNext < n && added < kDetailsFillBatch) {
            auto* tile = IsThisPcPath(m_currentPath)
                ? static_cast<CButtonUI*>(new DriveTileButtonUI) : new CButtonUI;
            BindIconTile(tile, m_detailsFillNext, m_detailsFillQueue[m_detailsFillNext],
                gen, tileW, tileH, iconPx, maxLabel, listMode, tilesMode);
            m_pIconTiles->Add(tile);
            ++m_detailsFillNext;
            ++added;
        }
        m_pIconTiles->EnableScrollBar(true, false);
        m_pIconTiles->NeedUpdate();
        if (m_detailsFillNext < n) {
            PumpUiMessages();
            ::PostMessageW(m_hWnd, kMsgDetailsFill, 1, 0);
        } else {
            m_detailsFilling = false;
            m_detailsFillQueue.clear();
        }
        return;
    }

    if (!m_pFileList) return;
    int added = 0;
    while (m_detailsFillNext < n && added < kDetailsFillBatch) {
        m_pFileList->Add(CreateDetailsRow(m_detailsFillQueue[m_detailsFillNext]));
        ++m_detailsFillNext;
        ++added;
    }
    m_pFileList->NeedUpdate();
    if (m_detailsFillNext < n) {
        PumpUiMessages();
        ::PostMessageW(m_hWnd, kMsgDetailsFill, 0, 0);
    } else {
        m_detailsFilling = false;
        m_detailsFillQueue.clear();
    }
}

void CMainWnd::BindIconTile(CButtonUI* tile, int index, const DirEntry& e, UINT gen,
    int tileW, int tileH, int iconPx, int maxLabel, bool listMode, bool tilesMode)
{
    if (!tile) return;
    CDuiString name;
    name.Format(_T("icon_%d"), index);
    tile->SetName(name);
    tile->SetFixedWidth(tileW);
    tile->SetFixedHeight(tileH);
    tile->SetUserData(e.fullPath.c_str());
    // keep selection bit if same path? reset selection for virt remap
    UINT_PTR tag = e.isDir ? 1 : 0;
    tile->SetTag(tag);
    ApplyIconSelectionVisual(tile);

    if (listMode) {
        tile->SetAttribute(_T("align"), _T("left"));
        tile->SetAttribute(_T("valign"), _T("vcenter"));
        {
            CDuiString tp; tp.Format(_T("%d,%d,%d,%d"), DpiScale(24), DpiScale(0), DpiScale(4), DpiScale(0));
            tile->SetAttribute(_T("textpadding"), tp);
        }
    } else if (tilesMode) {
        tile->SetAttribute(_T("align"), _T("left"));
        tile->SetAttribute(_T("valign"), _T("vcenter"));
        {
            CDuiString tp; tp.Format(_T("%d,%d,%d,%d"), DpiScale(56), DpiScale(4), DpiScale(8), DpiScale(4));
            tile->SetAttribute(_T("textpadding"), tp);
        }
    } else {
        tile->SetAttribute(_T("align"), _T("center"));
        tile->SetAttribute(_T("valign"), _T("bottom"));
        {
            CDuiString tp; tp.Format(_T("%d,%d,%d,%d"), DpiScale(4), DpiScale(4), DpiScale(4), DpiScale(6));
            tile->SetAttribute(_T("textpadding"), tp);
        }
    }
    tile->SetAttribute(_T("endellipsis"), _T("true"));
    if (auto* drive = dynamic_cast<DriveTileButtonUI*>(tile))
        drive->SetDriveSpace(e.size, e.capacity);

    std::wstring label = e.name;
    if (IsThisPcPath(m_currentPath) && e.capacity > 0) {
        label += L"\n" + FormatFileSize(e.size) + L" 可用，共 " + FormatFileSize(e.capacity);
    }
    // Tiles/icon: folder label = name only (never append 文件夹).
    if (tilesMode && !e.isDir) {
        std::wstring typeText = L"文件";
        std::wstring sizeText = FormatFileSize(e.size);
        std::wstring line2 = sizeText.empty() ? typeText : (typeText + L"  " + sizeText);
        if (label.size() > static_cast<size_t>(maxLabel))
            label = label.substr(0, maxLabel - 1) + L"…";
        label = label + L"\n" + line2;
    } else if (!IsThisPcPath(m_currentPath)) {
        if (label.size() > static_cast<size_t>(maxLabel))
            label = label.substr(0, maxLabel - 1) + L"…";
    }
    tile->SetText(label.c_str());

    tile->SetAttribute(_T("foreimage"), _T(""));
    tile->SetAttribute(_T("hotforeimage"), _T(""));
    if (index < kMaxIconThumbs || m_iconVirtMode) {
        std::wstring cached = PeekCachedIconBmp(e.fullPath, e.isDir, iconPx);
        if (!cached.empty()) {
            ApplyTileIconImage(tile, cached, tileW, tileH, iconPx, listMode, tilesMode);
        } else {
            ThumbJob job;
            job.generation = gen;
            job.index = index;
            job.path = e.fullPath;
            job.isDir = e.isDir;
            job.iconPx = iconPx;
            job.tileW = tileW;
            job.tileH = tileH;
            job.listMode = listMode;
            job.tilesMode = tilesMode;
            EnqueueThumbJob(job);
        }
    }
}

void CMainWnd::RebuildIconsViewFull(const std::vector<DirEntry>& all)
{
    if (!m_pIconTiles) return;
    m_iconVirtMode = false;
    ClearIconView();
    ApplyTileLayoutMetrics();

    int tileW = 100, tileH = 108, iconPx = 48, childPad = 6, maxLabel = 16;
    GetViewMetrics(tileW, tileH, iconPx, childPad, maxLabel);
    m_iconPx = iconPx;
    const bool listMode = (m_viewMode == ViewMode::List);
    const bool tilesMode = (m_viewMode == ViewMode::Tiles);
    const UINT gen = m_thumbGeneration.load();

    int added = 0;
    for (const auto& e : all) {
        auto* tile = IsThisPcPath(m_currentPath)
            ? static_cast<CButtonUI*>(new DriveTileButtonUI) : new CButtonUI;
        BindIconTile(tile, added, e, gen, tileW, tileH, iconPx, maxLabel, listMode, tilesMode);
        m_pIconTiles->Add(tile);
        ++added;
        if ((added % kUiBatchSize) == 0)
            PumpUiMessages();
    }
    m_pIconTiles->NeedUpdate();
    if (m_pFileList) m_pFileList->SetVisible(false);
    if (m_pIconScroll) m_pIconScroll->SetVisible(true);
    if (m_pIconTiles) {
        m_pIconTiles->EnableScrollBar(true, false);
        SIZE sp = { 0, 0 };
        m_pIconTiles->SetScrollPos(sp);
    }
    if (m_pIconScroll) m_pIconScroll->NeedUpdate();
}

void CMainWnd::EnsureIconTilePool(int /*poolCount*/, int /*tileW*/, int /*tileH*/)
{
    // retained for header ABI; progressive fill does not use a fixed pool
}

int CMainWnd::ComputeIconVirtPoolSize(int tileW, int tileH) const
{
    RECT rc = { 0, 0, 800, 600 };
    if (m_pIconScroll) rc = m_pIconScroll->GetPos();
    int vw = (std::max)(200, static_cast<int>(rc.right - rc.left));
    int vh = (std::max)(200, static_cast<int>(rc.bottom - rc.top));
    int cols = (std::max)(1, vw / (std::max)(1, tileW + 6));
    int rows = (std::max)(1, vh / (std::max)(1, tileH + 6));
    return cols * (rows + kVirtOverscanRows * 2);
}

void CMainWnd::SyncVisibleIconWindow(bool /*force*/)
{
    // Visible-window remapping is limited by DuiLib TileLayout scroll model.
    // Large folders use progressive creation instead (see RebuildIconsViewVirtual).
}

void CMainWnd::RebuildIconsViewVirtual(const std::vector<DirEntry>& all)
{
    // Strong batching for large folders: first screen immediately, rest async.
    if (!m_pIconTiles) return;
    m_iconVirtMode = false;
    m_flatListing = all;
    CancelThumbJobs();
    ClearIconView();
    ApplyTileLayoutMetrics();

    int tileW = 100, tileH = 108, iconPx = 48, childPad = 6, maxLabel = 16;
    GetViewMetrics(tileW, tileH, iconPx, childPad, maxLabel);
    m_iconPx = iconPx;
    const bool listMode = (m_viewMode == ViewMode::List);
    const bool tilesMode = (m_viewMode == ViewMode::Tiles);
    const UINT gen = m_thumbGeneration.load();

    // Reuse details fill queue machinery for icon progressive create
    StopDetailsFill();
    m_detailsFillQueue = all;
    m_detailsFillNext = 0;
    m_detailsFilling = true;

    const int first = (std::min)(ComputeIconVirtPoolSize(tileW, tileH), (int)all.size());
    for (int i = 0; i < first; ++i) {
        auto* tile = IsThisPcPath(m_currentPath)
            ? static_cast<CButtonUI*>(new DriveTileButtonUI) : new CButtonUI;
        BindIconTile(tile, i, all[i], gen, tileW, tileH, iconPx, maxLabel, listMode, tilesMode);
        m_pIconTiles->Add(tile);
    }
    m_detailsFillNext = first;
    m_pIconTiles->NeedUpdate();
    if (m_pFileList) m_pFileList->SetVisible(false);
    if (m_pIconScroll) m_pIconScroll->SetVisible(true);
    if (m_pIconTiles) {
        m_pIconTiles->EnableScrollBar(true, false);
        SIZE sp = { 0, 0 };
        m_pIconTiles->SetScrollPos(sp);
    }

    if (m_detailsFillNext < (int)m_detailsFillQueue.size() && m_hWnd)
        ::PostMessageW(m_hWnd, kMsgDetailsFill, 1, 0); // wParam=1 => icon mode
    else
        m_detailsFilling = false;
}


// ===== C: Favorites bar persistence + UI =====================================

std::wstring CMainWnd::GetFavoritesFilePath()
{
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir;
    if (n > 0 && n < MAX_PATH)
        dir = appdata;
    else
        dir = L".";
    dir += L"\\FastFile";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\favorites.txt";
}

std::wstring CMainWnd::GetQuickAccessFilePath()
{
    std::wstring path = GetFavoritesFilePath();
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"quick_access.txt" : path.substr(0, slash + 1) + L"quick_access.txt";
}

void CMainWnd::LoadQuickAccess()
{
    m_quickAccess.clear();
    FILE* fp = nullptr;
    const std::wstring file = GetQuickAccessFilePath();
    if (_wfopen_s(&fp, file.c_str(), L"rb") != 0 || !fp) return;
    fseek(fp, 0, SEEK_END);
    const long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 2) { fclose(fp); return; }
    std::wstring content(static_cast<size_t>(size) / sizeof(wchar_t), L'\0');
    fread(&content[0], 1, size, fp);
    fclose(fp);
    if (!content.empty() && content[0] == 0xFEFF) content.erase(content.begin());
    size_t pos = 0;
    while (pos < content.size()) {
        const size_t eol = content.find(L'\n', pos);
        std::wstring line = content.substr(pos, (eol == std::wstring::npos ? content.size() : eol) - pos);
        pos = eol == std::wstring::npos ? content.size() : eol + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        const std::wstring path = NormalizePath(line);
        if (path.empty() || IsQuickAccessPinned(path)) continue;
        DWORD attrs = ::GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        FavoriteItem item{ path, GetLeafName(path) };
        if (item.displayName.empty()) item.displayName = path;
        m_quickAccess.push_back(std::move(item));
    }
}

void CMainWnd::SaveQuickAccess() const
{
    FILE* fp = nullptr;
    const std::wstring file = GetQuickAccessFilePath();
    if (_wfopen_s(&fp, file.c_str(), L"wb") != 0 || !fp) return;
    const wchar_t bom = 0xFEFF;
    fwrite(&bom, sizeof(bom), 1, fp);
    for (const auto& item : m_quickAccess) {
        fwrite(item.path.c_str(), sizeof(wchar_t), item.path.size(), fp);
        const wchar_t nl = L'\n';
        fwrite(&nl, sizeof(nl), 1, fp);
    }
    fclose(fp);
}

bool CMainWnd::IsQuickAccessPinned(const std::wstring& path) const
{
    for (const auto& item : m_quickAccess)
        if (PathEquals(item.path, path)) return true;
    return false;
}

bool CMainWnd::PinQuickAccess(const std::wstring& path)
{
    const std::wstring normalized = NormalizePath(path);
    if (normalized.empty() || IsThisPcPath(normalized) || IsQuickAccessPinned(normalized)) return false;
    DWORD attrs = ::GetFileAttributesW(normalized.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;
    FavoriteItem item{ normalized, GetLeafName(normalized) };
    if (item.displayName.empty()) item.displayName = normalized;
    m_quickAccess.push_back(std::move(item));
    SaveQuickAccess();
    RebuildLeftPinnedFavorites();
    return true;
}

bool CMainWnd::UnpinQuickAccess(const std::wstring& path)
{
    const auto end = std::remove_if(m_quickAccess.begin(), m_quickAccess.end(),
        [&](const FavoriteItem& item) { return PathEquals(item.path, path); });
    if (end == m_quickAccess.end()) return false;
    m_quickAccess.erase(end, m_quickAccess.end());
    SaveQuickAccess();
    RebuildLeftPinnedFavorites();
    return true;
}

void CMainWnd::LoadFavorites()
{
    m_favorites.clear();
    std::wstring file = GetFavoritesFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, file.c_str(), L"rb") != 0 || !fp)
        return;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 2) { fclose(fp); return; }
    std::wstring content;
    content.resize(sz / sizeof(wchar_t));
    fread(&content[0], 1, sz, fp);
    fclose(fp);
    if (!content.empty() && content[0] == 0xFEFF)
        content.erase(content.begin());

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = content.size();
        std::wstring line = content.substr(pos, eol - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        pos = eol + 1;
        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
        std::wstring path = NormalizePath(line);
        if (path.empty()) continue;
        DWORD attrs = ::GetFileAttributesW(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
            continue;
        if (IsFavoritePinned(path)) continue;
        FavoriteItem it;
        it.path = path;
        it.displayName = GetLeafName(path);
        if (it.displayName.empty()) it.displayName = path;
        m_favorites.push_back(std::move(it));
    }
}

void CMainWnd::SaveFavorites() const
{
    std::wstring file = GetFavoritesFilePath();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, file.c_str(), L"wb") != 0 || !fp)
        return;
    wchar_t bom = 0xFEFF;
    fwrite(&bom, sizeof(bom), 1, fp);
    for (const auto& it : m_favorites) {
        fwrite(it.path.c_str(), sizeof(wchar_t), it.path.size(), fp);
        wchar_t nl = L'\n';
        fwrite(&nl, sizeof(nl), 1, fp);
    }
    fclose(fp);
}

bool CMainWnd::IsFavoritePinned(const std::wstring& path) const
{
    std::wstring n = NormalizePath(path);
    for (const auto& it : m_favorites) {
        if (PathEquals(it.path, n)) return true;
    }
    return false;
}

bool CMainWnd::PinFavorite(const std::wstring& path)
{
    std::wstring n = NormalizePath(path);
    if (n.empty() || IsThisPcPath(n)) return false;
    DWORD attrs = ::GetFileAttributesW(n.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return false;
    if (IsFavoritePinned(n)) return false;
    FavoriteItem it;
    it.path = n;
    it.displayName = GetLeafName(n);
    if (it.displayName.empty()) it.displayName = n;
    m_favorites.push_back(std::move(it));
    SaveFavorites();
    RebuildFavoritesBar();
    return true;
}

bool CMainWnd::UnpinFavorite(const std::wstring& path)
{
    std::wstring n = NormalizePath(path);
    auto it = std::remove_if(m_favorites.begin(), m_favorites.end(),
        [&](const FavoriteItem& f) { return PathEquals(f.path, n); });
    if (it == m_favorites.end()) return false;
    m_favorites.erase(it, m_favorites.end());
    SaveFavorites();
    RebuildFavoritesBar();
    return true;
}

bool CMainWnd::IsOverFavoritesBar(POINT ptClient) const
{
    auto contains = [&](CControlUI* c) -> bool {
        if (!c || !c->IsVisible()) return false;
        RECT rc = c->GetPos();
        return ptClient.x >= rc.left && ptClient.x < rc.right
            && ptClient.y >= rc.top && ptClient.y < rc.bottom;
    };
    if (contains(m_pFavoritesBar)) return true;
    if (contains(m_pFavoritesStrip)) return true;
    return false;
}

void CMainWnd::RebuildFavoritesBar()
{
    if (!m_pFavoritesStrip) return;
    m_pFavoritesStrip->RemoveAll();
    const bool empty = m_favorites.empty();
    m_pFavoritesStrip->SetVisible(!empty);
    int stripW = 0;

    // Match fav_bar_label: font 0 (FontBody), vertically centered icon+text (no clip).
    const int iconPx = DpiScale(UiTokens::FavIconPx);
    const int btnH = DpiScale(UiTokens::FavChipH);

    for (size_t i = 0; i < m_favorites.size(); ++i) {
        const auto& fav = m_favorites[i];
        auto* btn = new CButtonUI;
        CDuiString name;
        name.Format(_T("fav_pin_%d"), (int)i);
        btn->SetName(name);
        btn->SetText(fav.displayName.c_str());
        btn->SetUserData(fav.path.c_str());
        btn->SetFixedHeight(btnH);
        btn->SetAttribute(_T("align"), _T("left"));
        btn->SetAttribute(_T("valign"), _T("vcenter"));
        btn->SetAttribute(_T("font"), _T("0"));
        btn->SetAttribute(_T("bkcolor"), _T("#00FFFFFF"));
        btn->SetAttribute(_T("hotbkcolor"), _T("#FFE8E8E8"));
        btn->SetAttribute(_T("pushedbkcolor"), _T("#FFDADADA"));
        btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        btn->SetAttribute(_T("bordercolor"), _T("#00FFFFFF"));
        btn->SetAttribute(_T("bordersize"), _T("0"));
        btn->SetAttribute(_T("endellipsis"), _T("true"));
        {
            CDuiString tp;
            // left room for 16px icon + gap; no vertical pad (valign centers)
            tp.Format(_T("%d,0,%d,0"), DpiScale(UiTokens::FavIconPx + 8), DpiScale(6));
            btn->SetAttribute(_T("textpadding"), tp);
        }
        // CJK-friendly width (~13px/glyph @12pt) + icon gutter
        int w = DpiScale(static_cast<int>(fav.displayName.size()) * 13 + 36);
        if (w < DpiScale(84)) w = DpiScale(84);
        if (w > DpiScale(240)) w = DpiScale(240);
        btn->SetFixedWidth(w);
        btn->SetToolTip(fav.path.c_str());

        std::wstring bmp = GetShellIconBmp(fav.path, true, iconPx);
        if (bmp.empty()) bmp = GetStockIconBmp(SIID_FOLDER, iconPx);
        if (!bmp.empty()) {
            const int y = (btnH - iconPx) / 2; // vertical center with label
            ApplyControlForeIcon(btn, bmp, iconPx, DpiScale(4), y, false);
        }
        m_pFavoritesStrip->Add(btn);
        stripW += w;
    }

    if (!empty)
        m_pFavoritesStrip->SetFixedWidth((std::max)(DpiScale(1), stripW));

    if (CControlUI* hint = m_PaintManager.FindControl(_T("fav_bar_hint"))) {
        hint->SetVisible(empty);
    }
    m_pFavoritesStrip->NeedUpdate();
    if (m_pFavoritesBar) m_pFavoritesBar->NeedUpdate();
    UpdateFavoritesHighlight();
}

void CMainWnd::RebuildLeftPinnedFavorites()
{
    if (!m_pLeftFavPins) return;
    m_pLeftFavPins->RemoveAll();
    m_pLeftFavPins->SetVisible(!m_quickAccess.empty());
    const int iconPx = DpiScale(UiTokens::NavIconPx);
    const int rowH = DpiScale(UiTokens::NavRowH);
    for (size_t i = 0; i < m_quickAccess.size(); ++i) {
        const auto& entry = m_quickAccess[i];
        auto* btn = new CButtonUI;
        CDuiString name;
        name.Format(_T("fav_dyn_%d"), (int)i);
        btn->SetName(name);
        btn->SetText(entry.displayName.c_str());
        btn->SetUserData(entry.path.c_str());
        btn->SetFixedHeight(rowH);
        btn->SetAttribute(_T("align"), _T("left"));
        btn->SetAttribute(_T("valign"), _T("vcenter"));
        btn->SetAttribute(_T("font"), _T("0"));
        btn->SetAttribute(_T("endellipsis"), _T("true"));
        btn->SetAttribute(_T("bkcolor"), UiTokens::ColorSurface);
        btn->SetAttribute(_T("hotbkcolor"), UiTokens::ColorNavHover);
        btn->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorNavSelected);
        btn->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        CDuiString textPad;
        textPad.Format(_T("%d,0,%d,0"), DpiScale(UiTokens::NavIconPad + UiTokens::NavIconPx + UiTokens::NavIconTextGap + 4), DpiScale(UiTokens::NavTextPadR));
        btn->SetAttribute(_T("textpadding"), textPad.GetData());
        std::wstring icon = GetShellIconBmp(entry.path, true, iconPx);
        if (icon.empty()) icon = GetStockIconBmp(SIID_FOLDER, iconPx);
        if (!icon.empty())
            ApplyControlForeIcon(btn, icon, iconPx, DpiScale(UiTokens::NavIconPad + 4), (rowH - iconPx) / 2, false);
        m_pLeftFavPins->Add(btn);
    }
    const int minimum = UiTokens::LeftQuickMinH + static_cast<int>(m_quickAccess.size()) * UiTokens::NavRowH;
    if (m_leftQuickDesignH < minimum)
        ApplyLeftNavSplitterHeight(minimum);
    m_pLeftFavPins->NeedUpdate();
    if (m_pLeftQuick) m_pLeftQuick->NeedUpdate();
}

void CMainWnd::OnPinnedFavoriteClick(CControlUI* btn)
{
    if (!btn) return;
    CDuiString ud = btn->GetUserData();
    if (ud.IsEmpty()) return;
    AddTab(ud.GetData(), true);
}

void CMainWnd::ShowFavoriteContextMenu(CControlUI* btn, POINT ptScreen)
{
    if (!btn) return;
    CDuiString ud = btn->GetUserData();
    if (ud.IsEmpty()) return;
    std::wstring path = ud.GetData();
    const bool quickAccess = btn->GetName().Find(_T("fav_dyn_")) == 0;

    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu) return;
    ::AppendMenuW(hMenu, MF_STRING, kCmdFavOpen, L"\u6253\u5f00");
    ::AppendMenuW(hMenu, MF_STRING, kCmdFavUnpin,
        quickAccess ? L"\u4ece\u5feb\u901f\u8bbf\u95ee\u53d6\u6d88\u56fa\u5b9a" : L"\u4ece\u6536\u85cf\u680f\u53d6\u6d88\u56fa\u5b9a");
    UINT cmd = ::TrackPopupMenuEx(hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        ptScreen.x, ptScreen.y, m_hWnd, nullptr);
    ::DestroyMenu(hMenu);
    if (cmd == kCmdFavOpen) {
        AddTab(path, true);
    } else if (cmd == kCmdFavUnpin) {
        if ((quickAccess ? UnpinQuickAccess(path) : UnpinFavorite(path)))
            UpdateStatus(quickAccess ? _T("已从快速访问取消固定") : _T("已从收藏栏取消固定"));
    }
}
