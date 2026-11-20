#pragma once
#include <string>
#include <utility>
#include <vector>

class UndoHistory {
public:
    enum class Kind { Rename, CopyFiles };

    struct Entry {
        Kind kind = Kind::Rename;
        std::wstring oldPath;
        std::wstring newPath;
        std::vector<std::wstring> copiedDests;
    };

    static constexpr size_t kDefaultMaxEntries = 64;

    explicit UndoHistory(size_t maxEntries = kDefaultMaxEntries)
        : m_maxEntries(maxEntries > 0 ? maxEntries : 1) {}

    void PushRename(const std::wstring& oldPath, const std::wstring& newPath);
    void PushCopy(const std::vector<std::pair<std::wstring, std::wstring>>& completedCopies);

    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    bool PopUndo(Entry& outEntry);
    bool PopRedo(Entry& outEntry);
    void PushRedoEntry(Entry entry);
    void PushUndoEntry(Entry entry);

    void Clear();

    size_t UndoCount() const { return m_undoStack.size(); }
    size_t RedoCount() const { return m_redoStack.size(); }

private:
    void Trim(std::vector<Entry>& stack);

    size_t m_maxEntries = kDefaultMaxEntries;
    std::vector<Entry> m_undoStack;
    std::vector<Entry> m_redoStack;
};
