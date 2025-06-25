#include "DocumentView.h"
#include "Platform.h"
#include <vector>
#include <string>
#include <algorithm> 
#include <cmath>     
#include <locale>    
#include <codecvt>   
#include "Styles.h"

void ExternalCreateNewDocument();

// Minimal VK codes (typically from WinUser.h or similar)
// These are for demonstration; a real app would use platform headers.
#ifndef VK_DEFINES_DOCUMENTVIEW // Guard against multiple definitions if included elsewhere
#define VK_DEFINES_DOCUMENTVIEW
#define MK_LBUTTON          0x0001
#define MK_RBUTTON          0x0002
#define MK_SHIFT            0x0004
#define MK_CONTROL          0x0008
#define MK_MBUTTON          0x0010

#define VK_LBUTTON        0x01
#define VK_RBUTTON        0x02
#define VK_MBUTTON        0x04 
#define VK_BACK           0x08 
#define VK_TAB            0x09
#define VK_RETURN         0x0D 
#define VK_SHIFT          0x10
#define VK_CONTROL        0x11
#define VK_MENU           0x12 
#define VK_PAUSE          0x13
#define VK_CAPITAL        0x14 // CAPS LOCK
#define VK_ESCAPE         0x1B
#define VK_SPACE          0x20
#define VK_PRIOR          0x21 // PAGE UP
#define VK_NEXT           0x22 // PAGE DOWN
#define VK_END            0x23 
#define VK_HOME           0x24 
#define VK_LEFT           0x25 
#define VK_UP             0x26 
#define VK_RIGHT          0x27 
#define VK_DOWN           0x28 
#define VK_INSERT         0x2D 
#define VK_DELETE         0x2E 
#define VK_A              0x41
#define VK_C              0x43
#define VK_V              0x56
#define VK_X              0x58
#define VK_Y              0x59
#define VK_Z              0x5A
#define VK_S              0x53 
#define VK_N              0x4E  
#define VK_OEM_PLUS       0xBB // For Ctrl+= (Zoom In)
#define VK_OEM_MINUS      0xBD // For Ctrl+- (Zoom Out)
#endif

std::u32string Utf8ToUtf32(const char* utf8_string, unsigned int bytes);
std::wstring u32string_to_wstring(const std::u32string& u32str);
std::u32string wstring_to_u32string(const std::wstring& wstr);

namespace TextEdit {
    
    DocumentView::DocumentView(std::shared_ptr<Renderer> renderer, std::shared_ptr<Document> doc, std::shared_ptr<Font> font, std::shared_ptr<Font> small)
        : mRenderer(renderer), mDocument(doc), mFont(font), mSmallFont(small),
        mScrollX(0.0f), mScrollY(0.0f), mLineNumberWidth(30.0f),
        mViewX(0.0f), mViewY(0.0f), mViewWidth(0.0f), mViewHeight(0.0f),
        mTotalContentWidth(0.0f), mTotalContentHeight(0.0f),
        mDesiredColumnX(0.0f),
        mCursorBlinkTimer(0.0f), mShowCursor(true),
        mIsSelecting(false), mIsMouseDown(false),
        mSelectionDragStartDocPos(0, 0),
        mLastClickTime(0), mClickCount(0),
        mIsHighlighterDropdownOpen(false),
        mSelectedHighlighterIndex(1),
        mIsContextMenuOpen(false)
    {
        if (!mDocument) {
            mDocument = Document::Create(); // Ensure a document exists
        }

        mIsDraggingVertScrollbar = false;
        mIsDraggingHorzScrollbar = false;
        mDragScrollbarOffset = 0.0f;

        mLastClickMousePos = { 0, 0 };
        mLastMousePos = { 0,0 };

        // Initialize highlighter options
        mHighlighterOptions = {
            { U"TEXT", Highlighter::Text },
            { U"CODE", Highlighter::Code }
        };

        // Initialize context menu options
        mContextMenuOptions = {
            U"CUT",
            U"COPY",
            U"PASTE"
        };
        mContextMenuPos = { 0, 0 };

        // Ensure selected index matches document's initial highlighter
        if (mDocument->GetHighlighter() == Highlighter::Text) {
            mSelectedHighlighterIndex = 0;
        }

        float digitWidth = mFont->GetGlyph(U'0').advance; // Approximate width of a digit
        mLineNumberWidth = digitWidth * 4 + 10.0f; // 4 digits + 5px padding on each side
    }

    void DocumentView::PerformCut() {
        if (!mDocument) return;
        Document::Cursor currentPos = mDocument->GetCursor();
        if (!mDocument->HasSelection()) {
            // No selection: Cut the entire current line, including newline
            Document::Cursor lineStart(currentPos.line, 0);
            Document::Cursor lineEnd = lineStart;
            if (currentPos.line < mDocument->GetLineCount() - 1) {
                // Not the last line: Select up to start of next line
                lineEnd.line = currentPos.line + 1;
                lineEnd.column = 0;
            }
            else {
                // Last line: Select to end of line
                lineEnd.column = static_cast<unsigned int>(mDocument->GetLine(currentPos.line).text.length());
            }
            mDocument->SetSelection({ lineStart, lineEnd });
            std::u32string selectedText = mDocument->GetText(mDocument->GetSelection());
            // For last line without newline, append one to clipboard for consistency
            if (currentPos.line == mDocument->GetLineCount() - 1 && !selectedText.empty() && selectedText.back() != U'\n') {
                selectedText += U'\n';
            }
            PlatformWriteClipboardU16(u32string_to_wstring(selectedText));
            mDocument->Remove();
        }
        else {
            // Existing selection: Cut selected text
            std::u32string selectedText = mDocument->GetText(mDocument->GetSelection());
            PlatformWriteClipboardU16(u32string_to_wstring(selectedText));
            mDocument->Remove();
        }
        ScrollToCursor();
        UpdateDesiredColumnXFromCursor();
        mShowCursor = true;
        mCursorBlinkTimer = 0.0f;
    }

    void DocumentView::PerformCopy() {
        if (!mDocument) return;
        Document::Cursor currentPos = mDocument->GetCursor();
        if (!mDocument->HasSelection()) {
            // No selection: Copy the entire current line
            Document::Cursor lineStart(currentPos.line, 0);
            Document::Cursor lineEnd(currentPos.line, static_cast<unsigned int>(mDocument->GetLine(currentPos.line).text.length()));
            mDocument->SetSelection({ lineStart, lineEnd });
            std::u32string selectedText = mDocument->GetText(mDocument->GetSelection());
            PlatformWriteClipboardU16(u32string_to_wstring(selectedText));
            mDocument->PlaceCursor(currentPos); // Clear the selection after copying
        }
        else {
            // Existing selection: Copy selected text
            std::u32string selectedText = mDocument->GetText(mDocument->GetSelection());
            PlatformWriteClipboardU16(u32string_to_wstring(selectedText));
        }
    }

    void DocumentView::PerformPaste() {
        if (!mDocument) return;
        std::wstring clipboardTextW = PlatformReadClipboardU16();
        std::u32string clipboardTextU32 = wstring_to_u32string(clipboardTextW);
        if (!clipboardTextU32.empty()) {
            mDocument->Insert(clipboardTextU32);
            ScrollToCursor();
            UpdateDesiredColumnXFromCursor();
            mShowCursor = true;
            mCursorBlinkTimer = 0.0f;
        }
    }

    DocumentView::~DocumentView() {
    }

    unsigned int CountDigits(unsigned int n) {
        if (n == 0) return 1;
        unsigned int count = 0;
        while (n > 0) {
            n /= 10;
            count++;
        }
        return count;
    }

