// Document view implementation.  This module bridges the document model with
// rendering and user interaction details (scrolling, selection, etc.).
#include "view.h"

#include "document.h"
#include "strings.h"
#include "renderer.h"
#include "imgui.h"
#include "syntax.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

// Internal state for each document view instance (opaque to callers).
struct document_view {
    document* target;
    font* fnt;
    u32_string* path;

    f32 scrollX;
    f32 scrollY;
    f32 maxScrollX;
    f32 maxScrollY;

    u32 displayAreaX;
    u32 displayAreaY;
    u32 displayAreaW;
    u32 displayAreaH;

    bool showLineNumbers;
    bool highlightSyntax;
    u32 lineNumberWidth;
    u32 tabWidth;

    u64 lastClickTime;
    u64 lastDoubleClickTime;
    u32 clickCount;
    document_cursor lastClickPosition;
};

#define CARROT_DOUBLE_CLICK_TIME 500
#define CARROT_TRIPLE_CLICK_TIME 500
#define CARROT_SCROLLBAR_WIDTH 30.0f

// Returns true when the supplied path looks like source code and should enable
// syntax highlighting by default.
static bool has_code_extension(u32_string* path) {
    if (!path) return false;

    u32 len = u32str_length(path);
    if (len < 3) return false;  // Too short to have an extension

    // Find the last dot
    u32 dot_pos = len;
    for (u32 i = len; i > 0; i--) {
        if (u32str_get(path, i - 1) == '.') {
            dot_pos = i - 1;
            break;
        }
        // Stop at path separator to avoid matching dots in directory names
        if (u32str_get(path, i - 1) == '/' || u32str_get(path, i - 1) == '\\') {
            break;
        }
    }

    if (dot_pos >= len - 1) return false;  // No extension found

    // Get the extension (everything after the last dot)
    u32 ext_len = len - dot_pos - 1;

    // Helper to convert to lowercase for comparison
    auto to_lower = [](u32 ch) -> u32 {
        if (ch >= 'A' && ch <= 'Z') return ch + ('a' - 'A');
        return ch;
    };

    // Check for known code file extensions (case-insensitive)
    // .c or .C
    if (ext_len == 1 && to_lower(u32str_get(path, dot_pos + 1)) == 'c') return true;

    // .h or .H
    if (ext_len == 1 && to_lower(u32str_get(path, dot_pos + 1)) == 'h') return true;

    // .cpp or .CPP or .Cpp etc.
    if (ext_len == 3 &&
        to_lower(u32str_get(path, dot_pos + 1)) == 'c' &&
        to_lower(u32str_get(path, dot_pos + 2)) == 'p' &&
        to_lower(u32str_get(path, dot_pos + 3)) == 'p') return true;

    // .cc or .CC or .Cc etc.
    if (ext_len == 2 &&
        to_lower(u32str_get(path, dot_pos + 1)) == 'c' &&
        to_lower(u32str_get(path, dot_pos + 2)) == 'c') return true;

    // .js or .JS or .Js etc.
    if (ext_len == 2 &&
        to_lower(u32str_get(path, dot_pos + 1)) == 'j' &&
        to_lower(u32str_get(path, dot_pos + 2)) == 's') return true;

    // .cs or .CS or .Cs etc.
    if (ext_len == 2 &&
        to_lower(u32str_get(path, dot_pos + 1)) == 'c' &&
        to_lower(u32str_get(path, dot_pos + 2)) == 's') return true;

    return false;
}

document* document_view_get_document(document_view* view) {
    if (view) {
        return view->target;
    }
    return 0;
}

u32 document_view_get_cursor_row(document_view* view) {
    if (view && view->target) {
        return doc_get_cursor(view->target).row;
    }
    return 0;
}

void document_view_set_cursor_row(document_view* view, u32 value) {
    if (view && view->target) {
        document_cursor cursor = doc_get_cursor(view->target);
        doc_set_cursor(view->target, value, cursor.column);
    }
}

void document_view_set_cursor_col(document_view* view, u32 value) {
    if (view && view->target) {
        document_cursor cursor = doc_get_cursor(view->target);
        doc_set_cursor(view->target, cursor.row, value);
    }
}

void document_view_set_display_area(document_view* view, u32 x, u32 y, u32 w, u32 h) {
 if (view) {
    view->displayAreaX = x;
    view->displayAreaY = y; 
    view->displayAreaW = w;
    view->displayAreaH = h;
 }
}

void document_view_set_display_size(document_view* view, u32 w, u32 h) {
    if (view) {
    view->displayAreaW = w;
    view->displayAreaH = h;
 }
}

void document_view_clear_path(document_view* view) {
    if (view) {
        if (view->path) {
            u32str_destroy(view->path);
        }
        view->path = 0;
    }
}

void document_view_set_path(document_view* view, u32_string* path) {
    document_view_clear_path(view);
    if (path) {
        view->path = u32str_init(u32str_getBuffer(path));
    }
}

u32 document_view_get_cursor_col(document_view* view) {
    if (view && view->target) {
        return doc_get_cursor(view->target).column;
    }
    return 0;
}

u32_string* document_view_get_path(document_view* view) {
    if (view) {
        return view->path;
    }
    return 0;
}

document_view* document_view_create(document* doc, font* fnt, u32_string* path) {
    document_view* view = (document_view*)malloc(sizeof(document_view));

    view->target = doc;
    view->fnt = fnt;
    
    //view->path = path ? u32str_substr(path, 0, u32str_length(path)) : nullptr;
    view->path = path ? u32str_init(u32str_getBuffer(path)) : nullptr;

    view->scrollX = 0;
    view->scrollY = 0;
    view->maxScrollX = 0;
    view->maxScrollY = 0;

    view->displayAreaX = 0;
    view->displayAreaY = 0;
    view->displayAreaW = 800;
    view->displayAreaH = 600;

    // Cursor and selection are now managed by the document

    view->showLineNumbers = true;
    view->highlightSyntax = has_code_extension(path);  // Enable for code files
    // Calculate line number width based on font metrics (space for 5 digits + padding)
    view->lineNumberWidth = font_get_char_width(fnt, '0') * 6 + 10;  // 5 digits + space + padding
    view->tabWidth = 4;

    view->lastClickTime = 0;
    view->lastDoubleClickTime = 0;
    view->clickCount = 0;
    view->lastClickPosition.row = 0;
    view->lastClickPosition.column = 0;

    return view;
}

void document_view_destroy(document_view* view) {
    if (!view) return;

    if (view->path) {
        u32str_destroy(view->path);
    }

    // Don't destroy the document - it's owned externally
    free(view);
}

void document_view_update_font(document_view* view, font* fnt) {
    view->fnt = fnt;
    // Recalculate line number width for new font
    view->lineNumberWidth = font_get_char_width(fnt, '0') * 6 + 10;  // 5 digits + space + padding
}

