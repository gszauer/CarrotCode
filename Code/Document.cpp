#include "Document.h" 
#include "Platform.h"
#include <algorithm>  
#include <utility>    
#include "Styles.h"
#include <vector>
#include <string>
#include <set>
#include <thread>
#include <atomic>
#include <codecvt>
#include <unordered_map>

std::u32string Utf8ToUtf32(const char* utf8_string, unsigned int bytes);
std::string Utf32ToUtf8(const std::u32string& utf32_string);
std::wstring u32string_to_wstring(const std::u32string& u32str);
std::u32string wstring_to_u32string(const std::wstring& wstr);

namespace TextEdit {
    static const std::u32string S_EMPTY_U32STRING = U"";
    static const size_t UNDO_LIMIT = 2000;

   
    

    // --- Helper Functions ---
    Document::Cursor Document::SanitizeCursor(const Cursor& _pos) const {
        if (mLines.empty()) {
            return { 0, 0 };
        }

        Cursor pos = _pos;
        if (pos.line >= mLines.size()) {
            pos.line = static_cast<unsigned int>(mLines.size() - 1);
            pos.column = static_cast<unsigned int>(mLines[pos.line].text.length());
        }
        else {
            if (pos.column > mLines[pos.line].text.length()) {
                pos.column = static_cast<unsigned int>(mLines[pos.line].text.length());
            }
        }
        return pos;
    }

    std::shared_ptr<Document> Document::Create() {
        return std::make_shared<Document>();
    }

    void Document::Line::Tokenize(std::vector<SyntaxRule>& syntax_rules, bool startInComment) {
        if (!dirty) {
            return;
        }

        dirty = false;
        tokens.clear();
        std::u32string _text = text;
        size_t pos = 0;
        bool inMultiLineComment = startInComment;
        endsInComment = false;

        // Static regex for multi-line comment boundaries
        static const srell::u32regex multiCommentEndPattern(UR"(\*/)");

        while (pos < _text.size()) {
            if (inMultiLineComment) {
                // Look for comment end
                srell::match_results<std::u32string::const_iterator> match;
                if (srell::regex_search(_text.cbegin() + pos, _text.cend(), match, multiCommentEndPattern)) {
                    size_t commentEnd = match[0].first - _text.cbegin() + match[0].length(); // Position after */
                    tokens.push_back(std::pair<TokenType, int>(TokenType::Comment, pos));
                    pos = commentEnd;
                    inMultiLineComment = false;
                }
                else {
                    tokens.push_back(std::pair<TokenType, int>(TokenType::Comment, pos));
                    pos = _text.size();
                    endsInComment = true;
                    break;
                }
            }
            else {
                bool matched = false;
                for (const auto& rule : syntax_rules) {
                    srell::match_results<std::u32string::const_iterator> match;
                    if (srell::regex_search(_text.cbegin() + pos, _text.cend(), match, rule.pattern, srell::regex_constants::match_continuous)) {
                        if (!match.empty()) {
                            size_t start = pos;
                            size_t end = pos + match[0].length();
                            if (rule.type == TokenType::Comment && _text.substr(pos, 2) == U"/*") {
                                // Handle multi-line comment
                                tokens.push_back(std::pair<TokenType, int>(TokenType::Comment, start));
                                pos = end;
                                if (match.str().find(U"*/") != std::u32string::npos) {
                                    // Complete multi-line comment (/* ... */)
                                    inMultiLineComment = false;
                                }
                                else {
                                    // Unclosed multi-line comment start
                                    inMultiLineComment = true;
                                    endsInComment = true;
                                }
                            }
                            else {
                                tokens.push_back(std::pair<TokenType, int>(rule.type, start));
                                pos = end;
                            }
                            matched = true;
                            break;
                        }
                    }
                }
                if (!matched) {
                    tokens.push_back(std::pair<TokenType, int>(TokenType::Normal, pos));
                    ++pos;
                }
            }
        }
    }

    Document::Document() : mCurrent(0, 0), mAnchor(0, 0), mFirstDirtyLine(0), mDirty(false) {
        // A document always starts with at least one empty line.
        mActiveHighlighter = Highlighter::Code;
        mLines.emplace_back(U"");
    }

