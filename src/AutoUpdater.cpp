#include "AutoUpdater.h"
#include "FastFileAbout.h"
#include <winhttp.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <cctype>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace FastFileUpdate {

SemanticVersion SemanticVersion::Parse(const std::wstring& str) {
    SemanticVersion ver;
    ver.raw = str;
    size_t i = 0;
    while (i < str.size() && iswspace(str[i])) i++;
    if (i < str.size() && (str[i] == L'v' || str[i] == L'V')) i++;

    auto readNum = [&]() -> int {
        while (i < str.size() && !iswdigit(str[i])) {
            if (str[i] == L'.' || iswspace(str[i])) { i++; continue; }
            return 0;
        }
        int val = 0;
        bool hasDigits = false;
        while (i < str.size() && iswdigit(str[i])) {
            val = val * 10 + (str[i] - L'0');
            hasDigits = true;
            i++;
        }
        return hasDigits ? val : 0;
    };

    ver.major = readNum();
    if (i < str.size() && str[i] == L'.') { i++; ver.minor = readNum(); }
    if (i < str.size() && str[i] == L'.') { i++; ver.patch = readNum(); }
    return ver;
}

std::wstring SemanticVersion::ToString() const {
    return std::to_wstring(major) + L"." + std::to_wstring(minor) + L"." + std::to_wstring(patch);
}

const ReleaseAsset* ReleaseInfo::FindInstallerAsset() const {
    for (const auto& asset : assets) {
        if (asset.name.rfind(L".exe") == asset.name.size() - 4 &&
            asset.name.find(L"FastFile-Setup") != std::wstring::npos) {
            return &asset;
        }
    }
    return nullptr;
}

namespace {

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &result[0], len);
    return result;
}

struct SimpleJsonParser {
    const std::string& src;
    size_t pos = 0;

    SimpleJsonParser(const std::string& s) : src(s) {}

    void SkipWhitespace() {
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r' || src[pos] == '\n')) {
            pos++;
        }
    }

    bool Peek(char expected) {
        SkipWhitespace();
        return pos < src.size() && src[pos] == expected;
    }

    bool Match(char expected) {
        SkipWhitespace();
        if (pos < src.size() && src[pos] == expected) {
            pos++;
            return true;
        }
        return false;
    }

    bool ParseString(std::string& outStr) {
        if (!Match('"')) return false;
        outStr.clear();
        while (pos < src.size()) {
            char c = src[pos++];
            if (c == '"') return true;
            if (c == '\\' && pos < src.size()) {
                char esc = src[pos++];
                switch (esc) {
                    case '"': outStr.push_back('"'); break;
                    case '\\': outStr.push_back('\\'); break;
                    case '/': outStr.push_back('/'); break;
                    case 'b': outStr.push_back('\b'); break;
                    case 'f': outStr.push_back('\f'); break;
                    case 'n': outStr.push_back('\n'); break;
                    case 'r': outStr.push_back('\r'); break;
                    case 't': outStr.push_back('\t'); break;
                    case 'u': {
                        if (pos + 4 <= src.size()) {
                            // 简易处理 Unicode 转义序列
                            std::string hex = src.substr(pos, 4);
                            pos += 4;
                            unsigned int codepoint = 0;
                            std::stringstream ss;
                            ss << std::hex << hex;
                            ss >> codepoint;
                            if (codepoint < 0x80) {
                                outStr.push_back(static_cast<char>(codepoint));
                            } else if (codepoint < 0x800) {
                                outStr.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                                outStr.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                            } else {
                                outStr.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                                outStr.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                                outStr.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                            }
                        }
                        break;
                    }
                    default: outStr.push_back(esc); break;
                }
            } else {
                outStr.push_back(c);
            }
        }
        return false;
    }

    bool ParseUint64(uint64_t& outVal) {
        SkipWhitespace();
        outVal = 0;
        bool hasDigits = false;
        while (pos < src.size() && isdigit(static_cast<unsigned char>(src[pos]))) {
            outVal = outVal * 10 + (src[pos++] - '0');
            hasDigits = true;
        }
        return hasDigits;
    }

    void SkipValue() {
        SkipWhitespace();
        if (pos >= src.size()) return;
        char c = src[pos];
        if (c == '"') {
            std::string dummy;
            ParseString(dummy);
        } else if (c == '{') {
            pos++;
            int depth = 1;
            while (pos < src.size() && depth > 0) {
                if (src[pos] == '"') {
                    std::string dummy;
                    ParseString(dummy);
                } else {
                    if (src[pos] == '{') depth++;
                    else if (src[pos] == '}') depth--;
                    pos++;
                }
            }
        } else if (c == '[') {
            pos++;
            int depth = 1;
            while (pos < src.size() && depth > 0) {
                if (src[pos] == '"') {
                    std::string dummy;
                    ParseString(dummy);
                } else {
                    if (src[pos] == '[') depth++;
                    else if (src[pos] == ']') depth--;
                    pos++;
                }
            }
        } else {
            while (pos < src.size() && src[pos] != ',' && src[pos] != '}' && src[pos] != ']' && !isspace(static_cast<unsigned char>(src[pos]))) {
                pos++;
            }
        }
    }
};

} // namespace