// Calculate the visual column (taking tabs into account) for a character index.
static u32 get_visual_column(u32_string* line, u32 charIndex, u32 tabWidth) {
    u32 visualCol = 0;
    for (u32 i = 0; i < charIndex && i < u32str_length(line); i++) {
        u32 ch = u32str_get(line, i);
        if (ch == '\t') {
            // Tab advances to next tab stop
            visualCol = ((visualCol / tabWidth) + 1) * tabWidth;
        } else {
            visualCol++;
        }
    }
    return visualCol;
}

// Convert a visual column measurement back into a character index.
static u32 visual_to_char_index(u32_string* line, u32 visualCol, u32 tabWidth) {
    u32 currentVisualCol = 0;
    u32 lineLength = u32str_length(line);

    for (u32 i = 0; i < lineLength; i++) {
        if (currentVisualCol >= visualCol) {
            return i;
        }

        u32 ch = u32str_get(line, i);
        if (ch == '\t') {
            currentVisualCol = ((currentVisualCol / tabWidth) + 1) * tabWidth;
        } else {
            currentVisualCol++;
        }
    }

    return lineLength;
}

// Populate start/end with an ordered selection range for this view.
static void normalize_selection(document_view* view, document_cursor* start, document_cursor* end) {
    // Use the document's selection normalization
    doc_get_selection_range(view->target, start, end);
}

// Process keyboard input (already translated to UTF-32 and PlatformKey) against
// the active document and selection.
void document_view_keyboard_input(document_view* view, u32 unicode, PlatformKey virtualKey,
                                 bool isDown, bool alt, bool ctrl, bool shift) {
    if (!isDown) return;

    switch (virtualKey) {
        case PlatformKey::Left:
            if (ctrl) {
                document_view_move_word_left(view, shift);
            } else {
                document_view_move_cursor(view, 0, -1, shift);
            }
            break;

        case PlatformKey::Right:
            if (ctrl) {
                document_view_move_word_right(view, shift);
            } else {
                document_view_move_cursor(view, 0, 1, shift);
            }
            break;

        case PlatformKey::Up:
            document_view_move_cursor(view, -1, 0, shift);
            break;

        case PlatformKey::Down:
            document_view_move_cursor(view, 1, 0, shift);
            break;

        case PlatformKey::Home:
            document_view_move_to_line_start(view, shift);
            break;

        case PlatformKey::End:
            document_view_move_to_line_end(view, shift);
            break;

        case PlatformKey::Backspace:
            document_view_delete_backward(view);
            break;

        case PlatformKey::Delete:
            document_view_delete_forward(view);
            break;

        case PlatformKey::Return:
            {
                u32 newline = '\n';
                document_view_insert_text(view, &newline, 1);
            }
            break;

        case PlatformKey::Tab:
            {
                u32 tab = '\t';
                document_view_insert_text(view, &tab, 1);
            }
            break;

        default:
            if (unicode >= 32 && unicode < 0x10000 && !ctrl && !alt) {
                document_view_insert_text(view, &unicode, 1);
            }
            break;
    }
}

