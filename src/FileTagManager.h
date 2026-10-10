#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <windows.h>

enum class FileTagColor : uint8_t {
    None = 0,
    Red = 1,     // #FF4D4F 重要/紧急
    Orange = 2,  // #FF7A45 进行中
    Yellow = 3,  // #FFC53D 待办/关注
    Green = 4,   // #52C41A 已完成/已审
    Blue = 5,    // #1890FF 工作/项目
    Purple = 6   // #722ED1 创意/归档
};

struct FileTagInfo {
    FileTagColor color = FileTagColor::None;
    bool starred = false;
};

class FileTagManager {
public:
    static FileTagManager& Instance();

    void Load();
    bool Save() const;

    FileTagInfo GetTag(const std::wstring& path) const;
    void SetColor(const std::wstring& path, FileTagColor color);
    void SetStarred(const std::wstring& path, bool starred);
    void ToggleStarred(const std::wstring& path);
    void ClearTag(const std::wstring& path);

    static COLORREF GetColorRef(FileTagColor color);
    static const wchar_t* GetColorName(FileTagColor color);

    const std::unordered_map<std::wstring, FileTagInfo>& GetAll() const { return m_tags; }

    static std::wstring Normalize(const std::wstring& path);

    static void DrawTagDot(HDC dc, int cx, int cy, int radius, COLORREF fill, COLORREF border = RGB(255, 255, 255));
    static void DrawStar(HDC dc, int cx, int cy, int outerR, COLORREF fill = RGB(255, 184, 0), COLORREF border = RGB(212, 155, 0));

private:
    FileTagManager();
    static std::wstring GetTagFilePath();

    std::unordered_map<std::wstring, FileTagInfo> m_tags;
    bool m_loaded = false;
};