bool ParseGitHubReleaseJson(const std::string& jsonUtf8, ReleaseInfo& outInfo) {
    outInfo = ReleaseInfo{};
    SimpleJsonParser p(jsonUtf8);
    if (!p.Match('{')) return false;

    while (!p.Peek('}') && p.pos < jsonUtf8.size()) {
        std::string key;
        if (!p.ParseString(key)) break;
        if (!p.Match(':')) break;

        if (key == "tag_name") {
            std::string val;
            p.ParseString(val);
            outInfo.tagName = Utf8ToWide(val);
            outInfo.version = SemanticVersion::Parse(outInfo.tagName);
        } else if (key == "name") {
            std::string val;
            p.ParseString(val);
            outInfo.title = Utf8ToWide(val);
        } else if (key == "body") {
            std::string val;
            p.ParseString(val);
            outInfo.notes = Utf8ToWide(val);
        } else if (key == "assets") {
            if (p.Match('[')) {
                while (!p.Peek(']') && p.pos < jsonUtf8.size()) {
                    if (p.Match('{')) {
                        ReleaseAsset asset;
                        while (!p.Peek('}') && p.pos < jsonUtf8.size()) {
                            std::string akey;
                            if (!p.ParseString(akey)) break;
                            if (!p.Match(':')) break;
                            if (akey == "name") {
                                std::string val;
                                p.ParseString(val);
                                asset.name = Utf8ToWide(val);
                            } else if (akey == "browser_download_url") {
                                std::string val;
                                p.ParseString(val);
                                asset.downloadUrl = Utf8ToWide(val);
                            } else if (akey == "size") {
                                p.ParseUint64(asset.size);
                            } else {
                                p.SkipValue();
                            }
                            p.Match(',');
                        }
                        p.Match('}');
                        if (!asset.name.empty() && !asset.downloadUrl.empty()) {
                            outInfo.assets.push_back(asset);
                        }
                    }
                    p.Match(',');
                }
                p.Match(']');
            } else {
                p.SkipValue();
            }
        } else {
            p.SkipValue();
        }
        p.Match(',');
    }
    p.Match('}');
    return !outInfo.tagName.empty() && outInfo.version.IsValid();
}

std::wstring BuildInstallerArguments(const std::wstring& installDir, bool restart) {
    std::wstring args = L"--quiet";
    if (restart) {
        args += L" --restart";
    }
    if (!installDir.empty()) {
        args += L" --dir \"" + installDir + L"\"";
    }
    return args;
}

std::wstring GetUpdateDownloadPath(const std::wstring& version) {
    wchar_t temp[MAX_PATH]{};
    DWORD len = GetTempPathW(_countof(temp), temp);
    if (!len || len >= _countof(temp)) return {};
    std::wstring dir = std::wstring(temp) + L"FastFile\\Updates";
    CreateDirectoryW((std::wstring(temp) + L"FastFile").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\FastFile-Setup-" + version + L".exe";
}

UpdateCheckResult CheckForUpdate(const std::wstring& currentVersion, const std::wstring& repo) {
    UpdateCheckResult result;
    const auto curVer = SemanticVersion::Parse(currentVersion);

    HINTERNET hSession = WinHttpOpen(
        L"FastFile-AutoUpdater/" FASTFILE_VERSION_W,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) {
        result.status = UpdateCheckStatus::NetworkError;
        result.message = L"网络组件初始化失败。";
        return result;
    }

    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 20000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        result.status = UpdateCheckStatus::NetworkError;
        result.message = L"连接更新服务器失败。";
        return result;
    }

    std::wstring path = L"/repos/" + repo + L"/releases/latest";
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        result.status = UpdateCheckStatus::NetworkError;
        result.message = L"创建更新请求失败。";
        return result;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    const wchar_t headers[] = L"Accept: application/vnd.github.v3+json\r\nUser-Agent: FastFile-AutoUpdater\r\n";
    BOOL sent = WinHttpSendRequest(hRequest, headers, static_cast<DWORD>(-1), nullptr, 0, 0, 0);
    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        result.status = UpdateCheckStatus::NetworkError;
        result.message = L"获取更新响应超时或网络异常。";
        return result;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        result.status = UpdateCheckStatus::NetworkError;
        result.message = L"更新服务器返回状态码 " + std::to_wstring(statusCode);
        return result;
    }

    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
            responseBody.append(buffer.data(), bytesRead);
        } else {
            break;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!ParseGitHubReleaseJson(responseBody, result.release)) {
        result.status = UpdateCheckStatus::ParseError;
        result.message = L"解析更新版本信息失败。";
        return result;
    }

    if (result.release.version > curVer) {
        result.status = UpdateCheckStatus::Success;
        result.hasUpdate = true;
        result.message = L"发现新版本：" + result.release.tagName;
    } else {
        result.status = UpdateCheckStatus::UpToDate;
        result.hasUpdate = false;
        result.message = L"当前已是最新版本（" + currentVersion + L"）。";
    }

    return result;
}

