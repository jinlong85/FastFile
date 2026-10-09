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

    // 容错性测试：空 JSON 或残缺 JSON
    ReleaseInfo badInfo;
    assert(!ParseGitHubReleaseJson("", badInfo));
    assert(!ParseGitHubReleaseJson("[]", badInfo));
    assert(!ParseGitHubReleaseJson("{}", badInfo));
    assert(!ParseGitHubReleaseJson("{ invalid json }", badInfo));

    std::cout << "TestReleaseJsonParser passed." << std::endl;
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
