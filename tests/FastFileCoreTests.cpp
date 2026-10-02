#include "FastFileCore.h"
#include "TabLayout.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void CheckEqual(const char* name, const std::wstring& actual, const std::wstring& expected)
{
    if (actual == expected)
        return;
    std::wcerr << L"FAIL " << name << L": expected [" << expected << L"], got [" << actual << L"]\n";
    ++failures;
}

} // namespace

int main()
{
    for (int scale : { 1, 2, 3 }) {
        const std::vector<int> preferred{ 100 * scale, 84 * scale, 200 * scale, 220 * scale };
        if (TabLayout::Fit(preferred, 80 * scale, 604 * scale) != preferred) {
            std::cerr << "FAIL loose tabs must retain preferred content widths\n"; ++failures;
        }
        const auto tight = TabLayout::Fit(preferred, 80 * scale, 500 * scale);
        if (tight != std::vector<int>{100 * scale, 84 * scale, 158 * scale, 158 * scale}) {
            std::cerr << "FAIL tight tabs must preserve short names and share remaining room among long names\n"; ++failures;
        }
        const auto overflow = TabLayout::Fit(preferred, 80 * scale, 300 * scale);
        if (overflow != std::vector<int>{80 * scale, 80 * scale, 80 * scale, 80 * scale}) {
            std::cerr << "FAIL overflow must keep every minimum width for horizontal scrolling\n"; ++failures;
        }
    }
    using FastFileCore::FormatFileSize;
    using FastFileCore::GetLeafName;
    using FastFileCore::IsValidLeafName;
    using FastFileCore::ParentPath;
    using FastFileCore::RenameSelectionEnd;

    if (!IsValidLeafName(L"report.txt")) {
        std::cerr << "FAIL valid leaf name was rejected\n";
        ++failures;
    }
    if (IsValidLeafName(L"")) {
        std::cerr << "FAIL empty leaf name was accepted\n";
        ++failures;
    }
    if (IsValidLeafName(L".")) {
        std::cerr << "FAIL current-directory name was accepted\n";
        ++failures;
    }
    if (IsValidLeafName(L"..")) {
        std::cerr << "FAIL parent-directory name was accepted\n";
        ++failures;
    }
    if (IsValidLeafName(L"subfolder\\file.txt") || IsValidLeafName(L"subfolder/file.txt")) {
        std::cerr << "FAIL path separators were accepted in a leaf name\n";
        ++failures;
    }

    CheckEqual("parent of drive root", ParentPath(L"C:\\"), L"");
    CheckEqual("parent of drive child", ParentPath(L"C:\\Users\\Jinlong"), L"C:\\Users");
    CheckEqual("parent with trailing separator", ParentPath(L"C:\\Users\\"), L"C:\\");
    CheckEqual("parent with forward slashes", ParentPath(L"C:/Users/Jinlong"), L"C:/Users");
    CheckEqual("parent of relative leaf", ParentPath(L"readme.txt"), L"");

    CheckEqual("leaf from Windows path", GetLeafName(L"C:\\Users\\readme.txt"), L"readme.txt");
    CheckEqual("leaf from slash path", GetLeafName(L"folder/subfolder"), L"subfolder");
    CheckEqual("leaf from bare name", GetLeafName(L"readme.txt"), L"readme.txt");
    CheckEqual("leaf after trailing separator", GetLeafName(L"C:\\folder\\"), L"");
    if (RenameSelectionEnd(L"report.txt", false) != 6
        || RenameSelectionEnd(L"archive.tar.gz", false) != 11
        || RenameSelectionEnd(L".gitignore", false) != 10
        || RenameSelectionEnd(L"folder.name", true) != 11) {
        std::cerr << "FAIL Explorer-style rename selection range\n";
        ++failures;
    }

    CheckEqual("zero bytes", FormatFileSize(0), L"0 B");
    CheckEqual("byte unit upper boundary", FormatFileSize(1023), L"1023 B");
    CheckEqual("kilobyte boundary", FormatFileSize(1024), L"1.0 KB");
    CheckEqual("kilobyte rounding", FormatFileSize(1024ULL * 1024ULL - 1), L"1024.0 KB");
    CheckEqual("megabyte boundary", FormatFileSize(1024ULL * 1024ULL), L"1.0 MB");
    CheckEqual("gigabyte boundary", FormatFileSize(1024ULL * 1024ULL * 1024ULL), L"1.00 GB");
    CheckEqual("gigabyte fraction", FormatFileSize(3ULL * 1024ULL * 1024ULL * 512ULL), L"1.50 GB");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "FastFileCoreTests: all checks passed\n";
    return 0;
}