    void DocumentView::Update(float deltaTime) {
        mCursorBlinkTimer += deltaTime;
        if (mCursorBlinkTimer >= TextEdit::Styles::CURSOR_BLINK_RATE) {
            mShowCursor = !mShowCursor;
            mCursorBlinkTimer = 0.0f;
        }

        if (mIsMouseDown && mIsSelecting && mFont) { // Auto-scroll during selection drag
            float scrollSpeedPixels = TextEdit::Styles::AUTOSCROLL_SPEED_LINES_PER_SEC * mFont->GetLineHeight() * deltaTime;
            bool scrolled = false;

            float textDisplayWidth = mViewWidth - TextEdit::Styles::SCROLLBAR_SIZE - mLineNumberWidth; // Text area starts after line numbers
            float textDisplayHeight = mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE;

            if (mLastMousePos.y < mViewY + TextEdit::Styles::AUTOSCROLL_MARGIN) {
                mScrollY -= scrollSpeedPixels;
                scrolled = true;
            }
            else if (mLastMousePos.y > mViewY + textDisplayHeight - TextEdit::Styles::AUTOSCROLL_MARGIN) {
                mScrollY += scrollSpeedPixels;
                scrolled = true;
            }

            if (mLastMousePos.x < mViewX + mLineNumberWidth + TextEdit::Styles::AUTOSCROLL_MARGIN) {
                mScrollX -= scrollSpeedPixels * 2; // Horizontal scroll can be faster
                scrolled = true;
            }
            else if (mLastMousePos.x > mViewX + mLineNumberWidth + textDisplayWidth - TextEdit::Styles::AUTOSCROLL_MARGIN) {
                mScrollX += scrollSpeedPixels * 2;
                scrolled = true;
            }

            if (scrolled) {
                ClampScroll();
                Document::Cursor docPos = ScreenToDocumentPos(static_cast<float>(mLastMousePos.x), static_cast<float>(mLastMousePos.y));
                mDocument->MoveCursor(docPos);
                // mDesiredColumnX is updated by PlaceCursor/MoveCursor if they call UpdateDesiredColumnX
                UpdateDesiredColumnXFromCursor(); // Ensure it's updated
            }
        }

        if (mDocument->GetHighlighter() == Highlighter::Code) {
            mDocument->UpdateIncrementalHighlight();
        }

        unsigned int numDigits = CountDigits(mDocument->GetLineCount());
        float digitWidth = mFont->GetGlyph(U'0').advance; // Approximate width of a digit
        mLineNumberWidth = digitWidth * (numDigits < 3 ? 3 : numDigits) + 10.0f; // 4 digits + 5px padding on each side
    }

