// FastFile - path and formatting helpers, UI message pumping
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"
#include "FastFileCore.h"

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
    std::wstring source = (expLen > 0 && expLen < _countof(expanded)) ? expanded : trimmed;
    // Folder open commands ending in a quoted drive root can arrive as C:".
    // After trimming the quote, C: must mean the root here, never the drive's CWD.
    if (source.size() == 2 && source[1] == L':' &&
        ((source[0] >= L'A' && source[0] <= L'Z') || (source[0] >= L'a' && source[0] <= L'z')))
        source += L'\\';
    const wchar_t* src = source.c_str();

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
    return FastFileCore::ParentPath(path);
}

std::wstring CMainWnd::FormatFileSize(ULONGLONG bytes)
{
    return FastFileCore::FormatFileSize(static_cast<std::uint64_t>(bytes));
}

std::wstring CMainWnd::GetLeafName(const std::wstring& path)
{
    return FastFileCore::GetLeafName(path);
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

void CMainWnd::UpdateStatus(LPCTSTR text)
{
    if (m_pStatus && text)
        m_pStatus->SetText(text);
}

int CMainWnd::MeasureTextWidth(const std::wstring& text)
{
    if (text.empty())
        return 0;
    HWND hdcWnd = m_hWnd ? m_hWnd : ::GetDesktopWindow();
    HDC dc = ::GetDC(hdcWnd);
    if (!dc)
        return 0;
    // Measure with the font DuiLib actually renders this text with, so chips and tabs size
    // themselves to the real glyph widths rather than a per-character guess.
    HFONT font = m_PaintManager.GetFont(0);
    HGDIOBJ old = font ? ::SelectObject(dc, font) : nullptr;
    SIZE sz = {};
    const int width = ::GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz)
        ? static_cast<int>(sz.cx) : 0;
    if (old) ::SelectObject(dc, old);
    ::ReleaseDC(hdcWnd, dc);
    return width;
}
