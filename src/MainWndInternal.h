#pragma once
// FastFile internal header - shared by the split CMainWnd translation units.
// Holds the former MainWnd.cpp include prologue plus the two file-local helpers that
// are used from more than one translation unit (kept inline).

#include "MainWnd.h"
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
