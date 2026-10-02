#pragma once
// FastFile internal header - shared by the split CMainWnd translation units.
// Holds the former MainWnd.cpp include prologue plus the two file-local helpers that
// are used from more than one translation unit (kept inline).

#include "MainWnd.h"
#include "ShellBrowserHost.h"
#include "FluentScrollBarUI.h"

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

// Preview paths keep the parent folder and filename when the full path is too
// wide. The underlying label text stays intact for selection, tests and tooltips.
class CPreviewPathLabelUI final : public CLabelUI {
public:
    static std::wstring FitTail(HDC dc, HFONT font, const std::wstring& path, int width) {
        const int saved=SaveDC(dc);
        if(font) SelectObject(dc,font);
        auto fits=[&](const std::wstring& value) {
            SIZE size{};GetTextExtentPoint32W(dc,value.c_str(),int(value.size()),&size);return size.cx<=width;
        };
        std::wstring result=path;
        if(!fits(result)) {
            const auto leaf=path.find_last_of(L"\\/");
            const auto parent=leaf==std::wstring::npos || leaf==0 ? std::wstring::npos : path.find_last_of(L"\\/",leaf-1);
            result=L"…"+(parent==std::wstring::npos ? path : path.substr(parent));
            while(result.size()>1 && !fits(result)) {
                result.erase(1,1);
                // Do not split a UTF-16 surrogate pair when shortening the prefix.
                if(result.size()>1 && result[1]>=0xDC00 && result[1]<=0xDFFF) result.erase(1,1);
            }
        }
        RestoreDC(dc,saved);return result;
    }
    void PaintText(HDC dc) override {
        if(!m_pManager || !dc) return;
        const CDuiString full=m_sText;
        const UINT style=m_uTextStyle;
        const int width=GetWidth()-m_rcTextPadding.left-m_rcTextPadding.right;
        m_sText=FitTail(dc,m_pManager->GetFont(m_iFont),full.GetData(),width).c_str();
        m_uTextStyle=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX;
        CLabelUI::PaintText(dc);
        m_sText=full;m_uTextStyle=style;
    }
};

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWCP_ROUNDSMALL
#define DWMWCP_ROUNDSMALL 3
#endif


inline CControlUI* FindListItemRoot(CControlUI* pSender)
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

inline void ScaleNamedFixed(CPaintManagerUI& pm, LPCTSTR name, int designW, int designH, UINT dpi)
{
    CControlUI* c = pm.FindControl(name);
    if (!c) return;
    if (designW > 0) c->SetFixedWidth(::MulDiv(designW, (int)dpi, 96));
    if (designH > 0) c->SetFixedHeight(::MulDiv(designH, (int)dpi, 96));
}