    Document::~Document() {
        // Default destructor is sufficient as std::vector and std::u32string manage their own memory.
    }

    void Document::ClearHistory() {
        mCurrent = Cursor(0, 0);
        mAnchor = Cursor(0, 0);
        mUndoStack.clear();
        mRedoStack.clear();
    }

    void Document::Clear() {
        mLines.clear();
        mLines.emplace_back(U"");
        mLines[0].dirty = true;
        mFirstDirtyLine = 0;
        mCurrent = Cursor(0, 0);
        mAnchor = Cursor(0, 0);
        mUndoStack.clear();
        mRedoStack.clear();
    }

    void Document::Load(const std::u32string& content) {
        Clear(); // Start from a clean state (one empty line, cursors at {0,0}).

        if (content.empty()) {
            return;
        }

        mLines.clear();
        size_t start_pos = 0;
        size_t find_pos;

        do {
            find_pos = content.find(U'\n', start_pos);
            if (find_pos == std::u32string::npos) {
                mLines.push_back(content.substr(start_pos));
                break;
            }
            else {
                mLines.push_back(content.substr(start_pos, find_pos - start_pos));
                start_pos = find_pos + 1;
            }
        } while (start_pos <= content.length() || find_pos != std::u32string::npos);

        if (mLines.empty()) {
            mLines.emplace_back(U"");
        }

        for (auto& line : mLines) {
            line.dirty = true;
        }
        mFirstDirtyLine = 0;
    }

    unsigned int Document::GetLineCount() const {
        return static_cast<unsigned int>(mLines.size());
    }

    char32_t Document::GetChar(unsigned int line, unsigned int column) const {
        if (line >= mLines.size()) {
            return U'\0'; // Line out of bounds.
        }
        const std::u32string& currentLine = mLines[line].text;
        if (column >= currentLine.length()) {
            // For column == length(), it's like asking for the char that a newline would represent conceptually.
            // However, for GetChar, if it's past the actual characters, return null.
            // The only exception could be if line < GetLineCount()-1 and column == currentLine.length(), it could be U'\n'.
            // But standard behavior is to return U'\0' for out-of-bounds char access.
            return U'\0';
        }
        return currentLine[column];
    }

    //const std::u32string& Document::GetLine(unsigned int line) const {
    const Document::Line& Document::GetLine(unsigned int line) const {
        return mLines[line];
    }

    Highlighter Document::GetHighlighter() const {
        return mActiveHighlighter;
    }

    void Document::SetHighlighter(Highlighter l) {
        mActiveHighlighter = l;
        // Mark all lines as dirty to re-tokenize
        for (auto& line : mLines) {
            line.dirty = true;
        }
        mFirstDirtyLine = 0;
    }

    void Document::UpdateIncrementalHighlight(int linesToProcess) {
        if (mActiveHighlighter != Highlighter::Code || mFirstDirtyLine >= mLines.size()) {
            return; // No highlighting needed or already processed all lines
        }

        unsigned int endLine = std::min(mFirstDirtyLine + static_cast<unsigned int>(linesToProcess), static_cast<unsigned int>(mLines.size()));
        bool prevLineEndsInComment = mFirstDirtyLine > 0 ? mLines[mFirstDirtyLine - 1].endsInComment : false;

        for (unsigned int line = mFirstDirtyLine; line < endLine; ++line) {
            mLines[line].dirty = true;
            mLines[line].Tokenize(Styles::syntax_rules, prevLineEndsInComment);
            prevLineEndsInComment = mLines[line].endsInComment;
        }

        mFirstDirtyLine = endLine;
    }

    void Document::TokenizeLine(unsigned int line) {
        if (mActiveHighlighter == Highlighter::Text) {
            mLines[line].ClearTokens();
        }
        else if (mActiveHighlighter == Highlighter::Code) {
            

            bool startInComment = (line > 0) ? mLines[line - 1].endsInComment : false;
            mLines[line].Tokenize(TextEdit::Styles::syntax_rules, startInComment);
        }
    }