bool DownloadUpdateFile(const std::wstring& url,
                        const std::wstring& destinationPath,
                        std::function<void(uint64_t downloadedBytes, uint64_t totalBytes)> onProgress,
                        std::function<bool()> shouldCancel,
                        std::wstring* errorMessage) {
    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t host[256]{};
    wchar_t path[2048]{};
    urlComp.lpszHostName = host;
    urlComp.dwHostNameLength = _countof(host);
    urlComp.lpszUrlPath = path;
    urlComp.dwUrlPathLength = _countof(path);

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &urlComp)) {
        if (errorMessage) *errorMessage = L"无效的下载 URL。";
        return false;
    }

    HINTERNET hSession = WinHttpOpen(
        L"FastFile-AutoUpdater/" FASTFILE_VERSION_W,
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) {
        if (errorMessage) *errorMessage = L"网络组件初始化失败。";
        return false;
    }

    WinHttpSetTimeouts(hSession, 15000, 15000, 30000, 60000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hConnect = WinHttpConnect(hSession, host, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        if (errorMessage) *errorMessage = L"无法连接到下载服务器。";
        return false;
    }

    DWORD reqFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        reqFlags
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        if (errorMessage) *errorMessage = L"无法创建下载请求。";
        return false;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        if (errorMessage) *errorMessage = L"连接下载地址失败或无响应。";
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        if (errorMessage) *errorMessage = L"下载服务器返回 HTTP 状态码 " + std::to_wstring(statusCode);
        return false;
    }

    uint64_t totalBytes = 0;
    wchar_t lengthBuf[64]{};
    DWORD lengthSize = sizeof(lengthBuf);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            lengthBuf, &lengthSize, WINHTTP_NO_HEADER_INDEX)) {
        totalBytes = _wcstoui64(lengthBuf, nullptr, 10);
    }

    std::wstring partPath = destinationPath + L".part";
    HANDLE hFile = CreateFileW(partPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        if (errorMessage) *errorMessage = L"无法创建临时安装包文件。";
        return false;
    }

    uint64_t downloadedBytes = 0;
    std::vector<BYTE> buffer(64 * 1024);
    bool success = true;

    while (true) {
        if (shouldCancel && shouldCancel()) {
            success = false;
            if (errorMessage) *errorMessage = L"用户已取消下载。";
            break;
        }

        DWORD bytesAvailable = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable) || bytesAvailable == 0) {
            break;
        }

        DWORD bytesToRead = (std::min)(bytesAvailable, static_cast<DWORD>(buffer.size()));
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), bytesToRead, &bytesRead) || bytesRead == 0) {
            break;
        }

        DWORD bytesWritten = 0;
        if (!WriteFile(hFile, buffer.data(), bytesRead, &bytesWritten, nullptr) || bytesWritten != bytesRead) {
            success = false;
            if (errorMessage) *errorMessage = L"写入磁盘文件失败，可能磁盘空间不足。";
            break;
        }

        downloadedBytes += bytesRead;
        if (onProgress) {
            onProgress(downloadedBytes, totalBytes);
        }
    }

    FlushFileBuffers(hFile);
    CloseHandle(hFile);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!success) {
        DeleteFileW(partPath.c_str());
        return false;
    }

    // 重命名原子覆盖到最终安装包文件
    if (!MoveFileExW(partPath.c_str(), destinationPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(partPath.c_str());
        if (errorMessage) *errorMessage = L"完成安装包转存失败。";
        return false;
    }

    return true;
}

bool LaunchInstallerAndExit(const std::wstring& installerPath, HWND owner) {
    if (installerPath.empty() || GetFileAttributesW(installerPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    std::wstring exePath = FastFileAbout::ExecutablePath();
    std::wstring installDir = FastFileAbout::Directory(exePath);
    std::wstring args = BuildInstallerArguments(installDir, true);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = owner;
    sei.lpVerb = L"open";
    sei.lpFile = installerPath.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        return false;
    }

    if (sei.hProcess) {
        CloseHandle(sei.hProcess);
    }

    // 退出当前界面进程，让安装程序静默覆盖并无缝重启
    PostQuitMessage(0);
    return true;
}

} // namespace FastFileUpdate
