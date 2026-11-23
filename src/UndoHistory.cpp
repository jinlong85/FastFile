#include "UndoHistory.h"

void UndoHistory::Trim(std::vector<Entry>& stack) {
    if (stack.size() > m_maxEntries) {
        stack.erase(stack.begin(), stack.begin() + static_cast<ptrdiff_t>(stack.size() - m_maxEntries));
    }
}

void UndoHistory::PushRename(const std::wstring& oldPath, const std::wstring& newPath) {
    if (oldPath.empty() || newPath.empty()) return;
    m_undoStack.push_back({ Kind::Rename, oldPath, newPath, {} });
    Trim(m_undoStack);
    m_redoStack.clear();
}

void UndoHistory::PushCopy(const std::vector<std::pair<std::wstring, std::wstring>>& completedCopies) {
    std::vector<std::wstring> dests;
    dests.reserve(completedCopies.size());
    for (const auto& pair : completedCopies) {
        if (!pair.second.empty()) dests.push_back(pair.second);
    }
    if (dests.empty()) return;
    m_undoStack.push_back({ Kind::CopyFiles, L"", L"", std::move(dests) });
    Trim(m_undoStack);
    m_redoStack.clear();
}

bool UndoHistory::PopUndo(Entry& outEntry) {
    if (m_undoStack.empty()) return false;
    outEntry = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    return true;
}

bool UndoHistory::PopRedo(Entry& outEntry) {
    if (m_redoStack.empty()) return false;
    outEntry = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    return true;
}

void UndoHistory::PushRedoEntry(Entry entry) {
    m_redoStack.push_back(std::move(entry));
    Trim(m_redoStack);
}

void UndoHistory::PushUndoEntry(Entry entry) {
    m_undoStack.push_back(std::move(entry));
    Trim(m_undoStack);
}

void UndoHistory::Clear() {
    m_undoStack.clear();
    m_redoStack.clear();
}