    std::u32string Document::GetText(const Span& span) const {
        Span currentSpan = span; // Make a copy to normalize.
        currentSpan.Normalize(); // Ensures start <= end
        Cursor start = SanitizeCursor(currentSpan.start);
        Cursor end = SanitizeCursor(currentSpan.end);

        if (!(start < end)) { // If start is not strictly less than end, span is empty or invalid.
            return U"";
        }

        std::u32string result_text;
        if (start.line == end.line) {
            result_text = mLines[start.line].text.substr(start.column, end.column - start.column);
        }
        else {
            result_text += mLines[start.line].text.substr(start.column);
            result_text += U'\n';

            for (unsigned int i = start.line + 1; i < end.line; ++i) {
                result_text += mLines[i].text;
                result_text += U'\n';
            }

            // Only append if there's content to take from the last line.
            // And only if end.column > 0, otherwise it's just the newline already added (or start of line).
            if (end.column > 0) {
                result_text += mLines[end.line].text.substr(0, end.column);
            }
        }
        return std::move(result_text); // Rely on RVO or std::move if compiler doesn't do RVO.
        // explicit std::move for local is fine.
    }

    

    void Document::InsertInternal(const Cursor& position, const std::u32string& text, Cursor& finalCursorPos) {
        // This internal version performs a pure insertion at a given position
        // without reference to selection or the main cursor. It does not generate an undo record.

        unsigned int currentLineIdx = position.line;
        unsigned int currentCol = position.column;

        // 1. Split the text to be inserted into separate lines
        std::vector<std::u32string> lines_to_insert;
        if (text.empty()) {
            lines_to_insert.emplace_back(U"");
        }
        else {
            size_t start_p = 0;
            size_t find_p;
            do {
                find_p = text.find(U'\n', start_p);
                if (find_p == std::u32string::npos) {
                    lines_to_insert.push_back(std::move(text.substr(start_p)));
                    break;
                }
                else {
                    lines_to_insert.push_back(std::move(text.substr(start_p, find_p - start_p)));
                    start_p = find_p + 1;
                }
            } while (start_p <= text.length() || find_p != std::u32string::npos);
        }

        if (lines_to_insert.empty()) {
            lines_to_insert.emplace_back(U"");
        }

        // 2. Perform the insertion
        std::u32string suffix_of_current_line = mLines[currentLineIdx].text.substr(currentCol);
        mLines[currentLineIdx].text.erase(currentCol);
        mLines[currentLineIdx].text += lines_to_insert[0];
        mLines[currentLineIdx].dirty = true;

        mFirstDirtyLine = std::min(mFirstDirtyLine, currentLineIdx);

        if (lines_to_insert.size() == 1) {
            // Single-line insertion
            finalCursorPos.line = currentLineIdx;
            finalCursorPos.column = static_cast<unsigned int>(mLines[currentLineIdx].text.length());
            mLines[currentLineIdx].text += suffix_of_current_line;
            mLines[currentLineIdx].dirty = true;
        }
        else {
            // Multi-line insertion
            std::u32string last_inserted_segment = lines_to_insert.back();
            finalCursorPos.column = static_cast<unsigned int>(last_inserted_segment.length());

            std::u32string line_for_suffix = std::move(last_inserted_segment);
            line_for_suffix += suffix_of_current_line;

            unsigned int insert_at_line_idx = currentLineIdx + 1;

            if (lines_to_insert.size() > 2) {
                // Use vector's insert for the intermediate lines for efficiency
                mLines.insert(mLines.begin() + insert_at_line_idx,
                    std::make_move_iterator(lines_to_insert.begin() + 1),
                    std::make_move_iterator(lines_to_insert.end() - 1));

                for (unsigned int i = insert_at_line_idx; i < insert_at_line_idx + lines_to_insert.size() - 2; ++i) {
                    mLines[i].dirty = true;
                }
                insert_at_line_idx += static_cast<unsigned int>(lines_to_insert.size() - 2);
            }

            mLines.insert(mLines.begin() + insert_at_line_idx, Line(std::move(line_for_suffix)));
            mLines[insert_at_line_idx].dirty = true;
            finalCursorPos.line = insert_at_line_idx;
        }
    }