    void DocumentView::Display(float x, float y, float w, float h) {
        mViewX = x;
        mViewY = y;
        mViewWidth = w;
        mViewHeight = h;

        mRenderer->SetFont(mFont);
        const float textAreaStartX = mViewX + mLineNumberWidth + Styles::GUTTER_RIGHT_PAD;

        // Calculate content dimensions
        mTotalContentHeight = static_cast<float>(mDocument->GetLineCount()) * mFont->GetLineHeight();
        mTotalContentWidth = 0.0f;
        for (unsigned int i = 0; i < mDocument->GetLineCount(); ++i) {
            mTotalContentWidth = std::max(mTotalContentWidth, GetLinePixelWidth(i));
        }
        mTotalContentWidth += mFont->GetSpaceWidthPixels(); // Add some padding

        float textDisplayWidth = mViewWidth - TextEdit::Styles::SCROLLBAR_SIZE - mLineNumberWidth - Styles::GUTTER_RIGHT_PAD; // Adjust width for padding
        float textDisplayHeight = mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE;
        float horzScrollbarTrackWidth = mViewWidth - Styles::HIGHLIGHTER_BUTTON_WIDTH - mLineNumberWidth; // Scrollbar track shortened for dropdown and line numbers

        // --- Line Numbers Clipping and Rendering ---
        if (mLineNumberWidth > 0) {
            mRenderer->DrawRect(mViewX, mViewY, mLineNumberWidth, mViewHeight, Styles::GutterColor.r, Styles::GutterColor.g, Styles::GutterColor.b);
            mRenderer->SetClip(mViewX, mViewY, mLineNumberWidth, textDisplayHeight);

            float lineH = mFont->GetLineHeight(); // Use main font line height for consistency
            int firstVisibleLine = static_cast<int>(mScrollY / lineH);
            int lastVisibleLine = static_cast<int>((mScrollY + textDisplayHeight) / lineH) + 1;
            firstVisibleLine = std::max(0, firstVisibleLine);
            lastVisibleLine = std::min(static_cast<int>(mDocument->GetLineCount() - 1), lastVisibleLine);

            for (int lineIdx = firstVisibleLine; lineIdx <= lastVisibleLine; ++lineIdx) {
                float lineScreenY = mViewY + (static_cast<float>(lineIdx) * lineH) - mScrollY;
                // Convert line number to u32string (1-based for display)
                std::u32string lineNumStr;
                unsigned int displayLineNum = lineIdx + 1;
                do {
                    lineNumStr.insert(lineNumStr.begin(), U'0' + (displayLineNum % 10));
                    displayLineNum /= 10;
                } while (displayLineNum > 0);
                // Right-align by calculating width and offsetting
                float textWidth = 0.0f;
                for (char32_t c : lineNumStr) {
                    textWidth += mFont->GetGlyph(c).advance;
                }
                float textX = mViewX + mLineNumberWidth - textWidth - 5.0f; // 5px padding from right
                float textY = lineScreenY;
                mRenderer->DrawText(lineNumStr, textX, textY, Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
            }
            mRenderer->ClearClip();
        }

        // --- Main Text Area Clipping ---
        mRenderer->SetClip(textAreaStartX, mViewY, textDisplayWidth, textDisplayHeight);

        float lineH = mFont->GetLineHeight();
        float ascent = mFont->GetScaledAscent();

        int firstVisibleLine = static_cast<int>(mScrollY / lineH);
        int lastVisibleLine = static_cast<int>((mScrollY + textDisplayHeight) / lineH) + 1;
        firstVisibleLine = std::max(0, firstVisibleLine);
        lastVisibleLine = std::min(static_cast<int>(mDocument->GetLineCount() - 1), lastVisibleLine);

        // --- Render Selection ---
        if (mDocument->HasSelection()) {
            Document::Span selection = mDocument->GetSelection(); // Normalized
            for (unsigned int lineIdx = selection.start.line; lineIdx <= selection.end.line; ++lineIdx) {
                if (static_cast<int>(lineIdx) < firstVisibleLine || static_cast<int>(lineIdx) > lastVisibleLine) continue;

                const std::u32string& lineText = mDocument->GetLine(lineIdx).text;
                float lineScreenY = mViewY + (static_cast<float>(lineIdx) * lineH) - mScrollY;

                unsigned int selStartCol = (lineIdx == selection.start.line) ? selection.start.column : 0;
                unsigned int selEndCol = (lineIdx == selection.end.line) ? selection.end.column : static_cast<unsigned int>(lineText.length());

                if (selStartCol >= selEndCol && !(lineIdx == selection.start.line && lineIdx == selection.end.line && selStartCol == selEndCol)) { // Empty selection on this line unless it's a single point in a multi-line selection
                    if (lineIdx > selection.start.line && lineIdx < selection.end.line && lineText.empty()) { // Full empty line selected
                        // Draw full width selection for empty line
                    }
                    else if (lineIdx == selection.start.line && selStartCol == lineText.length() && selection.start.line != selection.end.line) {
                        // End of line selected, draw small indicator if needed or handle as part of newline
                    }
                    else {
                        continue;
                    }
                }

                float selStartX = textAreaStartX + GetColumnPixelOffset(lineIdx, selStartCol) - mScrollX;
                float selEndX = textAreaStartX + GetColumnPixelOffset(lineIdx, selEndCol) - mScrollX;

                // Clamp selection rect to visible text area
                float rectX = std::max(textAreaStartX, selStartX);
                float rectW = std::min(selEndX, textAreaStartX + textDisplayWidth) - rectX;

                if (rectW > 0) {
                    mRenderer->DrawRect(rectX, lineScreenY, rectW, lineH, Styles::SelectionColor.r, Styles::SelectionColor.g, Styles::SelectionColor.b);
                }
                else if (selStartCol == selEndCol && selection.start.line != selection.end.line) { // For multi-line selection, show selection for newline
                    if (lineIdx < selection.end.line || (lineIdx == selection.end.line && selection.end.column == 0)) {
                        float endOfLineWidth = GetLinePixelWidth(lineIdx);
                        float newlineSelX = textAreaStartX + endOfLineWidth - mScrollX;
                        // Draw a small box for the newline character selection part
                        mRenderer->DrawRect(newlineSelX, lineScreenY, mFont->GetSpaceWidthPixels() > 0 ? mFont->GetSpaceWidthPixels() : 5.0f, lineH, Styles::SelectionColor.r, Styles::SelectionColor.g, Styles::SelectionColor.b);
                    }
                }
            }
        }

        // --- Render Text ---
        for (int lineIdx = firstVisibleLine; lineIdx <= lastVisibleLine; ++lineIdx) {
            const Document::Line& lineObj = mDocument->GetLine(static_cast<unsigned int>(lineIdx));
            const std::u32string& lineText = lineObj.text;
            float lineScreenY_top = mViewY + (static_cast<float>(lineIdx) * lineH) - mScrollY;

            float lineStartX_world = 0.0f; // Text is drawn relative to this X in world space (before scroll)
            float lineStartX_screen = textAreaStartX + lineStartX_world - mScrollX;

            // This forces visible lines to re-tokenize. It's optional since the background tokenizer is always running!
            mDocument->TokenizeLine(lineIdx);

            if (mDocument->GetHighlighter() == Highlighter::Text || lineObj.tokens.size() == 0) {
                mRenderer->DrawText(lineText, lineStartX_screen, lineScreenY_top,
                    Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b,
                    lineStartX_screen);  // Pass line start for tab calculation
            }
            else {
                float x_pos_pen = lineStartX_screen;
                for (unsigned int i = 0, size = (unsigned int)lineObj.tokens.size(); i < size; ++i) {
                    TextEdit::TokenType tokenType = lineObj.tokens[i].first;
                    const Styles::Color& style = Styles::style_map.at(tokenType);

                    int start_in_string = lineObj.tokens[i].second;
                    int end_in_string = (int)lineText.size();
                    if (i + 1 < size) {
                        end_in_string = lineObj.tokens[i + 1].second;
                    }

                    // Pass lineStartX_screen as the line start position for correct tab calculation
                    x_pos_pen += mRenderer->DrawText(lineText, start_in_string, end_in_string,
                        x_pos_pen, lineScreenY_top,
                        style.r, style.g, style.b,
                        lineStartX_screen);  // IMPORTANT: Pass line start!
                }
            }
        }


        // --- Render Cursor ---
        if (mShowCursor && !mDocument->HasSelection()) {
            Document::Cursor cursor = mDocument->GetCursor();
            if (static_cast<int>(cursor.line) >= firstVisibleLine && static_cast<int>(cursor.line) <= lastVisibleLine) {
                float cursorX_world = GetColumnPixelOffset(cursor.line, cursor.column);
                float cursorScreenX = textAreaStartX + cursorX_world - mScrollX;
                float cursorScreenY = mViewY + (static_cast<float>(cursor.line) * lineH) - mScrollY;

                // Ensure cursor is within the clipped text area, or at least at the edge
                cursorScreenX = std::max(textAreaStartX, std::min(cursorScreenX, textAreaStartX + textDisplayWidth - 1.0f));

                mRenderer->DrawRect(cursorScreenX, cursorScreenY, 1.0f, lineH, Styles::CursorColor.r, Styles::CursorColor.g, Styles::CursorColor.b);
            }
        }

        mRenderer->ClearClip(); // Reset clip for scrollbars and dropdown

        // --- Render Scrollbars ---
        { // V scroll bar
            float trackX = mViewX + mViewWidth - TextEdit::Styles::SCROLLBAR_SIZE;
            float trackY = mViewY;
            float trackH = mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE;
            mRenderer->DrawRect(trackX, trackY, TextEdit::Styles::SCROLLBAR_SIZE, trackH, Styles::ScrollbarTrackColor.r, Styles::ScrollbarTrackColor.g, Styles::ScrollbarTrackColor.b);

            // Calculate nib size as proportion of visible content
            float visibleRatio = textDisplayHeight / mTotalContentHeight;
            float nibH = trackH * visibleRatio;
            nibH = std::max(TextEdit::Styles::SCROLLBAR_SIZE, std::min(nibH, trackH)); // Ensure nib is at least SCROLLBAR_SIZE but not larger than track
            float maxScrollY = mTotalContentHeight - textDisplayHeight;
            float nibY = trackY + (mScrollY / maxScrollY) * (trackH - nibH);
            nibY = std::min(nibY, trackY + trackH - nibH); // Clamp nib

            mVertScrollbarRect = { trackX, nibY, TextEdit::Styles::SCROLLBAR_SIZE, nibH };
            bool hoverVert = mVertScrollbarRect.Contains(static_cast<float>(mLastMousePos.x), static_cast<float>(mLastMousePos.y));
            const auto& nibColor = (hoverVert || mIsDraggingVertScrollbar) ? Styles::ScrollbarNibHoverColor : Styles::ScrollbarNibColor;
            mRenderer->DrawRect(trackX, nibY, TextEdit::Styles::SCROLLBAR_SIZE, nibH, nibColor.r, nibColor.g, nibColor.b);
        }

        { // H scroll bar
            float trackX = mViewX + mLineNumberWidth;
            float trackY = mViewY + mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE;
            mRenderer->DrawRect(trackX, trackY, horzScrollbarTrackWidth, TextEdit::Styles::SCROLLBAR_SIZE, Styles::ScrollbarTrackColor.r, Styles::ScrollbarTrackColor.g, Styles::ScrollbarTrackColor.b);

            // Calculate nib size as proportion of visible content
            float visibleRatio = textDisplayWidth / mTotalContentWidth;
            float nibW = horzScrollbarTrackWidth * visibleRatio;
            nibW = std::max(TextEdit::Styles::SCROLLBAR_SIZE, std::min(nibW, horzScrollbarTrackWidth)); // Ensure nib is at least SCROLLBAR_SIZE but not larger than track
            float maxScrollX = mTotalContentWidth - textDisplayWidth;
            float nibX = trackX + (mScrollX / maxScrollX) * (horzScrollbarTrackWidth - nibW);
            nibX = std::min(nibX, trackX + horzScrollbarTrackWidth - nibW); // Clamp nib

            mHorzScrollbarRect = { nibX, trackY, nibW, TextEdit::Styles::SCROLLBAR_SIZE };
            bool hoverHorz = mHorzScrollbarRect.Contains(static_cast<float>(mLastMousePos.x), static_cast<float>(mLastMousePos.y));
            const auto& nibColor = (hoverHorz || mIsDraggingHorzScrollbar) ? Styles::ScrollbarNibHoverColor : Styles::ScrollbarNibColor;
            mRenderer->DrawRect(nibX, trackY, nibW, TextEdit::Styles::SCROLLBAR_SIZE, nibColor.r, nibColor.g, nibColor.b);
        }

        // --- Render Undo/Redo Buttons ---
        {
            mRenderer->SetFont(mSmallFont);
            float buttonY = mViewY + mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE;
            float buttonHeight = TextEdit::Styles::SCROLLBAR_SIZE;
            float buttonWidth = mLineNumberWidth / 2.0f;

            // --- Undo Button ---
            mUndoButtonRect = { mViewX, buttonY, buttonWidth, buttonHeight };
            bool canUndo = mDocument->CanUndo();
            bool hoverUndo = mUndoButtonRect.Contains(static_cast<float>(mLastMousePos.x), static_cast<float>(mLastMousePos.y));
            Styles::Color undoButtonColor = canUndo ? (hoverUndo ? Styles::ScrollbarNibHoverColor : Styles::HighlighterButtonColor) : Styles::GutterColor;
            mRenderer->DrawRect(mUndoButtonRect.x, mUndoButtonRect.y, mUndoButtonRect.width, mUndoButtonRect.height, undoButtonColor.r, undoButtonColor.g, undoButtonColor.b);

            const std::u32string undoIcon = U"👈";
            float undoIconWidth = mSmallFont->GetGlyph(U'👈').advance;
            float undoIconX = mUndoButtonRect.x + (mUndoButtonRect.width - undoIconWidth) / 2.0f;
            float undoIconY = mUndoButtonRect.y + (mUndoButtonRect.height - mSmallFont->GetLineHeight()) / 2.0f;
            Styles::Color undoTextColor = canUndo ? Styles::TextColor : Styles::TokenTypeComment; // Use comment color for disabled text
            mRenderer->DrawText(undoIcon, undoIconX, undoIconY, undoTextColor.r, undoTextColor.g, undoTextColor.b);

            // --- Redo Button ---
            mRedoButtonRect = { mViewX + buttonWidth, buttonY, buttonWidth, buttonHeight };
            bool canRedo = mDocument->CanRedo();
            bool hoverRedo = mRedoButtonRect.Contains(static_cast<float>(mLastMousePos.x), static_cast<float>(mLastMousePos.y));
            Styles::Color redoButtonColor = canRedo ? (hoverRedo ? Styles::ScrollbarNibHoverColor : Styles::HighlighterButtonColor) : Styles::GutterColor;
            mRenderer->DrawRect(mRedoButtonRect.x, mRedoButtonRect.y, mRedoButtonRect.width, mRedoButtonRect.height, redoButtonColor.r, redoButtonColor.g, redoButtonColor.b);

            const std::u32string redoIcon = U"👉";
            float redoIconWidth = mSmallFont->GetGlyph(U'👉').advance;
            float redoIconX = mRedoButtonRect.x + (mRedoButtonRect.width - redoIconWidth) / 2.0f;
            float redoIconY = mRedoButtonRect.y + (mRedoButtonRect.height - mSmallFont->GetLineHeight()) / 2.0f;
            Styles::Color redoTextColor = canRedo ? Styles::TextColor : Styles::TokenTypeComment;
            mRenderer->DrawText(redoIcon, redoIconX, redoIconY, redoTextColor.r, redoTextColor.g, redoTextColor.b);
        }

        mRenderer->SetFont(mSmallFont);

        // --- Render Highlighter Dropdown Button ---
        {
            float buttonX = mViewX + mViewWidth - Styles::HIGHLIGHTER_BUTTON_WIDTH;
            float buttonY = mViewY + mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE;
            mHighlighterButtonRect = { buttonX, buttonY, Styles::HIGHLIGHTER_BUTTON_WIDTH, TextEdit::Styles::SCROLLBAR_SIZE };
            bool hoverButton = mHighlighterButtonRect.Contains(static_cast<float>(mLastMousePos.x), static_cast<float>(mLastMousePos.y));
            Styles::Color buttonColor = hoverButton ? Styles::ScrollbarNibHoverColor : Styles::HighlighterButtonColor;
            mRenderer->DrawRect(buttonX, buttonY, Styles::HIGHLIGHTER_BUTTON_WIDTH, TextEdit::Styles::SCROLLBAR_SIZE, buttonColor.r, buttonColor.g, buttonColor.b);

            // Draw current highlighter name
            std::u32string buttonText = mHighlighterOptions[mSelectedHighlighterIndex].first;
            float textX = buttonX + 7.0f; // Padding
            float textY = buttonY + (TextEdit::Styles::SCROLLBAR_SIZE - mSmallFont->GetLineHeight()) / 2.0f;
            mRenderer->DrawText(buttonText, textX, textY, Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
        }

        // --- Render Highlighter Dropdown Menu (if open) ---
        if (mIsHighlighterDropdownOpen) {
            float menuWidth = Styles::HIGHLIGHTER_BUTTON_WIDTH;
            float optionHeight = mSmallFont->GetLineHeight() + 4.0f; // Padding
            float menuHeight = optionHeight * mHighlighterOptions.size();
            float menuX = mViewX + mViewWidth - menuWidth;
            float menuY = mViewY + mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE - menuHeight; // Render upward

            // Draw menu background
            mRenderer->DrawRect(menuX, menuY, menuWidth, menuHeight, Styles::ScrollbarTrackColor.r, Styles::ScrollbarTrackColor.g, Styles::ScrollbarTrackColor.b);

            // Draw each option
            for (size_t i = 0; i < mHighlighterOptions.size(); ++i) {
                float optionY = menuY + i * optionHeight;
                bool isSelected = static_cast<int>(i) == mSelectedHighlighterIndex;
                bool isHovered = Rect{ menuX, optionY, menuWidth, optionHeight }.Contains(static_cast<float>(mLastMousePos.x), static_cast<float>(mLastMousePos.y));
                if (isHovered) {
                    mRenderer->DrawRect(menuX, optionY, menuWidth, optionHeight, Styles::ScrollbarNibHoverColor.r, Styles::ScrollbarNibHoverColor.g, Styles::ScrollbarNibHoverColor.b);
                }
                if (isSelected) {
                    mRenderer->DrawRect(menuX, optionY, menuWidth, optionHeight, Styles::SelectionColor.r, Styles::SelectionColor.g, Styles::SelectionColor.b);
                }
                float textX = menuX + 5.0f;
                float textY = optionY + 2.0f;
                mRenderer->DrawText(mHighlighterOptions[i].first, textX, textY, Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
            }
        }

        // --- Render Context Menu (if open) ---
        if (mIsContextMenuOpen) {
            float menuWidth = Styles::CONTEXT_MENU_WIDTH;
            float optionHeight = mSmallFont->GetLineHeight() + 4.0f; // Same padding as highlighter menu
            float menuHeight = optionHeight * mContextMenuOptions.size();
            float menuX = static_cast<float>(mContextMenuPos.x);
            float menuY = static_cast<float>(mContextMenuPos.y);

            // Adjust menu position to fit within view
            if (menuX + menuWidth > mViewX + mViewWidth) {
                menuX = mViewX + mViewWidth - menuWidth;
            }
            if (menuY + menuHeight > mViewY + mViewHeight) {
                menuY = mViewY + mViewHeight - menuHeight;
            }
            if (menuX < mViewX) menuX = mViewX;
            if (menuY < mViewY) menuY = mViewY;

            mContextMenuRect = { menuX, menuY, menuWidth, menuHeight };

            // Draw menu background
            mRenderer->DrawRect(menuX, menuY, menuWidth, menuHeight, Styles::ScrollbarTrackColor.r, Styles::ScrollbarTrackColor.g, Styles::ScrollbarTrackColor.b);

            // Draw each option
            for (size_t i = 0; i < mContextMenuOptions.size(); ++i) {
                float optionY = menuY + i * optionHeight;
                bool isHovered = Rect{ menuX, optionY, menuWidth, optionHeight }.Contains(static_cast<float>(mLastMousePos.x), static_cast<float>(mLastMousePos.y));
                if (isHovered) {
                    mRenderer->DrawRect(menuX, optionY, menuWidth, optionHeight, Styles::ScrollbarNibHoverColor.r, Styles::ScrollbarNibHoverColor.g, Styles::ScrollbarNibHoverColor.b);
                }
                float textX = menuX + 5.0f;
                float textY = optionY + 2.0f;
                mRenderer->DrawText(mContextMenuOptions[i], textX, textY, Styles::TextColor.r, Styles::TextColor.g, Styles::TextColor.b);
            }
        }

    }

    void DocumentView::CloseMenus() {
        mIsHighlighterDropdownOpen = false;
        mIsContextMenuOpen = false;
    }

    void DocumentView::OnInput(const InputEvent& e) {
        if (!mDocument || !mFont || !mFont->IsValid()) return;

        mLastMousePos = { e.mouse.x, e.mouse.y }; // Store for hover detection, even if not MOUSE_MOVE

        switch (e.type) {
        case InputEvent::Type::KEY_DOWN:
            HandleKeyboardInput(e);
            break;
        case InputEvent::Type::MOUSE_DOWN:
            HandleMouseDown(e);
            break;
        case InputEvent::Type::MOUSE_UP:
            HandleMouseUp(e);
            break;
        case InputEvent::Type::MOUSE_MOVE:
            HandleMouseMove(e);
            break;
        case InputEvent::Type::MOUSE_WHEEL:
            HandleMouseWheel(e);
            break;
        default:
            break;
        }
    }

    // --- Input Handling Implementations ---
    void DocumentView::HandleKeyboardInput(const InputEvent& e) {
        if (!mDocument || !mFont || !mFont->IsValid()) return;

        Document::Cursor currentPos = mDocument->GetCursor();
        bool shift = e.key.shift;
        bool ctrl = e.key.ctrl;
        bool alt = e.key.alt; // Alt not used much in basic text editing, but available

        // Helper lambda to place/move cursor
        auto UpdateDocCursor = [&](const Document::Cursor& newPos) {
            if (shift) {
                mDocument->MoveCursor(newPos);
            }
            else {
                mDocument->PlaceCursor(newPos);
            }
            ScrollToCursor();
            UpdateDesiredColumnXFromCursor();
            mShowCursor = true;
            mCursorBlinkTimer = 0.0f;
            };

        // Character input
        if (!ctrl && !alt && e.key.unicode >= 32 && e.key.unicode != 127 /*DEL*/) {
            // Allow specific control chars if needed, e.g. tab is handled below
            if (e.key.unicode == U'\t' && e.key.keyCode != VK_TAB) { // Check if it's actual tab char not from VK_TAB
                // if so, treat as char input
            }
            else if (e.key.keyCode == VK_TAB) { // Handle VK_TAB separately
                // (Handled below)
            }
            else {
                std::u32string s;
                s += e.key.unicode;
                mDocument->Insert(s);
                ScrollToCursor();
                UpdateDesiredColumnXFromCursor();
                mShowCursor = true;
                mCursorBlinkTimer = 0.0f;
                return;
            }
        }

        switch (e.key.keyCode) {
        case VK_LEFT:
        {
            Document::Cursor newPos = currentPos;
            if (ctrl) {
                newPos = GetWordBoundaryLeft(currentPos);
            }
            else {
                if (newPos.column > 0) newPos.column--;
                else if (newPos.line > 0) {
                    newPos.line--;
                    newPos.column = static_cast<unsigned int>(mDocument->GetLine(newPos.line).text.length());
                }
            }
            UpdateDocCursor(newPos);
            break;
        }
        case VK_RIGHT:
        {
            Document::Cursor newPos = currentPos;
            if (ctrl) {
                newPos = GetWordBoundaryRight(currentPos);
            }
            else {
                const std::u32string& line = mDocument->GetLine(newPos.line).text;
                if (newPos.column < line.length()) newPos.column++;
                else if (newPos.line < mDocument->GetLineCount() - 1) {
                    newPos.line++;
                    newPos.column = 0;
                }
            }
            UpdateDocCursor(newPos);
            break;
        }
        case VK_UP:
        {
            Document::Cursor newPos = currentPos;
            int numLinesToMove = ctrl ? 5 : 1;
            if (static_cast<int>(newPos.line) - numLinesToMove >= 0) {
                newPos.line -= numLinesToMove;
                newPos.column = GetColumnFromPixelOffset(newPos.line, mDesiredColumnX);
            }
            else { // Go to beginning of document
                newPos = Document::Cursor(0, 0);
            }
            UpdateDocCursor(newPos);
            // Note: UpdateDesiredColumnX is not called here, mDesiredColumnX is used.
            break;
        }
        case VK_DOWN:
        {
            Document::Cursor newPos = currentPos;
            int numLinesToMove = ctrl ? 5 : 1;
            if (newPos.line + numLinesToMove < mDocument->GetLineCount()) {
                newPos.line += numLinesToMove;
                newPos.column = GetColumnFromPixelOffset(newPos.line, mDesiredColumnX);
            }
            else { // Go to end of document
                newPos.line = mDocument->GetLineCount() - 1;
                newPos.column = static_cast<unsigned int>(mDocument->GetLine(newPos.line).text.length());
            }
            UpdateDocCursor(newPos);
            // Note: UpdateDesiredColumnX is not called here, mDesiredColumnX is used.
            break;
        }
        case VK_HOME:
        {
            Document::Cursor newPos = currentPos;
            if (ctrl) newPos = Document::Cursor(0, 0); // Ctrl+Home: Doc start
            else newPos.column = 0; // Home: Line start
            UpdateDocCursor(newPos);
            break;
        }
        case VK_END:
        {
            Document::Cursor newPos = currentPos;
            if (ctrl) { // Ctrl+End: Doc end
                newPos.line = mDocument->GetLineCount() - 1;
                newPos.column = static_cast<unsigned int>(mDocument->GetLine(newPos.line).text.length());
            }
            else { // End: Line end
                newPos.column = static_cast<unsigned int>(mDocument->GetLine(newPos.line).text.length());
            }
            UpdateDocCursor(newPos);
            break;
        }
        case VK_BACK:
        { // Backspace
            if (!mDocument->HasSelection()) {
                Document::Cursor startDeletePos = currentPos;
                if (startDeletePos.column > 0) {
                    startDeletePos.column--;
                }
                else if (startDeletePos.line > 0) {
                    startDeletePos.line--;
                    startDeletePos.column = static_cast<unsigned int>(mDocument->GetLine(startDeletePos.line).text.length());
                }
                else {
                    break; // At start of document
                }
                mDocument->SetSelection({ startDeletePos, currentPos });
            }
            mDocument->Remove(); // Remove will place cursor at selection start
            ScrollToCursor();
            UpdateDesiredColumnXFromCursor();
            mShowCursor = true;
            mCursorBlinkTimer = 0.0f;
            break;
        }
        case VK_DELETE:
        {
            if (!mDocument->HasSelection()) {
                Document::Cursor endDeletePos = currentPos;
                const auto& line = mDocument->GetLine(currentPos.line);
                if (currentPos.column < line.text.length()) {
                    endDeletePos.column++;
                }
                else if (currentPos.line < mDocument->GetLineCount() - 1) {
                    endDeletePos.line++;
                    endDeletePos.column = 0;
                }
                else {
                    break; // At end of document
                }
                mDocument->SetSelection({ currentPos, endDeletePos });
            }
            mDocument->Remove();
            ScrollToCursor();
            UpdateDesiredColumnXFromCursor();
            mShowCursor = true;
            mCursorBlinkTimer = 0.0f;
            break;
        }
        case VK_RETURN:
        { // Enter
            mDocument->Insert(U"\n");
            ScrollToCursor();
            UpdateDesiredColumnXFromCursor();
            mShowCursor = true;
            mCursorBlinkTimer = 0.0f;
            break;
        }
        case VK_TAB:
        {
            if (shift) {
                // Shift+Tab: Un-indent (remove tab or equivalent spaces)
                if (mDocument->HasSelection()) {
                    // Handle multi-line selection
                    Document::Span selection = mDocument->GetSelection(); // Normalized
                    unsigned int tabSpaces = mFont->GetTabNumSpaces();
                    Document::Cursor newCursorPos = selection.start;

                    for (unsigned int lineIdx = selection.start.line; lineIdx <= selection.end.line; ++lineIdx) {
                        const std::u32string& lineText = mDocument->GetLine(lineIdx).text;
                        unsigned int charsToRemove = 0;

                        // Check if line starts with a tab
                        if (!lineText.empty() && lineText[0] == U'\t') {
                            charsToRemove = 1;
                        }
                        else {
                            // Check for spaces up to tabSpaces
                            for (unsigned int i = 0; i < std::min(static_cast<unsigned int>(lineText.length()), tabSpaces); ++i) {
                                if (lineText[i] == U' ') {
                                    charsToRemove++;
                                }
                                else {
                                    break;
                                }
                            }
                        }

                        if (charsToRemove > 0) {
                            // Set selection to remove the tab or spaces
                            mDocument->SetSelection({ {lineIdx, 0}, {lineIdx, charsToRemove} });
                            mDocument->Remove();
                            // Adjust cursor if it was on this line
                            if (lineIdx == selection.start.line && selection.start.column > 0) {
                                newCursorPos.column = std::max(0u, newCursorPos.column - charsToRemove);
                            }
                        }
                    }

                    // Restore cursor to start of selection
                    mDocument->PlaceCursor(newCursorPos);
                }
                else {
                    // No selection: Check for tab or spaces to the left
                    Document::Cursor newPos = currentPos;
                    const std::u32string& lineText = mDocument->GetLine(currentPos.line).text;
                    unsigned int tabSpaces = mFont->GetTabNumSpaces();

                    if (currentPos.column > 0) {
                        // Check for tab or spaces to the left
                        unsigned int charsToRemove = 0;
                        if (lineText[currentPos.column - 1] == U'\t') {
                            charsToRemove = 1;
                        }
                        else if (lineText[currentPos.column - 1] == U' ') {
                            // Count spaces up to tabSpaces
                            unsigned int startCol = currentPos.column;
                            for (unsigned int i = startCol; i > 0 && charsToRemove < tabSpaces; --i) {
                                if (lineText[i - 1] == U' ') {
                                    charsToRemove++;
                                }
                                else {
                                    break;
                                }
                            }
                        }

                        if (charsToRemove > 0) {
                            mDocument->SetSelection({ {currentPos.line, currentPos.column - charsToRemove}, currentPos });
                            mDocument->Remove();
                            newPos.column -= charsToRemove;
                        }
                    }
                    else {
                        // At start of line: Remove tab or spaces if present
                        unsigned int charsToRemove = 0;
                        if (!lineText.empty() && lineText[0] == U'\t') {
                            charsToRemove = 1;
                        }
                        else {
                            // Check for spaces up to tabSpaces
                            for (unsigned int i = 0; i < std::min(static_cast<unsigned int>(lineText.length()), tabSpaces); ++i) {
                                if (lineText[i] == U' ') {
                                    charsToRemove++;
                                }
                                else {
                                    break;
                                }
                            }
                        }

                        if (charsToRemove > 0) {
                            mDocument->SetSelection({ {currentPos.line, 0}, {currentPos.line, charsToRemove} });
                            mDocument->Remove();
                        }
                    }

                    UpdateDocCursor(newPos);
                }
            }
            else {
                // Tab: Insert a tab character
                mDocument->Insert(U"\t");
                ScrollToCursor();
                UpdateDesiredColumnXFromCursor();
                mShowCursor = true;
                mCursorBlinkTimer = 0.0f;
            }
            break;
        }
        case VK_A:
        { // Ctrl+A (Select All)
            if (ctrl) {
                Document::Cursor docStart(0, 0);
                Document::Cursor docEnd(mDocument->GetLineCount() - 1,
                    static_cast<unsigned int>(mDocument->GetLine(mDocument->GetLineCount() - 1).text.length()));
                mDocument->SetSelection({ docStart, docEnd });
                // No need to scroll or update desired column for select all typically
                mShowCursor = true;
                mCursorBlinkTimer = 0.0f;
            }
            break;
        }
        case VK_C:
        { // Ctrl+C (Copy)
            if (ctrl) {
                PerformCopy();
            }
            break;
        }
        case VK_X:
        { // Ctrl+X (Cut)
            if (ctrl) {
                PerformCut();
            }
            break;
        }
        case VK_V:
        { // Ctrl+V (Paste)
            if (ctrl) {
                PerformPaste();
            }
            break;
        }
        case VK_Z: 
            if (ctrl) {
                mDocument->Undo();
                // Invalidate view/cursor state
                mShowCursor = true;
                mCursorBlinkTimer = 0.0f;
                ScrollToCursor();
                UpdateDesiredColumnXFromCursor();
            }
            break;
        case VK_S:
            if (ctrl) {
                mDocument->Save();
            }
            break;
        case VK_N:
            if (ctrl) {
                ExternalCreateNewDocument();
            }
            break;
        case VK_Y: 
            if (ctrl) {
                mDocument->Redo();
                // Invalidate view/cursor state
                mShowCursor = true;
                mCursorBlinkTimer = 0.0f;
                ScrollToCursor();
                UpdateDesiredColumnXFromCursor();
            }
            break;
        default:
            // Other keys not handled.
            break;
        }
    }

    void DocumentView::HandleMouseDown(const InputEvent& e) {
        mIsMouseDown = true;

        // Handle context menu click FIRST, before any other processing
        if (mIsContextMenuOpen && e.mouse.button == VK_LBUTTON) {
            float optionHeight = mSmallFont->GetLineHeight() + 4.0f;
            float menuHeight = optionHeight * mContextMenuOptions.size();
            float menuX = static_cast<float>(mContextMenuPos.x);
            float menuY = static_cast<float>(mContextMenuPos.y);

            // Adjust menu position to fit within view (same logic as in Display)
            if (menuX + Styles::CONTEXT_MENU_WIDTH > mViewX + mViewWidth) {
                menuX = mViewX + mViewWidth - Styles::CONTEXT_MENU_WIDTH;
            }
            if (menuY + menuHeight > mViewY + mViewHeight) {
                menuY = mViewY + mViewHeight - menuHeight;
            }
            if (menuX < mViewX) menuX = mViewX;
            if (menuY < mViewY) menuY = mViewY;

            Rect menuRect = { menuX, menuY, Styles::CONTEXT_MENU_WIDTH, menuHeight };

            if (menuRect.Contains(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y))) {
                float relativeY = static_cast<float>(e.mouse.y) - menuY;
                size_t optionIndex = static_cast<size_t>(relativeY / optionHeight);

                if (optionIndex < mContextMenuOptions.size()) {
                    // Perform the action
                    switch (optionIndex) {
                    case 0: // Cut
                        PerformCut();
                        break;
                    case 1: // Copy
                        PerformCopy();
                        break;
                    case 2: // Paste
                        PerformPaste();
                        break;
                    }
                    mIsContextMenuOpen = false;
                }
                return; // Important: return here to prevent further processing
            }
            else {
                // Clicked outside menu, close it
                mIsContextMenuOpen = false;
                // Don't return here - process the click normally
            }
        }

        Document::Cursor docPos = ScreenToDocumentPos(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y));

        // Handle right-click for context menu
        if (e.mouse.button == VK_RBUTTON) {
            // Close any open menus
            mIsHighlighterDropdownOpen = false;
            // Open context menu at click position
            mIsContextMenuOpen = true;
            mContextMenuPos = { e.mouse.x, e.mouse.y };

            // Place cursor at click position unless clicking within a selection
            if (mDocument->HasSelection()) {
                Document::Span selection = mDocument->GetSelection();
                // Check if the click is within the selection
                if (docPos >= selection.start && docPos <= selection.end) {
                    // Click is within selection, keep selection
                    mShowCursor = true;
                    mCursorBlinkTimer = 0.0f;
                    return;
                }
            }

            // Click outside selection or no selection, place cursor
            mDocument->PlaceCursor(docPos);
            UpdateDesiredColumnXFromCursor();
            ScrollToCursor();
            mShowCursor = true;
            mCursorBlinkTimer = 0.0f;
            return;
        }

        // --- NEW: Handle Undo/Redo Button Clicks ---
        if (mUndoButtonRect.Contains(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y))) {
            if (mDocument->CanUndo()) {
                mDocument->Undo();
                ScrollToCursor();
                UpdateDesiredColumnXFromCursor();
            }
            return; // Absorb click
        }
        if (mRedoButtonRect.Contains(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y))) {
            if (mDocument->CanRedo()) {
                mDocument->Redo();
                ScrollToCursor();
                UpdateDesiredColumnXFromCursor();
            }
            return; // Absorb click
        }

