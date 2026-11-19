#include "AutoUpdater.h"
#include <iostream>
#include <cassert>
#include <string>

using namespace FastFileUpdate;

static void TestSemanticVersion() {
    auto v1 = SemanticVersion::Parse(L"1.0.22");
    assert(v1.IsValid());
    assert(v1.major == 1 && v1.minor == 0 && v1.patch == 22);

    auto v2 = SemanticVersion::Parse(L"v1.0.23");
    assert(v2.IsValid());
    assert(v2.major == 1 && v2.minor == 0 && v2.patch == 23);

    assert(v2 > v1);
    assert(v1 < v2);
    assert(!(v1 == v2));

    auto v3 = SemanticVersion::Parse(L"  V2.1.0-alpha ");
    assert(v3.IsValid());
    assert(v3.major == 2 && v3.minor == 1 && v3.patch == 0);
    assert(v3 > v2);

    auto vEqual = SemanticVersion::Parse(L"1.0.22");
    assert(v1 == vEqual);
    assert(v1 >= vEqual);
    assert(v1 <= vEqual);

    auto vInvalid = SemanticVersion::Parse(L"");
    assert(!vInvalid.IsValid());

    std::cout << "TestSemanticVersion passed." << std::endl;
}

static void TestReleaseJsonParser() {
    std::string sampleJson = R"({
        "url": "https://api.github.com/repos/jinlong85/FastFile/releases/1000",
        "tag_name": "v1.0.23",
        "name": "FastFile 1.0.23（预发布）",
        "body": "### 新增功能\r\n- 自动更新功能\r\n- 体验优化",
        "assets": [
            {
                "name": "FastFile-Setup-1.0.23.exe",
                "size": 2365952,
                "browser_download_url": "https://github.com/jinlong85/FastFile/releases/download/v1.0.23/FastFile-Setup-1.0.23.exe"
            },
            {
                "name": "SHA256SUMS.txt",
                "size": 256,
                "browser_download_url": "https://github.com/jinlong85/FastFile/releases/download/v1.0.23/SHA256SUMS.txt"
            }
        ]
    })";

    ReleaseInfo info;
    bool ok = ParseGitHubReleaseJson(sampleJson, info);
    assert(ok);
    assert(info.tagName == L"v1.0.23");
    assert(info.title == L"FastFile 1.0.23（预发布）");
    assert(info.version.major == 1 && info.version.minor == 0 && info.version.patch == 23);
    assert(info.assets.size() == 2);
    assert(info.assets[0].name == L"FastFile-Setup-1.0.23.exe");
    assert(info.assets[0].size == 2365952);

    const auto* installer = info.FindInstallerAsset();
    assert(installer != nullptr);
    assert(installer->name == L"FastFile-Setup-1.0.23.exe");
    assert(installer->downloadUrl.find(L"FastFile-Setup-1.0.23.exe") != std::wstring::npos);

    // 测试 Release 数组解析（/releases?per_page=1 返回的格式）
    std::string arrayJson = "[" + sampleJson + "]";
    ReleaseInfo arrayInfo;
    assert(ParseGitHubReleaseJson(arrayJson, arrayInfo));
    assert(arrayInfo.tagName == L"v1.0.23");
    assert(arrayInfo.assets.size() == 2);

    // 测试短文件名资产不会下溢，以及查找校验文件资产
    ReleaseInfo shortNameInfo;
    shortNameInfo.assets.push_back({ L"a", L"https://github.com/jinlong85/FastFile/releases/download/v1.0.23/a", 1 });
    shortNameInfo.assets.push_back({ L"FastFile-Setup-1.0.23.EXE", L"https://github.com/jinlong85/FastFile/releases/download/v1.0.23/FastFile-Setup-1.0.23.EXE", 1234 });
    shortNameInfo.assets.push_back({ L"SHA256SUMS.txt", L"https://github.com/jinlong85/FastFile/releases/download/v1.0.23/SHA256SUMS.txt", 90 });
    assert(shortNameInfo.FindInstallerAsset() != nullptr);
    assert(shortNameInfo.FindInstallerAsset()->name == L"FastFile-Setup-1.0.23.EXE");
    assert(shortNameInfo.FindChecksumAsset() != nullptr);

    // 测试多条 Release 跳过草稿并优先选择带安装包的发布
    std::string multiJson = R"([
        {
            "tag_name": "v1.0.99",
            "draft": true,
            "assets": [{ "name": "FastFile-Setup-1.0.99.exe", "size": 100, "browser_download_url": "https://github.com/jinlong85/FastFile/releases/download/v1.0.99/FastFile-Setup-1.0.99.exe" }]
        },
        {
            "tag_name": "v1.0.98",
            "draft": false,
            "assets": []
        },
        {
            "tag_name": "v1.0.97",
            "draft": false,
            "assets": [{ "name": "FastFile-Setup-1.0.97.exe", "size": 200, "browser_download_url": "https://github.com/jinlong85/FastFile/releases/download/v1.0.97/FastFile-Setup-1.0.97.exe" }]
        }
    ])";
    ReleaseInfo multiInfo;
    assert(ParseGitHubReleaseJson(multiJson, multiInfo));
    assert(multiInfo.tagName == L"v1.0.97");

    // 容错性测试：空 JSON 或残缺 JSON
    ReleaseInfo badInfo;
    assert(!ParseGitHubReleaseJson("", badInfo));
    assert(!ParseGitHubReleaseJson("[]", badInfo));
    assert(!ParseGitHubReleaseJson("{}", badInfo));
    assert(!ParseGitHubReleaseJson("{ invalid json }", badInfo));

    std::cout << "TestReleaseJsonParser passed." << std::endl;
}