    void Document::RemoveInternal(const Span& span) {
        // This internal version performs a pure removal on a given span
        // without reference to selection or the main cursor. It does not generate an undo record.

        // The input span is assumed to be normalized (start <= end)
        Cursor startPos = span.start;
        Cursor endPos = span.end;

        // If the span is empty, there is nothing to do.
        if (startPos == endPos) {
            return;
        }

        mFirstDirtyLine = std::min(mFirstDirtyLine, startPos.line);

        if (startPos.line == endPos.line) {
            // Single-line removal
            std::u32string& line = mLines[startPos.line].text;
            line.erase(startPos.column, endPos.column - startPos.column);
            mLines[startPos.line].dirty = true;
        }
        else {
            // Multi-line removal
            std::u32string& firstLineAffected = mLines[startPos.line].text;
            const std::u32string& lastLineContent = mLines[endPos.line].text;

            // Take the prefix from the first line and suffix from the last line
            std::u32string prefix = firstLineAffected.substr(0, startPos.column);
            std::u32string suffix;
            if (endPos.column < lastLineContent.length()) {
                suffix = lastLineContent.substr(endPos.column);
            }

            // Combine them into the first line
            mLines[startPos.line].text = prefix + suffix;
            mLines[startPos.line].dirty = true;

            // Determine the range of full lines to delete
            unsigned int first_line_idx_to_remove = startPos.line + 1;
            unsigned int last_line_idx_to_remove = endPos.line;

            if (first_line_idx_to_remove <= last_line_idx_to_remove && first_line_idx_to_remove < mLines.size()) {
                auto it_remove_start = mLines.begin() + first_line_idx_to_remove;
                // The end iterator is exclusive, so we use last_line_idx_to_remove + 1
                auto it_remove_end = mLines.begin() + std::min(static_cast<size_t>(last_line_idx_to_remove + 1), mLines.size());

                if (it_remove_start < it_remove_end) {
                    mLines.erase(it_remove_start, it_remove_end);
                }
            }
        }
    }

    void Document::Insert(const std::u32string& text_to_insert) {
        // 1. If there is a selection, this Insert operation should first remove it.
        // The Remove() call will generate its own, separate undo record for the deletion.
        if (HasSelection()) {
            Remove();
        }

        // 2. Capture the state *before* the insertion for the undo record.
        Cursor cursorBefore = mCurrent; // The cursor position before this action.

        // 3. Delegate the core insertion logic to the internal method.
        // It will return the final position of the cursor after the text is inserted.
        Cursor finalCursorPos;
        InsertInternal(cursorBefore, text_to_insert, finalCursorPos);

        // 4. Create a record of this action so it can be undone.
        UndoRecord insertRecord;
        insertRecord.type = ActionType::INSERT;
        insertRecord.cursorBefore = cursorBefore;
        insertRecord.text = text_to_insert; // Make a copy for the undo record

        // The span of the inserted text is from where the cursor was to where it ended up.
        insertRecord.span.start = cursorBefore;
        insertRecord.span.end = finalCursorPos;

        // Add the record to the undo stack. std::move is used for efficiency.
        AddUndoRecord(std::move(insertRecord));

        // 5. Place the document's main cursor at the end of the newly inserted text.
        PlaceCursor(finalCursorPos);
        mDirty = true;
    }

    void Document::Remove() {
        // 1. If there is no selection, there is nothing to remove.
        if (!HasSelection()) {
            return;
        }

        // 2. Capture the state *before* the removal for the undo record.
        Span selection = GetSelection(); // GetSelection() returns a normalized span.
        Cursor cursorBefore = mCurrent;   // The active cursor position before this action.

        // Crucially, get the text that is about to be deleted.
        std::u32string deletedText = GetText(selection);

        // 3. Delegate the core removal logic to the internal method.
        RemoveInternal(selection);

        // 4. Create a record of this action so it can be undone.
        UndoRecord removeRecord;
        removeRecord.type = ActionType::DELETE;
        removeRecord.cursorBefore = cursorBefore;
        removeRecord.span = selection;
        removeRecord.text = std::move(deletedText); // Move the captured text into the record.

        // Add the record to the undo stack.
        AddUndoRecord(std::move(removeRecord));

        // 5. Place the document's main cursor at the start of where the selection was.
        PlaceCursor(selection.start);
        mDirty = true;
    }

