#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include "FastFileBuildInfo.h"

namespace FastFileUpdate {

struct SemanticVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::wstring raw;

    static SemanticVersion Parse(const std::wstring& str);
    bool IsValid() const {
        return major >= 0 && minor >= 0 && patch >= 0 && (major > 0 || minor > 0 || patch > 0);
    }
    std::wstring ToString() const;

    bool operator>(const SemanticVersion& other) const {
        if (major != other.major) return major > other.major;
        if (minor != other.minor) return minor > other.minor;
        return patch > other.patch;
    }
    bool operator<(const SemanticVersion& other) const {
        if (major != other.major) return major < other.major;
        if (minor != other.minor) return minor < other.minor;
        return patch < other.patch;
    }
    bool operator==(const SemanticVersion& other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }
    bool operator>=(const SemanticVersion& other) const { return !(*this < other); }
    bool operator<=(const SemanticVersion& other) const { return !(*this > other); }
};

struct ReleaseAsset {
    std::wstring name;
    std::wstring downloadUrl;
    uint64_t size = 0;
};

struct ReleaseInfo {
    SemanticVersion version;
    std::wstring tagName;
    std::wstring title;
    std::wstring notes;
    std::vector<ReleaseAsset> assets;

    const ReleaseAsset* FindInstallerAsset() const;
};

enum class UpdateCheckStatus {
    Success,
    UpToDate,
    NetworkError,
    ParseError,
    Cancelled
};

struct UpdateCheckResult {
    UpdateCheckStatus status = UpdateCheckStatus::NetworkError;
    std::wstring message;
    ReleaseInfo release;
    bool hasUpdate = false;
};

// 从 GitHub Releases API 返回的 JSON 字符串中解析 ReleaseInfo
bool ParseGitHubReleaseJson(const std::string& jsonUtf8, ReleaseInfo& outInfo);

// 构造安装程序静默更新启动参数
std::wstring BuildInstallerArguments(const std::wstring& installDir, bool restart = true);

// 获取更新安装程序下载保存的目标路径
std::wstring GetUpdateDownloadPath(const std::wstring& version);

// 向 GitHub API 查询最新发布版本
UpdateCheckResult CheckForUpdate(const std::wstring& currentVersion = FASTFILE_VERSION_W,
                                 const std::wstring& repo = L"jinlong85/FastFile");

// 下载更新文件，提供进度回调与取消回调
bool DownloadUpdateFile(const std::wstring& url,
                        const std::wstring& destinationPath,
                        std::function<void(uint64_t downloadedBytes, uint64_t totalBytes)> onProgress = nullptr,
                        std::function<bool()> shouldCancel = nullptr,
                        std::wstring* errorMessage = nullptr);

// 启动安装程序并执行静默更新与重启
bool LaunchInstallerAndExit(const std::wstring& installerPath, HWND owner = nullptr);

} // namespace FastFileUpdate
