// FastFile - OLE drag and drop: drop target, drag source, transfers
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

namespace {

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
        return TransferWithBackgroundCopy(srcPaths, destDir, /*move*/ true);
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
            // An in-app drop starts its own background job; don't stomp that readout.
            if (!m_copyRunning.load())
                UpdateStatus(_T("已通过拖拽移动"));
        } else if (effect & DROPEFFECT_COPY) {
            if (!m_copyRunning.load())
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

bool CMainWnd::TransferWithBackgroundCopy(const std::vector<std::wstring>& srcPaths,
    const std::wstring& destDir, bool move)
{
    if (srcPaths.empty() || destDir.empty() || IsThisPcPath(destDir))
        return false;
    if (m_copyRunning.load()) {
        UpdateStatus(_T("已有复制/移动任务进行中，请稍候或取消后再试"));
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
    StartCopyJob(std::move(items), destDir, move);
    return true;
}