    void Document::Undo() {
        // 1. If there's nothing to undo, exit.
        if (mUndoStack.empty()) {
            return;
        }

        // 2. Take the most recent action record from the undo stack.
        // We use std::move to efficiently transfer ownership of the record.
        UndoRecord record = std::move(mUndoStack.back());
        mUndoStack.pop_back();

        // 3. Perform the inverse action.
        if (record.type == ActionType::INSERT) {
            // The original action was an INSERT, so to undo it, we must REMOVE the text.
            // The record's span tells us exactly what range of text to remove.
            RemoveInternal(record.span);
        }
        else { // record.type == ActionType::DELETE
            // The original action was a DELETE, so to undo it, we must INSERT the text back.
            // The record's span.start and text tell us where and what to insert.
            // The final cursor position isn't needed here, as we are about to place it manually.
            Cursor ignoredCursor;
            InsertInternal(record.span.start, record.text, ignoredCursor);
        }

        // 4. After performing the inverse action, place the cursor where it was *before*
        // the original action took place. This is a crucial part of the user experience.
        PlaceCursor(record.cursorBefore);

        // 5. Finally, move the record onto the redo stack, so the action can be redone.
        mRedoStack.push_back(std::move(record));
    }

    void Document::Redo() {
        // 1. If there's nothing to redo, exit.
        if (mRedoStack.empty()) {
            return;
        }

        // 2. Take the most recent undone action from the redo stack.
        UndoRecord record = std::move(mRedoStack.back());
        mRedoStack.pop_back();

        // 3. Re-apply the original action.
        Cursor cursorAfterRedo;
        if (record.type == ActionType::INSERT) {
            // The original action was an INSERT. We re-apply it by calling insertInternal.
            // We need to capture the cursor position after the insertion is complete.
            InsertInternal(record.span.start, record.text, cursorAfterRedo);
        }
        else { // record.type == ActionType::DELETE
            // The original action was a DELETE. We re-apply it by calling removeInternal.
            RemoveInternal(record.span);
            // After a deletion, the cursor should be at the start of the deleted span.
            cursorAfterRedo = record.span.start;
        }

        // 4. Place the cursor where it should be *after* the action is redone.
        PlaceCursor(cursorAfterRedo);

        // 5. Move the record back to the undo stack, so it can be undone again.
        mUndoStack.push_back(std::move(record));
    }

    bool Document::CanUndo() const {
        // The user can perform an "undo" operation if the undo stack
        // contains at least one action record.
        return !mUndoStack.empty();
    }

    static Document* gHackLastSetBeforeSaveAs = nullptr;
    void Document::Save() {
#ifdef __EMSCRIPTEN__
        SaveAs();
#else
        if (mBackingFileName.length() == 0 || mBackingFilePath.length() == 0) {
            SaveAs();
            return;
        }

        std::u32string content = DocumentAsString();
        std::string adjusted = Utf32ToUtf8(content);

        gHackLastSetBeforeSaveAs = this;
        PlatformWriteFile(mBackingFilePath.c_str(), (unsigned char*)adjusted.c_str(), (unsigned int)adjusted.size(),
            [](const char* path, bool success, void* userData) {
                if (success) {
                    gHackLastSetBeforeSaveAs->MarkClean();
                }
        }, 0, 0);
#endif
    }

    void Document::SaveAs() {
        std::u32string content = DocumentAsString();
        std::string adjusted = Utf32ToUtf8(content);

        gHackLastSetBeforeSaveAs = this;

#ifdef __EMSCRIPTEN__
        if (mBackingFileName.size() == 0) {
            mBackingFileName = "Untitled";
        }
#endif // !__EMSCRIPTEN__

        PlatformSetNextSaveAsName(mBackingFileName.c_str());
        PlatformSaveAs((unsigned char*)adjusted.c_str(), (unsigned int)adjusted.size(), [](const char* path) {
            if (path != 0) {
#ifndef __EMSCRIPTEN__
                TextEdit::gHackLastSetBeforeSaveAs->SetSource(path, false);
                TextEdit::gHackLastSetBeforeSaveAs->MarkClean();
#endif // !__EMSCRIPTEN__
            }
        });
    }

