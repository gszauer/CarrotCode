#pragma once

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm> 
#include <unordered_map>
#include "srell.hpp"

namespace TextEdit {
    enum class Highlighter {
        Text = 0,
        Code
    };

    enum class TokenType {
        Normal = 0,
        Keyword,
        Identifier,
        String,
        Number,
        Comment,
        Operator,
        Grouping,
        Preprocessor,
        Type,        
        Constant,    
        Function,    
        Regex,       
        Template,    
        Decorator,   
        Label,       
        Attribute    
    };

    struct SyntaxRule {
        srell::u32regex pattern;
        TokenType type;
    };


    enum class ActionType { // undo / redo action
        INSERT, 
        DELETE, 
    };


    struct MousePos {
        int x, y;
    };

    class Document : public std::enable_shared_from_this<Document> {
    public:
        struct Cursor {
            unsigned int line;
            unsigned int column;

            inline Cursor() : line(0), column(0) {
            }
            inline Cursor(unsigned int l, unsigned int c) : line(l), column(c) {
            } // Changed int to unsigned int for consistency
            inline bool operator==(const Cursor& other) const {
                return line == other.line && column == other.column;
            }
            inline bool operator!=(const Cursor& other) const {
                return !(*this == other);
            }
            inline bool operator<(const Cursor& other) const {
                if (line < other.line) return true;
                if (line == other.line && column < other.column) return true;
                return false;
            }
            inline bool operator>(const Cursor& other) const {
                if (line > other.line) return true;
                if (line == other.line && column > other.column) return true;
                return false;
            }
            inline bool operator<=(const Cursor& other) const {
                return (*this < other) || (*this == other);
            }
            inline bool operator>=(const Cursor& other) const {
                return (*this > other) || (*this == other);
            }
            inline void Reset() {
                line = 0;
                column = 0;
            }
        };
        struct Span {
            Cursor start;
            Cursor end;

            inline Span() = default;
            inline Span(const Cursor& s, const Cursor& e) : start(s), end(e) {
            }

            inline void Normalize() {
                // Ensure start <= end
                if (end < start) { // Check if end is strictly less than start
                    std::swap(this->start, this->end);
                }
            }
        };
        struct Line {
            friend class Document;

            std::u32string text;
            bool dirty; // Only re-tokenize if true
            std::vector<std::pair<TokenType, int>> tokens;
            bool endsInComment; // Indicates if this line ends inside a multi-line comment

            inline Line() : dirty(true), endsInComment(false) {
            }
            inline Line(const std::u32string& _text) : text(_text), dirty(true), endsInComment(false) {
            }
        protected:
            void Tokenize(std::vector<SyntaxRule>& syntax_rules, bool startInComment);
            inline void ClearTokens() {
                dirty = false;
            }
        };
        struct UndoRecord {
            ActionType type = ActionType::INSERT;

            // The text that was affected.
            // For an INSERT, this is the text that was added.
            // For a DELETE, this is the text that was removed.
            std::u32string text;

            // The location of the change.
            // For an INSERT, this is the span of the newly added text.
            // For a DELETE, this is the span of the text that was removed.
            Document::Span span;

            // We store the cursor position *before* the action was performed.
            // This lets us restore the cursor correctly when we undo.
            Cursor cursorBefore;
        };
    protected:
        Document(const Document&) = delete;
        Document& operator=(const Document&) = delete;
    public:
        Document();
        static std::shared_ptr<Document> Create();
        virtual ~Document();

        void Clear();
        void ClearHistory();
        void Load(const std::u32string& content);

        char32_t GetChar(unsigned int line, unsigned int column) const;
        unsigned int GetLineCount() const;
        //const std::u32string& GetLine(unsigned int line) const;
        const Line& GetLine(unsigned int line) const;

        void TokenizeLine(unsigned int line); // Since GetLine is const

        std::u32string GetText(const Span& span) const;

        void Insert(const std::u32string& text_to_insert); // Corrected: removed extra const
        void Remove();

        void PlaceCursor(const Cursor& position);
        void MoveCursor(const Cursor& position); // Extends selection

        bool HasSelection() const;
        Span GetSelection() const; // Returns normalized selection
        void SetSelection(const Span& span);

        const Cursor& GetCursor() const;
        const Cursor& GetAnchor() const;

        Highlighter GetHighlighter() const;
        void SetHighlighter(Highlighter l);
        void UpdateIncrementalHighlight(int linesToProcess = 5);

        void Undo();
        void Redo();
        bool CanUndo() const;
        bool CanRedo() const;

        const std::u32string& GetName() const;
        const bool IsDirty() const;
        void MarkClean();

        std::u32string DocumentAsString() const;

        void SetSource(const char* path, bool inMemoryOnly);
        void Save();
        void SaveAs();
        void SaveIfNeededOnClose();
    protected:
        Cursor SanitizeCursor(const Cursor& pos) const;
        void AddUndoRecord(UndoRecord&& record);

        std::string mBackingFilePath;
        std::string mBackingFileName;
        std::u32string mU32FileName;

        std::vector<Line> mLines;

        Highlighter mActiveHighlighter;
        unsigned int mFirstDirtyLine;

        // mCurrent is always the Current Cursor for the document class to use for things like insert
        Cursor mAnchor; // If nothing is selected, mAnchor and mCurrent are always the same
        Cursor mCurrent; // If there is a selection, mAnchor is where it starts (fixed point), and mCurrent is where it ends (moving point)

        bool mDirty;

        // Internal modification methods that do NOT create undo records
        // to prevent recursion when Undo/Redo are called.
        void InsertInternal(const Cursor& position, const std::u32string& text, Cursor& finalCursorPos);
        void RemoveInternal(const Span& span);

        std::deque<UndoRecord> mUndoStack;
        std::deque<UndoRecord> mRedoStack;
    };
}