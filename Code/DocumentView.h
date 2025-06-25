#pragma once

#include "Renderer.h"
#include "Document.h"
#include "Font.h"
#include "application.h"
#include <string> // For std::wstring in Clipboard namespace

namespace TextEdit {
    
    class DocumentView {
        std::shared_ptr<Renderer> mRenderer;
        std::shared_ptr<Document> mDocument;
        std::shared_ptr<Font> mFont;
        std::shared_ptr<Font> mSmallFont;

        // Viewport and Scroll State
        float mScrollX;
        float mScrollY;
        float mViewX, mViewY, mViewWidth, mViewHeight; // Display area for the view
        float mTotalContentWidth;
        float mTotalContentHeight;

        // Cursor and Selection State
        float mDesiredColumnX;      // Desired X position for vertical cursor movement (in world/content pixels)
        float mCursorBlinkTimer;
        bool mShowCursor;
        bool mIsSelecting;          // True if mouse is dragging to select text
        bool mIsMouseDown;          // True if a mouse button is currently pressed down
        Document::Cursor mSelectionDragStartDocPos; // Document position where mouse selection drag started

        // Input State
        MousePos mLastMousePos;

        // Click state for double/triple click (optional, not fully implemented here)
        unsigned long mLastClickTime;
        int mClickCount;
        MousePos mLastClickMousePos;

        // Scrollbar interaction state
        Rect mVertScrollbarRect; // Screen rect of the vertical scrollbar nib
        Rect mHorzScrollbarRect; // Screen rect of the horizontal scrollbar nib
        bool mIsDraggingVertScrollbar;
        bool mIsDraggingHorzScrollbar;
        float mDragScrollbarOffset; // Offset from nib top/left to mouse click point

        // Highlighter Dropdown State
        Rect mHighlighterButtonRect;   // Screen rect of the highlighter dropdown button
        bool mIsHighlighterDropdownOpen; // True if dropdown is open
        std::vector<std::pair<std::u32string, Highlighter>> mHighlighterOptions; // Available highlighters
        int mSelectedHighlighterIndex; // Index of currently selected highlighter

        // Context Menu State
        bool mIsContextMenuOpen; // True if context menu is open
        Rect mContextMenuRect;   // Screen rect of the context menu
        std::vector<std::u32string> mContextMenuOptions; // Context menu options (Cut, Copy, Paste)
        MousePos mContextMenuPos; // Screen position where context menu was opened

        float mLineNumberWidth;
        Rect mUndoButtonRect;
        Rect mRedoButtonRect;
    public:
        DocumentView(std::shared_ptr<Renderer> renderer, std::shared_ptr<Document> doc, std::shared_ptr<Font> font, std::shared_ptr<Font> smallFont);
        virtual ~DocumentView();
        void ResetView();

        void Update(float deltaTime); // For animating the cursor tick, auto-scroll
        void Display(float x, float y, float w, float h); // x,y,w,h define the viewport for this view
        void OnInput(const InputEvent& e);

        void CloseMenus();

        inline std::shared_ptr<Document> GetTarget() {
            return mDocument;
        }

        // Context Menu Actions
        void PerformCut();
        void PerformCopy();
        void PerformPaste();
    private:
        // Input Handling
        void HandleKeyboardInput(const InputEvent& e);
        void HandleMouseDown(const InputEvent& e);
        void HandleMouseUp(const InputEvent& e);
        void HandleMouseMove(const InputEvent& e);
        void HandleMouseWheel(const InputEvent& e);

        // Coordinate Conversion & Measurement
        Document::Cursor ScreenToDocumentPos(float screenX, float screenY) const;
        float GetColumnPixelOffset(unsigned int lineIdx, unsigned int column) const; // X offset from line start in pixels
        unsigned int GetColumnFromPixelOffset(unsigned int lineIdx, float targetX) const; // Column from X offset
        float GetLinePixelWidth(unsigned int lineIdx) const;

        // Scrolling & View
        void ScrollToCursor();
        void ClampScroll();
        void UpdateDesiredColumnXFromCursor(); // Sets mDesiredColumnX based on current cursor

        // Word Navigation Helpers
        bool IsWordChar(char32_t c) const;
        bool IsWhitespace(char32_t c) const; // Excludes newline
        Document::Cursor GetWordBoundaryLeft(Document::Cursor pos) const;
        Document::Cursor GetWordBoundaryRight(Document::Cursor pos) const;
    };
}