    static Document* gHackLastSetBeforeSave = nullptr;
    void Document::SaveIfNeededOnClose() {
        if (IsDirty()) {
            std::string msg = "Untitled";
            if (mBackingFileName.length() > 0) {
                msg = mBackingFileName;
            }
            msg += " has unsaved changes.\nSave now?";
            gHackLastSetBeforeSave = this;
            PlatformYesNoAlert(msg.c_str(), [](bool result) {
                if (result) {
                    gHackLastSetBeforeSave->Save();
                }
            });
        }
    }


    void Document::SetSource(const char* path, bool memOnly) {
        std::string pathStr(path);
        size_t lastSlash = pathStr.find_last_of("/\\");
        std::string filename = (lastSlash != std::string::npos) ?
            pathStr.substr(lastSlash + 1) : pathStr;

        mBackingFilePath = path;


        mBackingFileName = filename;
        mU32FileName = U"";
        for (char c : filename) {
            mU32FileName += static_cast<char32_t>(c);
        }

        if (memOnly) {
            mBackingFilePath.clear();
        }
    }

    const bool Document::IsDirty() const {
#ifdef __EMSCRIPTEN__
        return true;
#else
        if (mBackingFileName.size() == 0 || mBackingFilePath.size() == 0) {
            if (mLines.size() == 1 && mLines[0].text.size() == 0) {
                return false;
            }
            return true;
        }

        return mDirty;
#endif
    }

 void Document::MarkClean() {
        mDirty = false;
    }

    std::u32string Document::DocumentAsString() const {
        if (mLines.empty()) {
            return U"";
        }

        // Calculate total size needed to avoid multiple reallocations
        size_t totalSize = 1; // Start with 1 for the BOM
        for (size_t i = 0; i < mLines.size(); ++i) {
            totalSize += mLines[i].text.length();
            if (i < mLines.size() - 1) {
                totalSize += 1; // For newline character
            }
        }

        // Reserve space for efficiency
        std::u32string result;
        result.reserve(totalSize);

        // Add UTF-32 BOM (Byte Order Mark)
        //result.push_back(static_cast<char32_t>(0xFEFF));

        // Build the string
        for (size_t i = 0; i < mLines.size(); ++i) {
            result += mLines[i].text;
            if (i < mLines.size() - 1) {
                result += U'\n';
            }
        }

        return result;
    }

    const std::u32string& Document::GetName() const {
        static std::u32string empty = U"Untitled";
        
        if (mU32FileName.size() != 0) {
            return mU32FileName;
        }
        return empty;
    }

    bool Document::CanRedo() const {
        // The user can perform a "redo" operation if the redo stack
        // contains at least one action record that was previously undone.
        return !mRedoStack.empty();
    }

    void Document::AddUndoRecord(UndoRecord&& record) {
        // 1. Move the new record onto the back of the undo stack.
        mUndoStack.push_back(std::move(record));

        // 2. NEW: Check if the stack has grown beyond the defined limit.
        if (mUndoStack.size() > UNDO_LIMIT) {
            // If it has, remove the oldest element from the front of the deque.
            // This is a very fast operation for a std::deque.
            mUndoStack.pop_front();
        }

        // 3. A new action always invalidates the redo history.
        mRedoStack.clear();
    }

    void Document::PlaceCursor(const Cursor& position) {
        mCurrent = SanitizeCursor(position);
        mAnchor = mCurrent;
    }

    void Document::MoveCursor(const Cursor& position) {
        mCurrent = SanitizeCursor(position);
        // mAnchor remains, creating/extending selection.
    }

    bool Document::HasSelection() const {
        return mAnchor != mCurrent;
    }

    Document::Span Document::GetSelection() const {
        // mAnchor and mCurrent are presumed to be always sanitized.
        Span selection(mAnchor, mCurrent);
        selection.Normalize();
        return selection;
    }

    void Document::SetSelection(const Span& span) {
        mAnchor = SanitizeCursor(span.start);
        mCurrent = SanitizeCursor(span.end);
    }

    const Document::Cursor& Document::GetCursor() const {
        return mCurrent;
    }

    const Document::Cursor& Document::GetAnchor() const {
        return mAnchor;
    }

} // namespace TextEdit