static void TestUrlAllowlistAndSha256() {
    assert(IsAllowedUpdateUrl(L"https://github.com/jinlong85/FastFile/releases/download/v1.0.27/FastFile-Setup-1.0.27.exe"));
    assert(IsAllowedUpdateUrl(L"https://objects.githubusercontent.com/github-production-release-asset/foo.exe"));
    assert(IsAllowedUpdateUrl(L"https://release-assets.githubusercontent.com/bar.exe"));
    assert(!IsAllowedUpdateUrl(L"http://github.com/jinlong85/FastFile/releases/download/v1.0.27/FastFile-Setup-1.0.27.exe"));
    assert(!IsAllowedUpdateUrl(L"https://evilgithub.com/FastFile-Setup.exe"));
    assert(!IsAllowedUpdateUrl(L"https://github.com.evil.example/FastFile-Setup.exe"));
    assert(!IsAllowedUpdateUrl(L""));

    // 解析 SHA256SUMS.txt（支持 BOM、大写、* 前缀与多行）
    const std::string sums =
        "\xEF\xBB\xBF"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA  other.zip\r\n"
        "2CF24DBA5FB0A30E26E83B2AC5B9E29E1B161E5C1FA7425E73043362938B9824  *FastFile-Setup-1.0.28.exe\n";
    assert(ParseSha256ForAsset(sums, L"FastFile-Setup-1.0.28.exe") ==
           L"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
    assert(ParseSha256ForAsset(sums, L"missing.exe").empty());

    // 写入临时文件 "hello" 并验证 SHA-256（2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824）
    wchar_t tempDir[MAX_PATH]{};
    DWORD n = GetTempPathW(_countof(tempDir), tempDir);
    assert(n > 0 && n < _countof(tempDir));
    const std::wstring samplePath = std::wstring(tempDir) + L"FastFileSha256Test_" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE hFile = CreateFileW(samplePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(hFile != INVALID_HANDLE_VALUE);
    const char payload[] = "hello";
    DWORD written = 0;
    assert(WriteFile(hFile, payload, 5, &written, nullptr) && written == 5);
    CloseHandle(hFile);

    std::wstring actual;
    assert(VerifyFileSha256(samplePath, L"2CF24DBA5FB0A30E26E83B2AC5B9E29E1B161E5C1FA7425E73043362938B9824", &actual));
    assert(actual == L"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
    assert(!VerifyFileSha256(samplePath, L"0000000000000000000000000000000000000000000000000000000000000000"));
    DeleteFileW(samplePath.c_str());

    // 下载器拒绝非白名单 URL
    std::wstring err;
    assert(!DownloadUpdateFile(L"http://example.com/bad.exe", samplePath, nullptr, nullptr, &err));
    assert(!err.empty());

    std::cout << "TestUrlAllowlistAndSha256 passed." << std::endl;
}

static void TestInstallerArguments() {
    auto args1 = BuildInstallerArguments(L"", true);
    assert(args1 == L"--quiet --restart");

    auto args2 = BuildInstallerArguments(L"C:\\Programs\\FastFile", true);
    assert(args2 == L"--quiet --restart --dir \"C:\\Programs\\FastFile\"");

    auto args3 = BuildInstallerArguments(L"D:\\App", false);
    assert(args3 == L"--quiet --dir \"D:\\App\"");

    std::cout << "TestInstallerArguments passed." << std::endl;
}

static void TestDownloadPath() {
    auto path = GetUpdateDownloadPath(L"1.0.23");
    assert(!path.empty());
    assert(path.find(L"FastFile-Setup-1.0.23.exe") != std::wstring::npos);
    assert(path.find(L"FastFile\\Updates") != std::wstring::npos);

    std::cout << "TestDownloadPath passed." << std::endl;
}

static void TestLiveGitHubCheck() {
    // 真实联网查询 jinlong85/FastFile 仓库（当前线上为 v1.0.17）
    // 假定本地为 1.0.16 时应成功识别出有更新 v1.0.17
    auto res16 = CheckForUpdate(L"1.0.16", L"jinlong85/FastFile");
    if (res16.status == UpdateCheckStatus::Success) {
        assert(res16.hasUpdate);
        assert(res16.release.tagName.find(L"v1.") != std::wstring::npos);
        assert(res16.release.FindInstallerAsset() != nullptr);
        std::cout << "TestLiveGitHubCheck (newer version found): " << res16.release.tagName.c_str() << " passed." << std::endl;
    } else {
        // 网络环境离线或受限时不强制失败
        std::cout << "TestLiveGitHubCheck skipped or network unavailable: " << res16.message.c_str() << std::endl;
    }

    // 假定本地为 1.0.23 时应报告已是最新
    auto res23 = CheckForUpdate(L"1.0.23", L"jinlong85/FastFile");
    if (res23.status == UpdateCheckStatus::UpToDate) {
        assert(!res23.hasUpdate);
        std::cout << "TestLiveGitHubCheck (up to date): passed." << std::endl;
    }
}

int main() {
    try {
        TestSemanticVersion();
        TestReleaseJsonParser();
        TestUrlAllowlistAndSha256();
        TestInstallerArguments();
        TestDownloadPath();
        TestLiveGitHubCheck();
        std::cout << "All AutoUpdater tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "AutoUpdater test failed: " << ex.what() << std::endl;
        return 1;
    }
}