        // Click detection for double/triple click
        const float MULTI_CLICK_TIME = 0.25f; // Time threshold for multi-click (in seconds)
        bool isDoubleClick = false;
        bool isTripleClick = false;
        if ((e.time - mLastClickTime) / 1000.0f <= MULTI_CLICK_TIME &&
            std::abs(e.mouse.x - mLastClickMousePos.x) < 5 &&
            std::abs(e.mouse.y - mLastClickMousePos.y) < 5 &&
            e.mouse.button == VK_LBUTTON) {
            mClickCount++;
            if (mClickCount == 2) {
                isDoubleClick = true;
            }
            else if (mClickCount == 3) {
                isTripleClick = true;
                mClickCount = 0; // Reset after triple-click
            }
        }
        else {
            mClickCount = 1;
        }
        mLastClickTime = e.time;
        mLastClickMousePos = { e.mouse.x, e.mouse.y };

        // Handle highlighter dropdown button click
        if (mHighlighterButtonRect.width > 0 && mHighlighterButtonRect.Contains(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y))) {
            mIsHighlighterDropdownOpen = !mIsHighlighterDropdownOpen;
            mIsContextMenuOpen = false; // Close context menu if open
            return;
        }

        // Handle highlighter dropdown menu click
        if (mIsHighlighterDropdownOpen) {
            float menuWidth = Styles::HIGHLIGHTER_BUTTON_WIDTH;
            float optionHeight = mSmallFont->GetLineHeight() + 4.0f;
            float menuHeight = optionHeight * mHighlighterOptions.size();
            float menuX = mViewX + mViewWidth - menuWidth;
            float menuY = mViewY + mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE - menuHeight;
            Rect menuRect = { menuX, menuY, menuWidth, menuHeight };
            if (menuRect.Contains(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y))) {
                float relativeY = static_cast<float>(e.mouse.y) - menuY;
                size_t optionIndex = static_cast<size_t>(relativeY / optionHeight);
                if (optionIndex < mHighlighterOptions.size()) {
                    mSelectedHighlighterIndex = static_cast<int>(optionIndex);
                    mDocument->SetHighlighter(mHighlighterOptions[optionIndex].second);
                    mIsHighlighterDropdownOpen = false;
                }
                return;
            }
            else {
                mIsHighlighterDropdownOpen = false; // Click outside menu closes it
                return;
            }
        }

        if (mVertScrollbarRect.width > 0 && mVertScrollbarRect.Contains(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y))) {
            mIsDraggingVertScrollbar = true;
            mDragScrollbarOffset = static_cast<float>(e.mouse.y) - mVertScrollbarRect.y; // Offset from top of nib
            return;
        }
        if (mHorzScrollbarRect.width > 0 && mHorzScrollbarRect.Contains(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y))) {
            mIsDraggingHorzScrollbar = true;
            mDragScrollbarOffset = static_cast<float>(e.mouse.x) - mHorzScrollbarRect.x; // Offset from left of nib
            return;
        }

        // Check for clicks on scrollbar tracks (outside the nib)
        float textDisplayWidth = mViewWidth - TextEdit::Styles::SCROLLBAR_SIZE - mLineNumberWidth; // Text area starts after line numbers
        float textDisplayHeight = mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE;
        float horzScrollbarTrackWidth = mViewWidth - Styles::HIGHLIGHTER_BUTTON_WIDTH - mLineNumberWidth;

        if (mTotalContentHeight > mViewHeight) { // Vertical scrollbar exists
            Rect vTrackRect = { mViewX + mLineNumberWidth + textDisplayWidth, mViewY, TextEdit::Styles::SCROLLBAR_SIZE, textDisplayHeight };
            if (vTrackRect.Contains(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y)) && !mVertScrollbarRect.Contains(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y))) {
                float clickRatio = (static_cast<float>(e.mouse.y) - vTrackRect.y) / vTrackRect.height;
                mScrollY = clickRatio * (mTotalContentHeight - textDisplayHeight); // Center view on click
                ClampScroll();
                return;
            }
        }
        if (mTotalContentWidth > mViewWidth) { // Horizontal scrollbar exists
            Rect hTrackRect = { mViewX + mLineNumberWidth, mViewY + textDisplayHeight, horzScrollbarTrackWidth, TextEdit::Styles::SCROLLBAR_SIZE };
            if (hTrackRect.Contains(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y)) && !mHorzScrollbarRect.Contains(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y))) {
                float clickRatio = (static_cast<float>(e.mouse.x) - hTrackRect.x) / hTrackRect.width;
                mScrollX = clickRatio * (mTotalContentWidth - textDisplayWidth);
                ClampScroll();
                return;
            }
        }

        if (isTripleClick) {
            // Select the entire line
            Document::Cursor lineStart(docPos.line, 0);
            Document::Cursor lineEnd(docPos.line, static_cast<unsigned int>(mDocument->GetLine(docPos.line).text.length()));
            mDocument->SetSelection({ lineStart, lineEnd });
            mIsSelecting = false; // Prevent further drag selection after triple-click
        }
        else if (isDoubleClick) {
            // Select the word under the cursor
            Document::Cursor wordStart = GetWordBoundaryLeft(docPos);
            Document::Cursor wordEnd = GetWordBoundaryRight(docPos);
            mDocument->SetSelection({ wordStart, wordEnd });
            mIsSelecting = false; // Prevent further drag selection after double-click
        }
        else if (e.mouse.shift) {
            mDocument->MoveCursor(docPos);
            mIsSelecting = true;
        }
        else {
            mDocument->PlaceCursor(docPos);
            mIsSelecting = true; // Start selection on single click
        }
        mSelectionDragStartDocPos = docPos;

        UpdateDesiredColumnXFromCursor();
        ScrollToCursor();
        mShowCursor = true;
        mCursorBlinkTimer = 0.0f;
    }

    void DocumentView::ResetView() {
        mScrollX = 0;
        mScrollY = 0;
    }

    void DocumentView::HandleMouseUp(const InputEvent& e) {
        mIsMouseDown = false;
        mIsSelecting = false;
        mIsDraggingVertScrollbar = false;
        mIsDraggingHorzScrollbar = false;
    }

    void DocumentView::HandleMouseMove(const InputEvent& e) {
        mLastMousePos = { e.mouse.x, e.mouse.y }; // Update for auto-scroll and hover effects

        if (mIsDraggingVertScrollbar) {
            float trackY = mViewY;
            float trackH = mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE;
            float textDisplayHeight = mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE;
            float visibleRatio = textDisplayHeight / mTotalContentHeight;
            float nibH = trackH * visibleRatio;
            nibH = std::max(TextEdit::Styles::SCROLLBAR_SIZE, std::min(nibH, trackH));

            float newNibY = static_cast<float>(e.mouse.y) - mDragScrollbarOffset;
            float maxScrollY = mTotalContentHeight - textDisplayHeight;
            float trackRange = trackH - nibH;
            if (trackRange > 0) {
                mScrollY = ((newNibY - trackY) / trackRange) * maxScrollY;
            }
            ClampScroll();
            return;
        }
        if (mIsDraggingHorzScrollbar) {
            float trackX = mViewX;
            float trackW = mViewWidth - TextEdit::Styles::SCROLLBAR_SIZE;
            float textDisplayWidth = mViewWidth - Styles::HIGHLIGHTER_BUTTON_WIDTH;
            float visibleRatio = textDisplayWidth / mTotalContentWidth;
            float nibW = trackW * visibleRatio;
            nibW = std::max(TextEdit::Styles::SCROLLBAR_SIZE, std::min(nibW, trackW));

            float newNibX = static_cast<float>(e.mouse.x) - mDragScrollbarOffset;
            float maxScrollX = mTotalContentWidth - textDisplayWidth;
            float trackRange = trackW - nibW;
            if (trackRange > 0) {
                mScrollX = ((newNibX - trackX) / trackRange) * maxScrollX;
            }
            ClampScroll();
            return;
        }

        if (mIsSelecting && mIsMouseDown) { // Only extend selection if mouse button is still down
            Document::Cursor docPos = ScreenToDocumentPos(static_cast<float>(e.mouse.x), static_cast<float>(e.mouse.y));
            mDocument->MoveCursor(docPos);
            // UpdateDesiredColumnXFromCursor(); // mDesiredColumnX updates on MoveCursor->SanitizeCursor
            // ScrollToCursor(); // Auto-scroll handles this if dragging out of view
            mShowCursor = true; // Keep cursor visible while selecting
            mCursorBlinkTimer = 0.0f;
        }
    }

    void DocumentView::HandleMouseWheel(const InputEvent& e) {
        float scrollAmountLines = -e.mouse.delta / 120.0f * 3.0f; // Standard wheel delta is 120; scroll 3 lines.

        if (e.mouse.shift) { // Horizontal scroll with Shift+Wheel
            mScrollX += scrollAmountLines * mFont->GetLineHeight() * 3.0f; // Use line height as a basis for speed
        }
        else { // Vertical scroll
            mScrollY += scrollAmountLines * mFont->GetLineHeight();
        }
        ClampScroll();
    }

    // --- Helper Methods ---

    Document::Cursor DocumentView::ScreenToDocumentPos(float screenX, float screenY) const {
        if (!mFont || mViewHeight <= 0) return { 0,0 };

        float textDisplayY = screenY - mViewY; // Y relative to view top
        unsigned int lineIdx = static_cast<unsigned int>((textDisplayY + mScrollY) / mFont->GetLineHeight());
        lineIdx = std::min(lineIdx, mDocument->GetLineCount() > 0 ? mDocument->GetLineCount() - 1 : 0);

        // If click is in line number area, return column 0.
        // The padding area to the right of the numbers is treated as part of the text area.
        if (screenX < mViewX + mLineNumberWidth) {
            return Document::Cursor(lineIdx, 0);
        }

        // Adjust the coordinate to be relative to the start of the actual text, after the padding.
        float textDisplayX = screenX - (mViewX + mLineNumberWidth + Styles::GUTTER_RIGHT_PAD);
        float targetWorldX = textDisplayX + mScrollX;
        unsigned int columnIdx = GetColumnFromPixelOffset(lineIdx, targetWorldX);

        return Document::Cursor(lineIdx, columnIdx);
    }

    float DocumentView::GetColumnPixelOffset(unsigned int lineIdx, unsigned int column) const {
        if (!mFont) return 0.0f;
        const std::u32string& lineText = mDocument->GetLine(lineIdx).text;
        float currentX = 0.0f;
        unsigned int tabSpaces = mFont->GetTabNumSpaces();
        float spaceWidth = static_cast<float>(mFont->GetSpaceWidthPixels());
        if (spaceWidth == 0.0f) spaceWidth = mFont->GetGlyph(U' ').advance;
        if (spaceWidth == 0.0f) spaceWidth = 10.0f;

        // Simple fixed-width tabs
        float tabWidth = spaceWidth * static_cast<float>(tabSpaces);

        for (unsigned int i = 0; i < column; ++i) {
            if (i >= lineText.length()) break;
            char32_t c = lineText[i];
            if (c == U'\t') {
                // Tab is simply a fixed width
                currentX += tabWidth;
            }
            else {
                currentX += mFont->GetGlyph(c).advance;
            }
        }
        return currentX;
    }

    unsigned int DocumentView::GetColumnFromPixelOffset(unsigned int lineIdx, float targetX) const {
        if (!mFont) return 0;
        const std::u32string& lineText = mDocument->GetLine(lineIdx).text;
        float currentX = 0.0f;
        unsigned int tabSpaces = mFont->GetTabNumSpaces();
        float spaceWidth = static_cast<float>(mFont->GetSpaceWidthPixels());
        if (spaceWidth == 0.0f) spaceWidth = mFont->GetGlyph(U' ').advance;
        if (spaceWidth == 0.0f) spaceWidth = 10.0f;

        // Simple fixed-width tabs
        float tabWidth = spaceWidth * static_cast<float>(tabSpaces);

        for (unsigned int i = 0; i < lineText.length(); ++i) {
            char32_t c = lineText[i];
            float charWidth;
            if (c == U'\t') {
                // Tab is simply a fixed width
                charWidth = tabWidth;
            }
            else {
                charWidth = mFont->GetGlyph(c).advance;
            }

            // Check if the target position is within this character
            if (targetX < currentX + charWidth / 2.0f) {
                return i;
            }
            currentX += charWidth;
        }
        return static_cast<unsigned int>(lineText.length());
    }

    float DocumentView::GetLinePixelWidth(unsigned int lineIdx) const {
        if (!mFont) return 0.0f;
        const std::u32string& lineText = mDocument->GetLine(lineIdx).text;
        // This is the same as GetColumnPixelOffset for the full line length
        return GetColumnPixelOffset(lineIdx, static_cast<unsigned int>(lineText.length()));
    }

    void DocumentView::ScrollToCursor() {
        if (!mFont || mViewHeight <= 0 || mViewWidth <= 0) return;

        Document::Cursor cursor = mDocument->GetCursor();
        float lineH = mFont->GetLineHeight();

        float textDisplayWidth = mViewWidth - TextEdit::Styles::SCROLLBAR_SIZE - mLineNumberWidth; // Text area starts after line numbers
        float textDisplayHeight = mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE;

        // Vertical scroll
        float cursorTopY_world = static_cast<float>(cursor.line) * lineH;
        float cursorBottomY_world = cursorTopY_world + lineH;

        if (cursorTopY_world < mScrollY) {
            mScrollY = cursorTopY_world;
        }
        else if (cursorBottomY_world > mScrollY + textDisplayHeight) {
            mScrollY = cursorBottomY_world - textDisplayHeight;
        }

        // Horizontal scroll
        float cursorX_world = GetColumnPixelOffset(cursor.line, cursor.column);
        float charApproxWidth = mFont->GetGlyph(U'M').advance; // Approximate for one char view
        if (charApproxWidth <= 0) charApproxWidth = 10.0f;

        if (cursorX_world < mScrollX) {
            mScrollX = cursorX_world - charApproxWidth; // Show a bit before cursor
        }
        else if (cursorX_world > mScrollX + textDisplayWidth - charApproxWidth) { // Cursor needs charApproxWidth to be visible
            mScrollX = cursorX_world - textDisplayWidth + charApproxWidth * 2; // Show char and bit after
        }

        ClampScroll();
    }

    void DocumentView::ClampScroll() {
        float textDisplayWidth = mViewWidth - TextEdit::Styles::SCROLLBAR_SIZE - mLineNumberWidth; // Text area starts after line numbers
        float textDisplayHeight = mViewHeight - TextEdit::Styles::SCROLLBAR_SIZE;

        float maxScrollY = std::max(0.0f, mTotalContentHeight - textDisplayHeight);
        float maxScrollX = std::max(0.0f, mTotalContentWidth - textDisplayWidth);

        mScrollY = std::max(0.0f, std::min(mScrollY, maxScrollY));
        mScrollX = std::max(0.0f, std::min(mScrollX, maxScrollX));
    }

    void DocumentView::UpdateDesiredColumnXFromCursor() {
        if (!mDocument || !mFont) return;
        Document::Cursor c = mDocument->GetCursor();
        mDesiredColumnX = GetColumnPixelOffset(c.line, c.column);
    }

    bool DocumentView::IsWordChar(char32_t c) const {
        // Basic definition: alphanumeric
        // More advanced: Unicode properties
        return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9') || c == U'_';
    }

    bool DocumentView::IsWhitespace(char32_t c) const {
        return c == U' ' || c == U'\t'; // Does not include newline for this context
    }

    // In DocumentView.cpp, update the GetWordBoundaryLeft and GetWordBoundaryRight methods:

    Document::Cursor DocumentView::GetWordBoundaryLeft(Document::Cursor pos) const {
        if (pos.column == 0) return pos; // Already at line start, can't go further left

        Document::Cursor current = pos;
        const std::u32string& lineText = mDocument->GetLine(current.line).text;

        // Mode 0: skip current char's class
        // Mode 1: skip other class
        int mode = 0;
        bool initialCharIsWord = IsWordChar(lineText[current.column - 1]);
        bool initialCharIsSpace = IsWhitespace(lineText[current.column - 1]);

        while (current.column > 0) {
            char32_t c = lineText[current.column - 1];
            bool isWord = IsWordChar(c);
            bool isSpace = IsWhitespace(c);

            if (mode == 0) {
                if ((isWord && initialCharIsWord) ||
                    (isSpace && initialCharIsSpace) ||
                    (!isWord && !isSpace && !initialCharIsWord && !initialCharIsSpace)) {
                    // Still in the initial character class, continue skipping
                    current.column--;
                }
                else {
                    mode = 1; // Switched class
                    break;
                }
            }
            else { // mode == 1
                if ((isWord && initialCharIsWord) ||
                    (isSpace && initialCharIsSpace) ||
                    (!isWord && !isSpace && !initialCharIsWord && !initialCharIsSpace)) {
                    break; // Found the start of the previous block of same initial type
                }
                current.column--;
            }
        }
        return current;
    }

    Document::Cursor DocumentView::GetWordBoundaryRight(Document::Cursor pos) const {
        Document::Cursor current = pos;
        const std::u32string& lineText = mDocument->GetLine(current.line).text;

        if (current.column >= lineText.length()) {
            return pos; // Already at line end, can't go further right
        }

        // Mode 0: skip current char's class
        // Mode 1: skip other class
        int mode = 0;
        bool initialCharIsWord = IsWordChar(lineText[current.column]);
        bool initialCharIsSpace = IsWhitespace(lineText[current.column]);

        while (current.column < lineText.length()) {
            char32_t c = lineText[current.column];
            bool isWord = IsWordChar(c);
            bool isSpace = IsWhitespace(c);

            if (mode == 0) {
                if ((isWord && initialCharIsWord) ||
                    (isSpace && initialCharIsSpace) ||
                    (!isWord && !isSpace && !initialCharIsWord && !initialCharIsSpace)) {
                    current.column++;
                }
                else {
                    mode = 1; // Switched class
                    break;
                }
            }
            else { // mode == 1
                if ((isWord && initialCharIsWord) ||
                    (isSpace && initialCharIsSpace) ||
                    (!isWord && !isSpace && !initialCharIsWord && !initialCharIsSpace)) {
                    break; // Found start of next block of same initial type
                }
                current.column++;
            }
        }
        return current;
    }

} // namespace TextEdit