#pragma once
// FastFile - helpers for the Windows Shell context menus FastFile shows.
//
// Shell menus do not mark separators consistently: IContextMenu implementations insert
// them with id 0, -1 or private ids such as 0x7FFD / 0x7FFE, and an empty text item draws
// as a separator line too. Separator detection must therefore look at MFT_SEPARATOR (and
// at empty text items), never at the command id - checking "id == 0" left two stacked lines
// after 授予访问权限 was pruned from the folder background menu.

#include <Windows.h>
#include <shobjidl.h>

namespace ShellMenuUtil {

// True when the item at pos renders as a separator line.
inline bool IsSeparatorAt(HMENU menu, int pos)
{
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE | MIIM_SUBMENU | MIIM_STRING | MIIM_BITMAP;
    if (!::GetMenuItemInfoW(menu, static_cast<UINT>(pos), TRUE, &info)) return false;
    if (info.fType & MFT_SEPARATOR) return true;
    if (info.hSubMenu || (info.fType & (MFT_OWNERDRAW | MFT_BITMAP)) || info.hbmpItem) return false;
    return info.cch == 0;
}

// Removes leading, trailing and consecutive separators (recursively into populated
// submenus). Returns the number of removed items.
inline int NormalizeSeparators(HMENU menu, bool recursive = true)
{
    if (!menu) return 0;
    int removed = 0;
    bool previousSeparator = true; // a leading separator is dropped
    for (int pos = 0; pos < ::GetMenuItemCount(menu);) {
        if (IsSeparatorAt(menu, pos)) {
            if (previousSeparator) {
                ::DeleteMenu(menu, static_cast<UINT>(pos), MF_BYPOSITION);
                ++removed;
                continue;
            }
            previousSeparator = true;
        } else {
            previousSeparator = false;
            if (recursive) {
                HMENU sub = ::GetSubMenu(menu, pos);
                if (sub && ::GetMenuItemCount(sub) > 0) removed += NormalizeSeparators(sub, true);
            }
        }
        ++pos;
    }
    for (int last = ::GetMenuItemCount(menu) - 1; last >= 0 && IsSeparatorAt(menu, last); --last) {
        ::DeleteMenu(menu, static_cast<UINT>(last), MF_BYPOSITION);
        ++removed;
    }
    return removed;
}

// Position of the first item whose canonical Shell verb equals verb (case-insensitive),
// or -1. Only items in [first, last) belong to the IContextMenu.
template <class ContextMenu>
inline int FindVerb(ContextMenu* contextMenu, HMENU menu, UINT first, UINT last, const wchar_t* verb)
{
    if (!contextMenu || !menu || !verb) return -1;
    for (int pos = 0; pos < ::GetMenuItemCount(menu); ++pos) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_ID | MIIM_FTYPE;
        if (!::GetMenuItemInfoW(menu, static_cast<UINT>(pos), TRUE, &info) || (info.fType & MFT_SEPARATOR)) continue;
        if (info.wID < first || info.wID >= last) continue;
        wchar_t name[128]{};
        if (SUCCEEDED(contextMenu->GetCommandString(info.wID - first, GCS_VERBW, nullptr,
                reinterpret_cast<LPSTR>(name), _countof(name))) && ::_wcsicmp(name, verb) == 0)
            return pos;
    }
    return -1;
}

} // namespace ShellMenuUtil