static bool is_whitespace(u32 ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static void select_word_at_cursor(document_view* view, document_cursor cursor) {
    u32_string* line = doc_get_line(view->target, cursor.row);
    if (!line) return;

    u32 lineLength = u32str_length(line);
    if (cursor.column >= lineLength) return;

    u32 wordStart = cursor.column;
    u32 wordEnd = cursor.column;

    while (wordStart > 0 && !is_whitespace(u32str_get(line, wordStart - 1))) {
        wordStart--;
    }

    while (wordEnd < lineLength && !is_whitespace(u32str_get(line, wordEnd))) {
        wordEnd++;
    }

    doc_set_selection_anchor(view->target, cursor.row, wordStart);
    doc_set_cursor(view->target, cursor.row, wordEnd);
    doc_set_has_selection(view->target, true);
}

static void select_line(document_view* view, u32 row) {
    doc_set_selection_anchor(view->target, row, 0);
    doc_set_cursor(view->target, row, doc_get_line_length(view->target, row));
    doc_set_has_selection(view->target, true);
}

// Handle mouse button presses, releases, and wheel scrolling.
void document_view_mouse_input(document_view* view, u32 x, u32 y,
                               f32 scrollDelta, bool leftDown, bool middleDown, bool rightDown,
                               bool shiftDown) {
    static bool wasLeftDown = false;
    static bool wasRightDown = false;

    // Commit any pending word when mouse is clicked
    if (view && view->target && (leftDown || rightDown)) {
        doc_commit_pending_undo(view->target);
    }

    // Check if mouse is over scrollbar areas
    f32 viewWidth = view->displayAreaW;
    f32 viewHeight = view->displayAreaH;

    if (view->showLineNumbers) {
        viewWidth -= view->lineNumberWidth;
    }

    bool needsHScroll = view->maxScrollX > 0;
    bool needsVScroll = view->maxScrollY > 0;

    if (needsHScroll) viewHeight -= CARROT_SCROLLBAR_WIDTH;
    if (needsVScroll) viewWidth -= CARROT_SCROLLBAR_WIDTH;

    // Check if mouse is in vertical scrollbar area
    bool inVScrollBar = false;
    if (needsVScroll) {
        u32 scrollbarX = view->displayAreaX + view->displayAreaW - (u32)CARROT_SCROLLBAR_WIDTH;
        if (x >= scrollbarX && x < view->displayAreaX + view->displayAreaW &&
            y >= view->displayAreaY && y < view->displayAreaY + view->displayAreaH) {
            inVScrollBar = true;
        }
    }

    // Check if mouse is in horizontal scrollbar area
    bool inHScrollBar = false;
    if (needsHScroll) {
        u32 scrollbarY = view->displayAreaY + view->displayAreaH - (u32)CARROT_SCROLLBAR_WIDTH;
        if (y >= scrollbarY && y < view->displayAreaY + view->displayAreaH &&
            x >= view->displayAreaX && x < view->displayAreaX + view->displayAreaW) {
            inHScrollBar = true;
        }
    }

    // If mouse is over a scrollbar, don't process document interactions
    if (inVScrollBar || inHScrollBar) {
        return;
    }

    if (scrollDelta != 0) {
        view->scrollY -= scrollDelta * font_get_line_height(view->fnt) * 3;
        view->scrollY = std::max(0.0f, std::min(view->scrollY, view->maxScrollY));
    }

    u32 effectiveX = x - view->displayAreaX;
    u32 effectiveY = y - view->displayAreaY;

    if (view->showLineNumbers) {
        effectiveX = effectiveX > view->lineNumberWidth ? effectiveX - view->lineNumberWidth : 0;
    }

    document_cursor clickedCursor = document_view_pixel_to_cursor(view, effectiveX, effectiveY);

    if (leftDown && !wasLeftDown) {
        u64 currentTime = platform_get_milliseconds();

        if (shiftDown) {
            document_cursor currentCursor = doc_get_cursor(view->target);
            if (!doc_has_selection(view->target)) {
                doc_set_selection_anchor(view->target, currentCursor.row, currentCursor.column);
            }

            doc_set_cursor(view->target, clickedCursor.row, clickedCursor.column);

            document_cursor anchor = doc_get_selection_anchor(view->target);
            document_cursor cursor = doc_get_cursor(view->target);
            bool hasSelection = (anchor.row != cursor.row) || (anchor.column != cursor.column);
            doc_set_has_selection(view->target, hasSelection);
            view->clickCount = 1;
            view->lastDoubleClickTime = 0;
        } else {
            bool samePosition = (clickedCursor.row == view->lastClickPosition.row &&
                                 clickedCursor.column == view->lastClickPosition.column);

            if (!samePosition || currentTime - view->lastClickTime > CARROT_DOUBLE_CLICK_TIME) {
                view->clickCount = 1;
                view->lastDoubleClickTime = 0;
                doc_set_cursor(view->target, clickedCursor.row, clickedCursor.column);
                doc_set_selection_anchor(view->target, clickedCursor.row, clickedCursor.column);
                doc_set_has_selection(view->target, false);
            } else if (view->clickCount == 1 &&
                       currentTime - view->lastClickTime <= CARROT_DOUBLE_CLICK_TIME) {
                view->clickCount = 2;
                view->lastDoubleClickTime = currentTime;
                select_word_at_cursor(view, clickedCursor);
            } else if (view->clickCount >= 2 &&
                       currentTime - view->lastDoubleClickTime <= CARROT_TRIPLE_CLICK_TIME) {
                view->clickCount = 3;
                select_line(view, clickedCursor.row);
            } else {
                view->clickCount = 1;
                view->lastDoubleClickTime = 0;
                doc_set_cursor(view->target, clickedCursor.row, clickedCursor.column);
                doc_set_selection_anchor(view->target, clickedCursor.row, clickedCursor.column);
                doc_set_has_selection(view->target, false);
            }
        }

        view->lastClickTime = currentTime;
        view->lastClickPosition = clickedCursor;
        doc_validate_cursor(view->target);
    }

    if (rightDown && !wasRightDown) {
        bool insideSelection = false;

        if (doc_has_selection(view->target)) {
            document_cursor start, end;
            normalize_selection(view, &start, &end);

            if (clickedCursor.row > start.row && clickedCursor.row < end.row) {
                insideSelection = true;
            } else if (clickedCursor.row == start.row && clickedCursor.row == end.row) {
                insideSelection = clickedCursor.column >= start.column && clickedCursor.column <= end.column;
            } else if (clickedCursor.row == start.row) {
                insideSelection = clickedCursor.column >= start.column;
            } else if (clickedCursor.row == end.row) {
                insideSelection = clickedCursor.column <= end.column;
            }
        }

        if (!insideSelection) {
            doc_set_cursor(view->target, clickedCursor.row, clickedCursor.column);
            doc_set_has_selection(view->target, false);
            doc_validate_cursor(view->target);
        }

    }

    wasLeftDown = leftDown;
    wasRightDown = rightDown;
}

void document_view_mouse_moved(document_view* view, u32 x, u32 y, bool leftDown) {
    if (!leftDown) return;

    // Check if mouse is over scrollbar areas
    f32 viewWidth = view->displayAreaW;
    f32 viewHeight = view->displayAreaH;

    if (view->showLineNumbers) {
        viewWidth -= view->lineNumberWidth;
    }

    bool needsHScroll = view->maxScrollX > 0;
    bool needsVScroll = view->maxScrollY > 0;

    if (needsHScroll) viewHeight -= CARROT_SCROLLBAR_WIDTH;
    if (needsVScroll) viewWidth -= CARROT_SCROLLBAR_WIDTH;

    // Check if mouse is in vertical scrollbar area
    bool inVScrollBar = false;
    if (needsVScroll) {
        u32 scrollbarX = view->displayAreaX + view->displayAreaW - (u32)CARROT_SCROLLBAR_WIDTH;
        if (x >= scrollbarX && x < view->displayAreaX + view->displayAreaW &&
            y >= view->displayAreaY && y < view->displayAreaY + view->displayAreaH) {
            inVScrollBar = true;
        }
    }

    // Check if mouse is in horizontal scrollbar area
    bool inHScrollBar = false;
    if (needsHScroll) {
        u32 scrollbarY = view->displayAreaY + view->displayAreaH - (u32)CARROT_SCROLLBAR_WIDTH;
        if (y >= scrollbarY && y < view->displayAreaY + view->displayAreaH &&
            x >= view->displayAreaX && x < view->displayAreaX + view->displayAreaW) {
            inHScrollBar = true;
        }
    }

    // If mouse is over a scrollbar, don't process document interactions
    if (inVScrollBar || inHScrollBar) {
        return;
    }

    u32 effectiveX = x - view->displayAreaX;
    u32 effectiveY = y - view->displayAreaY;

    if (view->showLineNumbers) {
        effectiveX = effectiveX > view->lineNumberWidth ? effectiveX - view->lineNumberWidth : 0;
    }

    document_cursor newCursor = document_view_pixel_to_cursor(view, effectiveX, effectiveY);
    doc_set_cursor(view->target, newCursor.row, newCursor.column);
    doc_validate_cursor(view->target);

    document_cursor cursor = doc_get_cursor(view->target);
    document_cursor anchor = doc_get_selection_anchor(view->target);
    if (cursor.row != anchor.row || cursor.column != anchor.column) {
        doc_set_has_selection(view->target, true);
    }
}

void document_view_update(document_view* view, f32 deltaTime) {
    u32 lineCount = doc_line_count(view->target);
    f32 lineHeight = font_get_line_height(view->fnt);
    f32 charWidth = font_get_char_width(view->fnt, 'x');  // Use average char width
    f32 contentHeight = lineCount * lineHeight;
    f32 contentWidth = 0;

    for (u32 i = 0; i < lineCount; i++) {
        u32_string* line = doc_get_line(view->target, i);
        if (line && u32str_length(line) > 0) {
            // Calculate visual width accounting for tabs
            u32 visualWidth = get_visual_column(line, u32str_length(line), view->tabWidth);
            f32 lineWidth = visualWidth * charWidth;
            if (lineWidth > contentWidth) {
                contentWidth = lineWidth;
            }
        }
    }

    f32 viewWidth = view->displayAreaW;
    f32 viewHeight = view->displayAreaH;

    if (view->showLineNumbers) {
        viewWidth -= view->lineNumberWidth;
    }

    bool needsHScroll = contentWidth > viewWidth - CARROT_SCROLLBAR_WIDTH;
    bool needsVScroll = contentHeight > viewHeight - CARROT_SCROLLBAR_WIDTH;

    if (needsHScroll) viewHeight -= CARROT_SCROLLBAR_WIDTH;
    if (needsVScroll) viewWidth -= CARROT_SCROLLBAR_WIDTH;

    view->maxScrollX = std::max(0.0f, contentWidth - viewWidth);
    view->maxScrollY = std::max(0.0f, contentHeight - viewHeight);

    view->scrollX = std::max(0.0f, std::min(view->scrollX, view->maxScrollX));
    view->scrollY = std::max(0.0f, std::min(view->scrollY, view->maxScrollY));
}

void document_view_render(document_view* view, struct ImGui* imgui_context, canvas* cnvs, font* fnt, bool showLineNumbers) {
    if (!cnvs || !fnt || !imgui_context) return;

    view->showLineNumbers = showLineNumbers;

    // Draw document background - SPECTRUM_DARKEST_GRAY_100 (darker than header)
    canvas_draw_rect(cnvs, view->displayAreaX, view->displayAreaY,
                     view->displayAreaW, view->displayAreaH,
                     0x2F, 0x2F, 0x2F);

    f32 viewWidth = view->displayAreaW;
    f32 viewHeight = view->displayAreaH;
    f32 contentStartX = view->displayAreaX;

    if (view->showLineNumbers) {
        viewWidth -= view->lineNumberWidth;
        contentStartX += view->lineNumberWidth;
    }

    bool needsHScroll = view->maxScrollX > 0;
    bool needsVScroll = view->maxScrollY > 0;

    if (needsHScroll) viewHeight -= CARROT_SCROLLBAR_WIDTH;
    if (needsVScroll) viewWidth -= CARROT_SCROLLBAR_WIDTH;

    u32 lineHeight = font_get_line_height(fnt);
    u32 charWidth = font_get_char_width(fnt, 'x');  // Use average char width
    u32 firstVisibleLine = (u32)(view->scrollY / lineHeight);
    u32 lastVisibleLine = (u32)((view->scrollY + viewHeight) / lineHeight) + 1;
    u32 lineCount = doc_line_count(view->target);
    lastVisibleLine = std::min(lastVisibleLine, lineCount);

    document_cursor selStart = {0, 0}, selEnd = {0, 0};
    if (doc_has_selection(view->target)) {
        normalize_selection(view, &selStart, &selEnd);
    }

    document_cursor cursorPos = doc_get_cursor(view->target);

    // Draw line numbers area with different background (before setting clip rect)
    if (view->showLineNumbers) {
        // Calculate the split point for the margin (half of one character width)
        u32 charWidth = font_get_char_width(fnt, '0');
        u32 marginSplit = charWidth / 2;

        // Draw line number background - SPECTRUM_DARKEST_GRAY_75 (slightly darker than document)
        // Reduce width slightly to give some margin pixels to the document area
        canvas_draw_rect(cnvs, view->displayAreaX, view->displayAreaY,
                        view->lineNumberWidth - marginSplit, view->displayAreaH,
                        0x27, 0x27, 0x27);  // Darker than document background

        // Draw a thin strip of document background color in the margin area
        canvas_draw_rect(cnvs, view->displayAreaX + view->lineNumberWidth - marginSplit, view->displayAreaY,
                        marginSplit, view->displayAreaH,
                        0x2F, 0x2F, 0x2F);  // Document background color

        // Set clip rect for line numbers to prevent overlap with menu bar
        canvas_set_clip(cnvs, view->displayAreaX, view->displayAreaY,
                       view->lineNumberWidth, view->displayAreaH);

        // Draw line numbers
        for (u32 lineIdx = firstVisibleLine; lineIdx < lastVisibleLine; lineIdx++) {
            f32 yPos = view->displayAreaY + (lineIdx * lineHeight) - view->scrollY;
            char lineNumStr[16];
            sprintf(lineNumStr, "%5u ", lineIdx + 1);  // 5 digits + space
            canvas_draw_text_cstr(cnvs, fnt, lineNumStr, view->displayAreaX + 5, (u32)yPos, 0x75, 0x75, 0x75);  // SPECTRUM_DARKEST_GRAY_500
        }

        // Reset clip rect to full canvas before setting document content clip
        canvas_set_clip(cnvs, 0, 0, canvas_get_width(cnvs), canvas_get_height(cnvs));
    }

    // Now set clip rect for content area only
    canvas_set_clip(cnvs, (u32)contentStartX, view->displayAreaY,
                    (u32)viewWidth, (u32)viewHeight);

    // Define colors for different token types using Adobe Spectrum Darkest Theme
    const u8 color_default[3] = {0xDC, 0xDC, 0xDC};       // SPECTRUM_DARKEST_GRAY_800 - default text
    const u8 color_keyword[3] = {0x37, 0x8E, 0xF0};       // SPECTRUM_DARKEST_BLUE_500 - keywords
    const u8 color_comment[3] = {0x33, 0xAB, 0x84};       // SPECTRUM_DARKEST_GREEN_500 - comments
    const u8 color_preprocessor[3] = {0xEC, 0x44, 0x91};  // SPECTRUM_DARKEST_MAGENTA_500 - preprocessor
    const u8 color_whitespace[3] = {0xDC, 0xDC, 0xDC};    // Same as default
    const u8 color_literal[3] = {0xF2, 0x94, 0x23};       // SPECTRUM_DARKEST_ORANGE_500 - literals

    for (u32 lineIdx = firstVisibleLine; lineIdx < lastVisibleLine; lineIdx++) {
        f32 yPos = view->displayAreaY + (lineIdx * lineHeight) - view->scrollY;

        u32_string* line = doc_get_line(view->target, lineIdx);
        if (!line) continue;

        u32 lineLength = u32str_length(line);

        // Draw selection background
        if (doc_has_selection(view->target)) {
            u32 selStartCol = 0, selEndCol = lineLength;

            if (lineIdx == selStart.row && lineIdx == selEnd.row) {
                selStartCol = selStart.column;
                selEndCol = selEnd.column;
            } else if (lineIdx == selStart.row) {
                selStartCol = selStart.column;
            } else if (lineIdx == selEnd.row) {
                selEndCol = selEnd.column;
            } else if (lineIdx < selStart.row || lineIdx > selEnd.row) {
                selStartCol = selEndCol = 0;
            }

            if (selEndCol > selStartCol) {
                // Calculate visual columns for selection
                u32 selStartVisualCol = get_visual_column(line, selStartCol, view->tabWidth);
                u32 selEndVisualCol = get_visual_column(line, selEndCol, view->tabWidth);

                u32 selX1 = (u32)(contentStartX + (selStartVisualCol * charWidth) - view->scrollX);
                u32 selX2 = (u32)(contentStartX + (selEndVisualCol * charWidth) - view->scrollX);

                // Clip to visible area
                if (selX1 < contentStartX) selX1 = contentStartX;
                if (selX2 > contentStartX + viewWidth) selX2 = contentStartX + (u32)viewWidth;

                if (selX2 > selX1) {
                    canvas_draw_rect(cnvs, selX1, (u32)yPos, selX2 - selX1, lineHeight, 0x7A, 0x3A, 0x6E);  // Gray-pink to match tabs
                }
            }
        }

        // Draw text with or without syntax highlighting
        if (!view->highlightSyntax) {
            // Draw text without syntax highlighting - character by character for tab handling
            u32 visualCol = 0;
            for (u32 charIdx = 0; charIdx < lineLength; charIdx++) {
                u32 ch = u32str_get(line, charIdx);
                u32 xPos = (u32)(contentStartX + (visualCol * charWidth) - view->scrollX);

                // Check if this character is visible
                if (visualCol * charWidth >= view->scrollX &&
                    visualCol * charWidth < view->scrollX + viewWidth) {
                    if (ch == '\t') {
                        // Draw spaces for tab
                        u32 tabStop = ((visualCol / view->tabWidth) + 1) * view->tabWidth;
                        u32 spacesToDraw = tabStop - visualCol;
                        char space[2] = {' ', '\0'};
                        for (u32 i = 0; i < spacesToDraw; i++) {
                            canvas_draw_text_cstr(cnvs, fnt, space, xPos + i * charWidth, (u32)yPos,
                                                color_default[0], color_default[1], color_default[2]);
                        }
                    } else {
                        char glyph = (ch >= 32 && ch <= 126) ? (char)ch : '?';
                        char str[2] = {glyph, '\0'};
                        canvas_draw_text_cstr(cnvs, fnt, str, xPos, (u32)yPos,
                                            color_default[0], color_default[1], color_default[2]);
                    }
                }

                // Update visual column
                if (ch == '\t') {
                    visualCol = ((visualCol / view->tabWidth) + 1) * view->tabWidth;
                } else {
                    visualCol++;
                }
            }
        } else {
            // Draw text with syntax highlighting
            // Tokenize the line if needed
            doc_tokenize_line(view->target, lineIdx);

            // Get tokens for this line
            token_span* tokens = doc_get_line_tokens(view->target, lineIdx);
            u32 token_count = doc_get_line_token_count(view->target, lineIdx);

            if (token_count == 0 || !tokens) {
                // No tokens available, fall back to default color
                u32 visualCol = 0;
                for (u32 charIdx = 0; charIdx < lineLength; charIdx++) {
                    u32 ch = u32str_get(line, charIdx);
                    u32 xPos = (u32)(contentStartX + (visualCol * charWidth) - view->scrollX);

                    // Check if this character is visible
                    if (visualCol * charWidth >= view->scrollX &&
                        visualCol * charWidth < view->scrollX + viewWidth) {
                        if (ch == '\t') {
                            // Draw spaces for tab
                            u32 tabStop = ((visualCol / view->tabWidth) + 1) * view->tabWidth;
                            u32 spacesToDraw = tabStop - visualCol;
                            char space[2] = {' ', '\0'};
                            for (u32 i = 0; i < spacesToDraw; i++) {
                                canvas_draw_text_cstr(cnvs, fnt, space, xPos + i * charWidth, (u32)yPos,
                                                    color_default[0], color_default[1], color_default[2]);
                            }
                        } else {
                            char glyph = (ch >= 32 && ch <= 126) ? (char)ch : '?';
                            char str[2] = {glyph, '\0'};
                            canvas_draw_text_cstr(cnvs, fnt, str, xPos, (u32)yPos,
                                                color_default[0], color_default[1], color_default[2]);
                        }
                    }

                    // Update visual column
                    if (ch == '\t') {
                        visualCol = ((visualCol / view->tabWidth) + 1) * view->tabWidth;
                    } else {
                        visualCol++;
                    }
                }
            } else {
                // Render each token with its appropriate color
                u32 visualCol = 0;

                for (u32 tok_idx = 0; tok_idx < token_count; tok_idx++) {
                    u32 token_span_start = token_span_get_start(token_span_array_at(tokens, tok_idx));
                    u32 token_span_end = token_span_get_end(token_span_array_at(tokens, tok_idx));
                    token_type token_span_type = token_span_get_type(token_span_array_at(tokens, tok_idx));

                    // Choose color based on token type
                    const u8* color = color_default;
                    switch (token_span_type) {
                        case TOKEN_KEYWORD:
                            color = color_keyword;
                            break;
                        case TOKEN_COMMENT:
                            color = color_comment;
                            break;
                        case TOKEN_PREPROCESSOR:
                            color = color_preprocessor;
                            break;
                        case TOKEN_WHITESPACE:
                            color = color_whitespace;
                            break;
                        case TOKEN_LITERAL:
                            color = color_literal;
                            break;
                        case TOKEN_NONE:
                        default:
                            color = color_default;
                            break;
                    }

                    // Draw each character in the token (handling tabs properly)
                    for (u32 charIdx = token_span_start; charIdx < token_span_end && charIdx < lineLength; charIdx++) {
                        u32 ch = u32str_get(line, charIdx);
                        u32 xPos = (u32)(contentStartX + (visualCol * charWidth) - view->scrollX);

                        // Check if this character is visible
                        if (visualCol * charWidth >= view->scrollX &&
                            visualCol * charWidth < view->scrollX + viewWidth) {
                            if (ch == '\t') {
                                // Draw spaces for tab
                                u32 tabStop = ((visualCol / view->tabWidth) + 1) * view->tabWidth;
                                u32 spacesToDraw = tabStop - visualCol;
                                char space[2] = {' ', '\0'};
                                for (u32 i = 0; i < spacesToDraw; i++) {
                                    canvas_draw_text_cstr(cnvs, fnt, space, xPos + i * charWidth, (u32)yPos,
                                                        color[0], color[1], color[2]);
                                }
                            } else {
                                char glyph = (ch >= 32 && ch <= 126) ? (char)ch : '?';
                                char str[2] = {glyph, '\0'};
                                canvas_draw_text_cstr(cnvs, fnt, str, xPos, (u32)yPos,
                                                    color[0], color[1], color[2]);
                            }
                        }

                        // Update visual column
                        if (ch == '\t') {
                            visualCol = ((visualCol / view->tabWidth) + 1) * view->tabWidth;
                        } else {
                            visualCol++;
                        }
                    }
                }
            }
        }

        // Draw cursor
        if (cursorPos.row == lineIdx) {
            u32 cursorVisualCol = get_visual_column(line, cursorPos.column, view->tabWidth);
            u32 cursorX = (u32)(contentStartX + (cursorVisualCol * charWidth) - view->scrollX);
            canvas_draw_rect(cnvs, cursorX, (u32)yPos, 2, lineHeight, 0xFF, 0xFF, 0xFF);  // SPECTRUM_DARKEST_GRAY_900 - bright cursor
        }
    }

    // Reset clip rect
    canvas_set_clip(cnvs, 0, 0, canvas_get_width(cnvs), canvas_get_height(cnvs));

    // Draw interactive scrollbars using ImGui extended functions
    // Vertical scrollbar goes all the way to the bottom
    if (needsVScroll) {
        u32 scrollbarX = view->displayAreaX + view->displayAreaW - (u32)CARROT_SCROLLBAR_WIDTH;
        u32 scrollbarHeight = view->displayAreaH;  // Full height, no reduction

        bool scrollChanged = false;
        f32 newScrollY = ImGuiVerticalScrollBarEx(imgui_context,
                                                  scrollbarX, view->displayAreaY,
                                                  (u32)CARROT_SCROLLBAR_WIDTH, scrollbarHeight,
                                                  view->scrollY, viewHeight, viewHeight + view->maxScrollY,
                                                  &scrollChanged);
        if (scrollChanged) {
            view->scrollY = newScrollY;
        }
    }

    // Horizontal scrollbar is shortened to avoid overlap with vertical scrollbar
    if (needsHScroll) {
        u32 scrollbarY = view->displayAreaY + view->displayAreaH - (u32)CARROT_SCROLLBAR_WIDTH;
        u32 scrollbarWidth = view->displayAreaW;
        if (needsVScroll) scrollbarWidth -= (u32)CARROT_SCROLLBAR_WIDTH;  // Shorten to avoid overlap

        bool scrollChanged = false;
        f32 newScrollX = ImGuiHorizontalScrollBarEx(imgui_context,
                                                    view->displayAreaX, scrollbarY,
                                                    scrollbarWidth, (u32)CARROT_SCROLLBAR_WIDTH,
                                                    view->scrollX, viewWidth, viewWidth + view->maxScrollX,
                                                    &scrollChanged);
        if (scrollChanged) {
            view->scrollX = newScrollX;
        }
    }
}

void document_view_set_cursor(document_view* view, u32 row, u32 column) {
    doc_set_cursor(view->target, row, column);
    doc_validate_cursor(view->target);
    doc_set_has_selection(view->target, false);
}

void document_view_set_highlight_syntax(document_view* view, bool highlight) {
    view->highlightSyntax = highlight;
}

bool document_view_get_highlight_syntax(document_view* view) {
    return view->highlightSyntax;
}

void document_view_select_all(document_view* view) {
    doc_set_selection_anchor(view->target, 0, 0);

    u32 lastLine = doc_line_count(view->target);
    if (lastLine > 0) {
        doc_set_cursor(view->target, lastLine - 1, doc_get_line_length(view->target, lastLine - 1));
    } else {
        doc_set_cursor(view->target, 0, 0);
    }

    doc_set_has_selection(view->target, true);
}

void document_view_clear_selection(document_view* view) {
    doc_set_has_selection(view->target, false);
}

bool document_view_has_selection(document_view* view) {
    return doc_has_selection(view->target);
}

u32_string* document_view_get_selection(document_view* view) {
    if (!doc_has_selection(view->target)) return nullptr;

    document_cursor start, end;
    normalize_selection(view, &start, &end);

    if (start.row == end.row && start.column == end.column) {
        return nullptr;
    }

    u32_string* result = u32str_create();

    if (start.row == end.row) {
        u32_string* line = doc_get_line(view->target, start.row);
        if (line) {
            u32 lineLength = u32str_length(line);
            u32 clampedEnd = std::min(end.column, lineLength);
            if (start.column < clampedEnd) {
                u32str_insert(result, line, u32str_length(result), start.column, clampedEnd - start.column);
                return result;
            }
        }
        u32str_destroy(result);
        return nullptr;
    }

    u32 totalLength = end.row - start.row; // newline characters between lines

    u32_string* line = doc_get_line(view->target, start.row);
    if (line) {
        u32 lineLength = u32str_length(line);
        if (start.column < lineLength) {
            totalLength += lineLength - start.column;
        }
    }

    for (u32 row = start.row + 1; row < end.row; ++row) {
        line = doc_get_line(view->target, row);
        if (line) {
            totalLength += u32str_length(line);
        }
    }

    line = doc_get_line(view->target, end.row);
    if (line) {
        u32 lineLength = u32str_length(line);
        totalLength += std::min(end.column, lineLength);
    }

    if (totalLength > 0) {
        u32str_reserve(result, totalLength);
    }

    // First line portion
    line = doc_get_line(view->target, start.row);
    if (line) {
        u32 lineLength = u32str_length(line);
        if (start.column < lineLength) {
            u32 copyLen = lineLength - start.column;
            u32str_insert(result, line, u32str_length(result), start.column, copyLen);
        }
    }
    u32str_insert_char(result, u32str_length(result), '\n');

    // Middle lines
    for (u32 row = start.row + 1; row < end.row; ++row) {
        line = doc_get_line(view->target, row);
        if (line) {
            u32 lineLen = u32str_length(line);
            if (lineLen > 0) {
                u32str_insert(result, line, u32str_length(result), 0, lineLen);
            }
        }
        u32str_insert_char(result, u32str_length(result), '\n');
    }

    // Last line portion
    line = doc_get_line(view->target, end.row);
    if (line) {
        u32 lineLength = u32str_length(line);
        u32 copyLen = std::min(end.column, lineLength);
        if (copyLen > 0) {
            u32str_insert(result, line, u32str_length(result), 0, copyLen);
        }
    }

    return result;
}

void document_view_insert_text(document_view* view, const u32* text, u32 length) {
    if (doc_has_selection(view->target)) {
        document_view_delete_selection(view);
    }

    for (u32 i = 0; i < length; i++) {
        if (text[i] == '\n') {
            document_cursor cursor = doc_get_cursor(view->target);
            doc_split_line(view->target, cursor.row, cursor.column);
        } else {
            document_cursor cursor = doc_get_cursor(view->target);
            doc_insert_char(view->target, cursor.row, cursor.column, text[i]);
            document_cursor updated = doc_get_cursor(view->target);
            doc_set_cursor(view->target, updated.row, updated.column + 1);
        }
    }

    doc_validate_cursor(view->target);
    document_view_ensure_cursor_visible(view);
}

void document_view_delete_selection(document_view* view) {
    if (!doc_has_selection(view->target)) return;

    document_cursor start, end;
    normalize_selection(view, &start, &end);

    // Use doc_delete_range for both single-line and multi-line selections
    // This creates a single undo action for the entire delete operation
    doc_delete_range(view->target, start.row, start.column, end.row, end.column);

    doc_set_cursor(view->target, start.row, start.column);
    doc_set_has_selection(view->target, false);
    doc_validate_cursor(view->target);
}

void document_view_delete_forward(document_view* view) {
    if (doc_has_selection(view->target)) {
        document_view_delete_selection(view);
        return;
    }

    document_cursor cursor = doc_get_cursor(view->target);
    u32 lineLength = doc_get_line_length(view->target, cursor.row);
    u32 cursor_row = cursor.row;
    u32 cursor_col = cursor.column;
    if (cursor_col < lineLength) {
        doc_delete_range(view->target, cursor_row, cursor_col, cursor_row, cursor_col + 1);
    } else if (cursor_row + 1 < doc_line_count(view->target)) {
        doc_join_lines(view->target, cursor_row);
    }
}

void document_view_delete_backward(document_view* view) {
    if (doc_has_selection(view->target)) {
        document_view_delete_selection(view);
        return;
    }

    document_cursor cursor = doc_get_cursor(view->target);
    u32 cursor_row = cursor.row;
    u32 cursor_col = cursor.column;

    if (cursor_col > 0) {
        doc_set_cursor(view->target, cursor_row, cursor_col - 1);
        doc_delete_range(view->target, cursor_row, cursor_col - 1, cursor_row, cursor_col);
    } else if (cursor_row > 0) {
        u32 prev_line_len = doc_get_line_length(view->target, cursor_row - 1);
        doc_set_cursor(view->target, cursor_row - 1, prev_line_len);
        doc_join_lines(view->target, cursor_row - 1);
    }
}

void document_view_move_cursor(document_view* view, i32 rowDelta, i32 colDelta, bool extend_selection) {
    // Commit any pending word when cursor moves
    if (view && view->target) {
        doc_commit_pending_undo(view->target);
    }

    if (!extend_selection && doc_has_selection(view->target)) {
        doc_set_has_selection(view->target, false);
    }

    if (extend_selection && !doc_has_selection(view->target)) {
        document_cursor cursor = doc_get_cursor(view->target);
        doc_set_selection_anchor(view->target, cursor.row, cursor.column);
        doc_set_has_selection(view->target, true);
    }

    document_cursor cursor = doc_get_cursor(view->target);
    u32 cursor_row = cursor.row;
    u32 cursor_col = cursor.column;

    if (rowDelta != 0) {
        i32 newRow = (i32)cursor_row + rowDelta;
        if (newRow < 0) newRow = 0;
        doc_set_cursor(view->target, (u32)newRow, cursor_col);
        cursor = doc_get_cursor(view->target);
        cursor_row = cursor.row;
        cursor_col = cursor.column;
    }

    if (colDelta != 0) {
        i32 newCol = (i32)cursor_col + colDelta;
        if (newCol < 0) {
            cursor = doc_get_cursor(view->target);
            u32 current_row = cursor.row;
            if (current_row > 0) {
                u32 prev_line_len = doc_get_line_length(view->target, current_row - 1);
                doc_set_cursor(view->target, current_row - 1, prev_line_len);
            } else {
                doc_set_cursor(view->target, current_row, 0);
            }
        } else {
            cursor = doc_get_cursor(view->target);
            u32 current_row = cursor.row;
            u32 lineLength = doc_get_line_length(view->target, current_row);
            if ((u32)newCol > lineLength) {
                if (current_row + 1 < doc_line_count(view->target)) {
                    doc_set_cursor(view->target, current_row + 1, 0);
                } else {
                    doc_set_cursor(view->target, current_row, lineLength);
                }
            } else {
                doc_set_cursor(view->target, current_row, (u32)newCol);
            }
        }
    }

    doc_validate_cursor(view->target);
    document_view_ensure_cursor_visible(view);
}

void document_view_move_word_left(document_view* view, bool extend_selection) {
    if (!extend_selection && doc_has_selection(view->target)) {
        document_cursor start, end;
        normalize_selection(view, &start, &end);
        doc_set_cursor(view->target, start.row, start.column);
        doc_set_has_selection(view->target, false);
        return;
    }

    if (extend_selection && !doc_has_selection(view->target)) {
        document_cursor cursor = doc_get_cursor(view->target);
        doc_set_selection_anchor(view->target, cursor.row, cursor.column);
        doc_set_has_selection(view->target, true);
    }

    document_cursor cursor = doc_get_cursor(view->target);
    u32 cursor_row = cursor.row;
    u32 cursor_col = cursor.column;
    u32_string* line = doc_get_line(view->target, cursor_row);

    if (cursor_col == 0) {
        if (cursor_row > 0) {
            u32 prev_line_len = doc_get_line_length(view->target, cursor_row - 1);
            doc_set_cursor(view->target, cursor_row - 1, prev_line_len);
        }
    } else if (line) {
        u32 pos = cursor_col - 1;

        while (pos > 0 && is_whitespace(u32str_get(line, pos))) {
            pos--;
        }

        while (pos > 0 && !is_whitespace(u32str_get(line, pos - 1))) {
            pos--;
        }

        doc_set_cursor(view->target, cursor_row, pos);
    }

    document_view_ensure_cursor_visible(view);
}

void document_view_move_word_right(document_view* view, bool extend_selection) {
    if (!extend_selection && doc_has_selection(view->target)) {
        document_cursor start, end;
        normalize_selection(view, &start, &end);
        doc_set_cursor(view->target, end.row, end.column);
        doc_set_has_selection(view->target, false);
        return;
    }

    if (extend_selection && !doc_has_selection(view->target)) {
        document_cursor cursor = doc_get_cursor(view->target);
        doc_set_selection_anchor(view->target, cursor.row, cursor.column);
        doc_set_has_selection(view->target, true);
    }

    document_cursor cursor = doc_get_cursor(view->target);
    u32 cursor_row = cursor.row;
    u32 cursor_col = cursor.column;
    u32_string* line = doc_get_line(view->target, cursor_row);
    u32 lineLength = line ? u32str_length(line) : 0;

    if (cursor_col >= lineLength) {
        if (cursor_row + 1 < doc_line_count(view->target)) {
            doc_set_cursor(view->target, cursor_row + 1, 0);
        }
    } else if (line) {
        u32 pos = cursor_col;

        while (pos < lineLength && !is_whitespace(u32str_get(line, pos))) {
            pos++;
        }

        while (pos < lineLength && is_whitespace(u32str_get(line, pos))) {
            pos++;
        }

        doc_set_cursor(view->target, cursor_row, pos);
    }

    document_view_ensure_cursor_visible(view);
}

void document_view_move_to_line_start(document_view* view, bool extend_selection) {
    if (!extend_selection && doc_has_selection(view->target)) {
        doc_set_has_selection(view->target, false);
    }

    if (extend_selection && !doc_has_selection(view->target)) {
        document_cursor cursor = doc_get_cursor(view->target);
        doc_set_selection_anchor(view->target, cursor.row, cursor.column);
        doc_set_has_selection(view->target, true);
    }

    document_cursor cursor = doc_get_cursor(view->target);
    doc_set_cursor(view->target, cursor.row, 0);
    document_view_ensure_cursor_visible(view);
}

void document_view_move_to_line_end(document_view* view, bool extend_selection) {
    if (!extend_selection && doc_has_selection(view->target)) {
        doc_set_has_selection(view->target, false);
    }

    if (extend_selection && !doc_has_selection(view->target)) {
        document_cursor cursor = doc_get_cursor(view->target);
        doc_set_selection_anchor(view->target, cursor.row, cursor.column);
        doc_set_has_selection(view->target, true);
    }

    document_cursor cursor = doc_get_cursor(view->target);
    doc_set_cursor(view->target, cursor.row, doc_get_line_length(view->target, cursor.row));
    document_view_ensure_cursor_visible(view);
}

void document_view_ensure_cursor_visible(document_view* view) {
    f32 charWidth = font_get_char_width(view->fnt, 'x');
    f32 lineHeight = font_get_line_height(view->fnt);

    // Get visual column position for cursor
    document_cursor cursor = doc_get_cursor(view->target);
    u32 cursor_row = cursor.row;
    u32 cursor_col = cursor.column;
    u32_string* line = doc_get_line(view->target, cursor_row);
    u32 visualCol = 0;
    if (line) {
        visualCol = get_visual_column(line, cursor_col, view->tabWidth);
    }

    f32 cursorX = visualCol * charWidth;
    f32 cursorY = cursor_row * lineHeight;

    f32 viewWidth = view->displayAreaW;
    f32 viewHeight = view->displayAreaH;

    if (view->showLineNumbers) {
        viewWidth -= view->lineNumberWidth;
    }

    if (view->maxScrollX > 0) viewHeight -= CARROT_SCROLLBAR_WIDTH;
    if (view->maxScrollY > 0) viewWidth -= CARROT_SCROLLBAR_WIDTH;

    if (cursorX < view->scrollX) {
        view->scrollX = cursorX;
    } else if (cursorX + charWidth > view->scrollX + viewWidth) {
        view->scrollX = cursorX + charWidth - viewWidth;
    }

    if (cursorY < view->scrollY) {
        view->scrollY = cursorY;
    } else if (cursorY + lineHeight > view->scrollY + viewHeight) {
        view->scrollY = cursorY + lineHeight - viewHeight;
    }

    view->scrollX = std::max(0.0f, std::min(view->scrollX, view->maxScrollX));
    view->scrollY = std::max(0.0f, std::min(view->scrollY, view->maxScrollY));
}

void document_view_scroll_to(document_view* view, f32 x, f32 y) {
    view->scrollX = std::max(0.0f, std::min(x, view->maxScrollX));
    view->scrollY = std::max(0.0f, std::min(y, view->maxScrollY));
}

void document_view_center_cursor(document_view* view) {
    f32 lineHeight = font_get_line_height(view->fnt);
    document_cursor cursor = doc_get_cursor(view->target);
    f32 cursorY = cursor.row * lineHeight;
    f32 viewHeight = view->displayAreaH;

    if (view->maxScrollY > 0) viewHeight -= CARROT_SCROLLBAR_WIDTH;

    view->scrollY = cursorY - viewHeight / 2;
    view->scrollY = std::max(0.0f, std::min(view->scrollY, view->maxScrollY));
}

u8* document_view_save_utf32(document_view* view, u32* out_size) {
    u32 totalSize = 0;
    u32 lineCount = doc_line_count(view->target);

    for (u32 i = 0; i < lineCount; i++) {
        totalSize += doc_get_line_length(view->target, i);
        if (i < lineCount - 1) totalSize++;
    }

    u32* buffer = (u32*)malloc(totalSize * sizeof(u32));
    u32 pos = 0;

    for (u32 i = 0; i < lineCount; i++) {
        u32_string* line = doc_get_line(view->target, i);
        if (line) {
            u32 lineLength = u32str_length(line);
            for (u32 j = 0; j < lineLength; j++) {
                buffer[pos++] = u32str_get(line, j);
            }
        }

        if (i < lineCount - 1) {
            buffer[pos++] = '\n';
        }
    }

    doc_set_modified(view->target, false);
    *out_size = totalSize * sizeof(u32);
    return (u8*)buffer;
}

u8* document_view_save_ascii(document_view* view, u32* out_size) {
    u32 totalSize = 0;
    u32 lineCount = doc_line_count(view->target);

    for (u32 i = 0; i < lineCount; i++) {
        totalSize += doc_get_line_length(view->target, i);
        if (i < lineCount - 1) totalSize++;
    }

    u8* buffer = (u8*)malloc(totalSize);
    u32 pos = 0;

    for (u32 i = 0; i < lineCount; i++) {
        u32_string* line = doc_get_line(view->target, i);
        if (line) {
            u32 lineLength = u32str_length(line);
            for (u32 j = 0; j < lineLength; j++) {
                u32 ch = u32str_get(line, j);
                buffer[pos++] = ch < 128 ? (u8)ch : '?';
            }
        }

        if (i < lineCount - 1) {
            buffer[pos++] = '\n';
        }
    }

    doc_set_modified(view->target, false);
    *out_size = totalSize;
    return buffer;
}

void document_view_undo(document_view* view) {
    doc_undo(view->target);
    document_view_update_cursor_after_undo_redo(view);
}

void document_view_redo(document_view* view) {
    doc_redo(view->target);
    document_view_update_cursor_after_undo_redo(view);
}

void document_view_validate_cursor_and_selection(document_view* view) {
    if (!view || !view->target) return;

    // Validate cursor
    doc_validate_cursor(view->target);

    // Validate selection anchor if selection exists
    if (doc_has_selection(view->target)) {
        u32 line_count = doc_line_count(view->target);

        // Selection anchor validation is handled by the document
        document_cursor anchor = doc_get_selection_anchor(view->target);
        if (anchor.row >= line_count) {
            doc_set_selection_anchor(view->target, line_count > 0 ? line_count - 1 : 0, anchor.column);
        }
    }
}

void document_view_update_cursor_after_undo_redo(document_view* view) {
    if (!view || !view->target) return;

    // Clear any existing selection
    document_view_clear_selection(view);

    // Try to get the last edit position from the document
    u32 edit_line, edit_col;
    if (doc_get_last_edit_position(view->target, &edit_line, &edit_col)) {
        // Move cursor to the edit position
        doc_set_cursor(view->target, edit_line, edit_col);

        // Clear the position so it doesn't affect other views
        doc_clear_last_edit_position(view->target);
    }

    // Ensure cursor is within valid bounds
    doc_validate_cursor(view->target);

    // Make sure the cursor is visible
    document_view_ensure_cursor_visible(view);
}

document_cursor document_view_pixel_to_cursor(document_view* view, u32 x, u32 y) {
    document_cursor result;
    f32 lineHeight = font_get_line_height(view->fnt);
    f32 charWidth = font_get_char_width(view->fnt, 'x');

    result.row = (u32)((y + view->scrollY) / lineHeight);

    u32 lineCount = doc_line_count(view->target);
    if (result.row >= lineCount) {
        result.row = lineCount > 0 ? lineCount - 1 : 0;
    }

    // Convert pixel X position to visual column, then to character index
    u32 visualCol = (u32)((x + view->scrollX) / charWidth);
    u32_string* line = doc_get_line(view->target, result.row);
    if (line) {
        result.column = visual_to_char_index(line, visualCol, view->tabWidth);
    } else {
        result.column = 0;
    }

    u32 lineLength = doc_get_line_length(view->target, result.row);
    if (result.column > lineLength) {
        result.column = lineLength;
    }

    return result;
}

void document_view_get_cursor_pixel_position(document_view* view, document_cursor cursor, u32* x, u32* y) {
    f32 charWidth = font_get_char_width(view->fnt, 'x');
    f32 lineHeight = font_get_line_height(view->fnt);

    // Get visual column position accounting for tabs
    u32_string* line = doc_get_line(view->target, cursor.row);
    u32 visualCol = 0;
    if (line) {
        visualCol = get_visual_column(line, cursor.column, view->tabWidth);
    }

    *x = (u32)(visualCol * charWidth - view->scrollX);
    *y = (u32)(cursor.row * lineHeight - view->scrollY);

    if (view->showLineNumbers) {
        *x += view->lineNumberWidth;
    }
}
