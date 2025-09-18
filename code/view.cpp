#include "view.h"
#include "document.h"
#include "strings.h"
#include "renderer.h"
#include "imgui.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

// Memory allocation functions
inline void* allocate_memory(size_t size) {
    return malloc(size);
}

inline void free_memory(void* ptr) {
    free(ptr);
}


// Use X11 keysym values instead of Windows VK codes
#define VK_BACK 0xff08     // XK_BackSpace
#define VK_TAB 0xff09      // XK_Tab
#define VK_RETURN 0xff0d   // XK_Return
#define VK_DELETE 0xffff   // XK_Delete
#define VK_LEFT 0xff51     // XK_Left
#define VK_UP 0xff52       // XK_Up
#define VK_RIGHT 0xff53    // XK_Right
#define VK_DOWN 0xff54     // XK_Down
#define VK_HOME 0xff50     // XK_Home
#define VK_END 0xff57      // XK_End

static const u32 DOUBLE_CLICK_TIME = 500;
static const u32 TRIPLE_CLICK_TIME = 500;
// Font metrics are now queried dynamically from the font object
static const f32 SCROLLBAR_WIDTH = 30.0f;

document_view* document_view_create(document* doc, font* fnt, u32_string* path) {
    document_view* view = (document_view*)allocate_memory(sizeof(document_view));

    view->target = doc;
    view->fnt = fnt;
    view->path = path ? u32str_substr(path, 0, u32str_length(path)) : nullptr;

    view->scrollX = 0;
    view->scrollY = 0;
    view->maxScrollX = 0;
    view->maxScrollY = 0;

    view->displayAreaX = 0;
    view->displayAreaY = 0;
    view->displayAreaW = 800;
    view->displayAreaH = 600;

    view->cursor.row = 0;
    view->cursor.column = 0;
    view->selectionAnchor.row = 0;
    view->selectionAnchor.column = 0;
    view->hasSelection = false;

    view->showLineNumbers = true;
    view->highlightSyntax = false;  // Default to no syntax highlighting
    // Calculate line number width based on font metrics (space for 5 digits + padding)
    view->lineNumberWidth = font_get_char_width(fnt, '0') * 6 + 10;  // 5 digits + space + padding
    view->tabWidth = 4;

    view->lastClickTime = 0;
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
    free_memory(view);
}

void document_view_update_font(document_view* view, font* fnt) {
    view->fnt = fnt;
    // Recalculate line number width for new font
    view->lineNumberWidth = font_get_char_width(fnt, '0') * 6 + 10;  // 5 digits + space + padding
}

// Helper function to calculate visual column position accounting for tabs
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

// Helper function to convert visual column to character index
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

static void clamp_cursor(document_view* view) {
    u32 lineCount = doc_line_count(view->target);
    if (view->cursor.row >= lineCount) {
        view->cursor.row = lineCount > 0 ? lineCount - 1 : 0;
    }

    u32 lineLength = doc_get_line_length(view->target, view->cursor.row);
    if (view->cursor.column > lineLength) {
        view->cursor.column = lineLength;
    }
}

static void normalize_selection(document_view* view, document_cursor* start, document_cursor* end) {
    if (view->cursor.row < view->selectionAnchor.row ||
        (view->cursor.row == view->selectionAnchor.row && view->cursor.column < view->selectionAnchor.column)) {
        *start = view->cursor;
        *end = view->selectionAnchor;
    } else {
        *start = view->selectionAnchor;
        *end = view->cursor;
    }
}

void document_view_keyboard_input(document_view* view, u32 unicode, u32 virtualKey,
                                 bool isDown, bool alt, bool ctrl, bool shift) {
    if (!isDown) return;

    if (ctrl) {
        switch (unicode) {
            case 'a':
            case 'A':
                document_view_select_all(view);
                return;
            case 'c':
            case 'C':
                document_view_copy(view);
                return;
            case 'x':
            case 'X':
                document_view_cut(view);
                return;
            case 'v':
            case 'V':
                return;
            case 'z':
            case 'Z':
                if (shift) {
                    document_view_redo(view);
                } else {
                    document_view_undo(view);
                }
                return;
            case 'y':
            case 'Y':
                document_view_redo(view);
                return;
        }
    }

    switch (virtualKey) {
        case VK_LEFT:
            if (ctrl) {
                document_view_move_word_left(view, shift);
            } else {
                document_view_move_cursor(view, 0, -1, shift);
            }
            break;

        case VK_RIGHT:
            if (ctrl) {
                document_view_move_word_right(view, shift);
            } else {
                document_view_move_cursor(view, 0, 1, shift);
            }
            break;

        case VK_UP:
            document_view_move_cursor(view, -1, 0, shift);
            break;

        case VK_DOWN:
            document_view_move_cursor(view, 1, 0, shift);
            break;

        case VK_HOME:
            document_view_move_to_line_start(view, shift);
            break;

        case VK_END:
            document_view_move_to_line_end(view, shift);
            break;

        case VK_BACK:
            document_view_delete_backward(view);
            break;

        case VK_DELETE:
            document_view_delete_forward(view);
            break;

        case VK_RETURN:
            {
                u32 newline = '\n';
                document_view_insert_text(view, &newline, 1);
            }
            break;

        case VK_TAB:
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

    view->selectionAnchor.row = cursor.row;
    view->selectionAnchor.column = wordStart;
    view->cursor.row = cursor.row;
    view->cursor.column = wordEnd;
    view->hasSelection = true;
}

static void select_line(document_view* view, u32 row) {
    view->selectionAnchor.row = row;
    view->selectionAnchor.column = 0;
    view->cursor.row = row;
    view->cursor.column = doc_get_line_length(view->target, row);
    view->hasSelection = true;
}

void document_view_mouse_input(document_view* view, u32 x, u32 y,
                              f32 scrollDelta, bool leftDown, bool middleDown, bool rightDown) {
    static bool wasLeftDown = false;
    static bool wasRightDown = false;

    // Check if mouse is over scrollbar areas
    f32 viewWidth = view->displayAreaW;
    f32 viewHeight = view->displayAreaH;

    if (view->showLineNumbers) {
        viewWidth -= view->lineNumberWidth;
    }

    bool needsHScroll = view->maxScrollX > 0;
    bool needsVScroll = view->maxScrollY > 0;

    if (needsHScroll) viewHeight -= SCROLLBAR_WIDTH;
    if (needsVScroll) viewWidth -= SCROLLBAR_WIDTH;

    // Check if mouse is in vertical scrollbar area
    bool inVScrollBar = false;
    if (needsVScroll) {
        u32 scrollbarX = view->displayAreaX + view->displayAreaW - (u32)SCROLLBAR_WIDTH;
        if (x >= scrollbarX && x < view->displayAreaX + view->displayAreaW &&
            y >= view->displayAreaY && y < view->displayAreaY + view->displayAreaH) {
            inVScrollBar = true;
        }
    }

    // Check if mouse is in horizontal scrollbar area
    bool inHScrollBar = false;
    if (needsHScroll) {
        u32 scrollbarY = view->displayAreaY + view->displayAreaH - (u32)SCROLLBAR_WIDTH;
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
        u32 currentTime = 0;

        if (clickedCursor.row == view->lastClickPosition.row &&
            clickedCursor.column == view->lastClickPosition.column &&
            currentTime - view->lastClickTime < DOUBLE_CLICK_TIME) {

            view->clickCount++;

            if (view->clickCount == 2) {
                select_word_at_cursor(view, clickedCursor);
            } else if (view->clickCount >= 3) {
                select_line(view, clickedCursor.row);
            }
        } else {
            view->clickCount = 1;
            view->cursor = clickedCursor;
            view->selectionAnchor = clickedCursor;
            view->hasSelection = false;
        }

        view->lastClickTime = currentTime;
        view->lastClickPosition = clickedCursor;
        clamp_cursor(view);
    }

    if (rightDown && !wasRightDown) {
        bool insideSelection = false;

        if (view->hasSelection) {
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
            view->cursor = clickedCursor;
            view->hasSelection = false;
            clamp_cursor(view);
        }

        // TODO: Show context menu
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

    if (needsHScroll) viewHeight -= SCROLLBAR_WIDTH;
    if (needsVScroll) viewWidth -= SCROLLBAR_WIDTH;

    // Check if mouse is in vertical scrollbar area
    bool inVScrollBar = false;
    if (needsVScroll) {
        u32 scrollbarX = view->displayAreaX + view->displayAreaW - (u32)SCROLLBAR_WIDTH;
        if (x >= scrollbarX && x < view->displayAreaX + view->displayAreaW &&
            y >= view->displayAreaY && y < view->displayAreaY + view->displayAreaH) {
            inVScrollBar = true;
        }
    }

    // Check if mouse is in horizontal scrollbar area
    bool inHScrollBar = false;
    if (needsHScroll) {
        u32 scrollbarY = view->displayAreaY + view->displayAreaH - (u32)SCROLLBAR_WIDTH;
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
    view->cursor = newCursor;
    clamp_cursor(view);

    if (view->cursor.row != view->selectionAnchor.row ||
        view->cursor.column != view->selectionAnchor.column) {
        view->hasSelection = true;
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
        if (line) {
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

    bool needsHScroll = contentWidth > viewWidth - SCROLLBAR_WIDTH;
    bool needsVScroll = contentHeight > viewHeight - SCROLLBAR_WIDTH;

    if (needsHScroll) viewHeight -= SCROLLBAR_WIDTH;
    if (needsVScroll) viewWidth -= SCROLLBAR_WIDTH;

    view->maxScrollX = std::max(0.0f, contentWidth - viewWidth);
    view->maxScrollY = std::max(0.0f, contentHeight - viewHeight);

    view->scrollX = std::max(0.0f, std::min(view->scrollX, view->maxScrollX));
    view->scrollY = std::max(0.0f, std::min(view->scrollY, view->maxScrollY));
}

void document_view_render(document_view* view, struct ImGui* imgui_context, canvas* cnvs, font* fnt, bool showLineNumbers) {
    if (!cnvs || !fnt || !imgui_context) return;

    view->showLineNumbers = showLineNumbers;

    f32 viewWidth = view->displayAreaW;
    f32 viewHeight = view->displayAreaH;
    f32 contentStartX = view->displayAreaX;

    if (view->showLineNumbers) {
        viewWidth -= view->lineNumberWidth;
        contentStartX += view->lineNumberWidth;
    }

    bool needsHScroll = view->maxScrollX > 0;
    bool needsVScroll = view->maxScrollY > 0;

    if (needsHScroll) viewHeight -= SCROLLBAR_WIDTH;
    if (needsVScroll) viewWidth -= SCROLLBAR_WIDTH;

    u32 lineHeight = font_get_line_height(fnt);
    u32 charWidth = font_get_char_width(fnt, 'x');  // Use average char width
    u32 firstVisibleLine = (u32)(view->scrollY / lineHeight);
    u32 lastVisibleLine = (u32)((view->scrollY + viewHeight) / lineHeight) + 1;
    u32 lineCount = doc_line_count(view->target);
    lastVisibleLine = std::min(lastVisibleLine, lineCount);

    document_cursor selStart, selEnd;
    if (view->hasSelection) {
        normalize_selection(view, &selStart, &selEnd);
    }

    // Draw line numbers first (before setting clip rect)
    if (view->showLineNumbers) {
        for (u32 lineIdx = firstVisibleLine; lineIdx < lastVisibleLine; lineIdx++) {
            f32 yPos = view->displayAreaY + (lineIdx * lineHeight) - view->scrollY;
            char lineNumStr[16];
            sprintf(lineNumStr, "%5u ", lineIdx + 1);  // 5 digits + space
            canvas_draw_text_cstr(cnvs, fnt, lineNumStr, view->displayAreaX + 5, (u32)yPos, 128, 128, 128);
        }
    }

    // Now set clip rect for content area only
    canvas_set_clip(cnvs, (u32)contentStartX, view->displayAreaY,
                    (u32)viewWidth, (u32)viewHeight);

    for (u32 lineIdx = firstVisibleLine; lineIdx < lastVisibleLine; lineIdx++) {
        f32 yPos = view->displayAreaY + (lineIdx * lineHeight) - view->scrollY;

        u32_string* line = doc_get_line(view->target, lineIdx);
        if (!line) continue;

        u32 lineLength = u32str_length(line);

        u32 firstVisibleChar = (u32)(view->scrollX / charWidth);
        u32 lastVisibleChar = (u32)((view->scrollX + viewWidth) / charWidth) + 1;
        lastVisibleChar = std::min(lastVisibleChar, lineLength);

        // Draw selection background
        if (view->hasSelection) {
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
                    canvas_draw_rect(cnvs, selX1, (u32)yPos, selX2 - selX1, lineHeight, 64, 64, 128);
                }
            }
        }

        // Draw text - need to track visual column position for tabs
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
                        canvas_draw_text_cstr(cnvs, fnt, space, xPos + i * charWidth, (u32)yPos, 255, 255, 255);
                    }
                } else if (ch >= 32 && ch < 127) {
                    char str[2] = {(char)ch, '\0'};
                    canvas_draw_text_cstr(cnvs, fnt, str, xPos, (u32)yPos, 255, 255, 255);
                }
            }

            // Update visual column
            if (ch == '\t') {
                visualCol = ((visualCol / view->tabWidth) + 1) * view->tabWidth;
            } else {
                visualCol++;
            }
        }

        // Draw cursor
        if (view->cursor.row == lineIdx) {
            u32 cursorVisualCol = get_visual_column(line, view->cursor.column, view->tabWidth);
            u32 cursorX = (u32)(contentStartX + (cursorVisualCol * charWidth) - view->scrollX);
            canvas_draw_rect(cnvs, cursorX, (u32)yPos, 2, lineHeight, 255, 255, 255);
        }
    }

    // Reset clip rect
    canvas_set_clip(cnvs, 0, 0, canvas_get_width(cnvs), canvas_get_height(cnvs));

    // Draw interactive scrollbars using ImGui extended functions
    // Vertical scrollbar goes all the way to the bottom
    if (needsVScroll) {
        u32 scrollbarX = view->displayAreaX + view->displayAreaW - (u32)SCROLLBAR_WIDTH;
        u32 scrollbarHeight = view->displayAreaH;  // Full height, no reduction

        bool scrollChanged = false;
        f32 newScrollY = ImGuiVerticalScrollBarEx(imgui_context,
                                                  scrollbarX, view->displayAreaY,
                                                  (u32)SCROLLBAR_WIDTH, scrollbarHeight,
                                                  view->scrollY, viewHeight, viewHeight + view->maxScrollY,
                                                  &scrollChanged);
        if (scrollChanged) {
            view->scrollY = newScrollY;
        }
    }

    // Horizontal scrollbar is shortened to avoid overlap with vertical scrollbar
    if (needsHScroll) {
        u32 scrollbarY = view->displayAreaY + view->displayAreaH - (u32)SCROLLBAR_WIDTH;
        u32 scrollbarWidth = view->displayAreaW;
        if (needsVScroll) scrollbarWidth -= (u32)SCROLLBAR_WIDTH;  // Shorten to avoid overlap

        bool scrollChanged = false;
        f32 newScrollX = ImGuiHorizontalScrollBarEx(imgui_context,
                                                    view->displayAreaX, scrollbarY,
                                                    scrollbarWidth, (u32)SCROLLBAR_WIDTH,
                                                    view->scrollX, viewWidth, viewWidth + view->maxScrollX,
                                                    &scrollChanged);
        if (scrollChanged) {
            view->scrollX = newScrollX;
        }
    }
}

void document_view_set_cursor(document_view* view, u32 row, u32 column) {
    view->cursor.row = row;
    view->cursor.column = column;
    clamp_cursor(view);
    view->hasSelection = false;
}

void document_view_set_highlight_syntax(document_view* view, bool highlight) {
    view->highlightSyntax = highlight;
}

bool document_view_get_highlight_syntax(document_view* view) {
    return view->highlightSyntax;
}

void document_view_select_all(document_view* view) {
    view->selectionAnchor.row = 0;
    view->selectionAnchor.column = 0;

    u32 lastLine = doc_line_count(view->target);
    if (lastLine > 0) {
        view->cursor.row = lastLine - 1;
        view->cursor.column = doc_get_line_length(view->target, lastLine - 1);
    } else {
        view->cursor.row = 0;
        view->cursor.column = 0;
    }

    view->hasSelection = true;
}

void document_view_clear_selection(document_view* view) {
    view->hasSelection = false;
}

bool document_view_has_selection(document_view* view) {
    return view->hasSelection;
}

u32_string* document_view_get_selection(document_view* view) {
    if (!view->hasSelection) return nullptr;

    document_cursor start, end;
    normalize_selection(view, &start, &end);

    u32_string* result = u32str_create();

    if (start.row == end.row) {
        u32_string* line = doc_get_line(view->target, start.row);
        if (line && end.column <= u32str_length(line)) {
            u32_string* substr = u32str_substr(line, start.column, end.column - start.column);
            if (substr) {
                u32str_insert(result, substr, u32str_length(result), 0, u32str_length(substr));
                u32str_destroy(substr);
            }
        }
    } else {
        // First line
        u32_string* line = doc_get_line(view->target, start.row);
        if (line) {
            u32 lineLength = u32str_length(line);
            if (start.column < lineLength) {
                u32_string* substr = u32str_substr(line, start.column, lineLength - start.column);
                if (substr) {
                    u32str_insert(result, substr, u32str_length(result), 0, u32str_length(substr));
                    u32str_destroy(substr);
                }
            }
            u32 newline = '\n';
            // Add newline - need to use a temporary string
            u32* nl_data = (u32*)allocate_memory(2 * sizeof(u32));
            nl_data[0] = newline;
            nl_data[1] = 0;
            u32_string* nl_str = u32str_init(nl_data);
            u32str_insert(result, nl_str, u32str_length(result), 0, 1);
            u32str_destroy(nl_str);
            free_memory(nl_data);
        }

        // Middle lines
        for (u32 row = start.row + 1; row < end.row; row++) {
            line = doc_get_line(view->target, row);
            if (line) {
                u32str_insert(result, line, u32str_length(result), 0, u32str_length(line));
                u32 newline = '\n';
                // Add newline - need to use a temporary string
            u32* nl_data = (u32*)allocate_memory(2 * sizeof(u32));
            nl_data[0] = newline;
            nl_data[1] = 0;
            u32_string* nl_str = u32str_init(nl_data);
            u32str_insert(result, nl_str, u32str_length(result), 0, 1);
            u32str_destroy(nl_str);
            free_memory(nl_data);
            }
        }

        // Last line
        line = doc_get_line(view->target, end.row);
        if (line && end.column <= u32str_length(line)) {
            u32_string* substr = u32str_substr(line, 0, end.column);
            if (substr) {
                u32str_insert(result, substr, u32str_length(result), 0, u32str_length(substr));
                u32str_destroy(substr);
            }
        }
    }

    return result;
}

void document_view_insert_text(document_view* view, const u32* text, u32 length) {
    if (view->hasSelection) {
        document_view_delete_selection(view);
    }

    for (u32 i = 0; i < length; i++) {
        if (text[i] == '\n') {
            doc_split_line(view->target, view->cursor.row, view->cursor.column);
            view->cursor.row++;
            view->cursor.column = 0;
        } else {
            doc_insert_char(view->target, view->cursor.row, view->cursor.column, text[i]);
            view->cursor.column++;
        }
    }

    clamp_cursor(view);
    document_view_ensure_cursor_visible(view);
}

void document_view_delete_selection(document_view* view) {
    if (!view->hasSelection) return;

    document_cursor start, end;
    normalize_selection(view, &start, &end);

    if (start.row == end.row) {
        doc_delete_range(view->target, start.row, start.column, end.row, end.column);
    } else {
        for (u32 row = end.row; row > start.row; row--) {
            if (row == end.row) {
                doc_delete_range(view->target, row, 0, row, end.column);
                doc_delete_line(view->target, row);
            } else {
                doc_delete_line(view->target, row);
            }
        }

        u32 remainingLength = doc_get_line_length(view->target, start.row);
        if (start.column < remainingLength) {
            doc_delete_range(view->target, start.row, start.column, start.row, remainingLength);
        }

        if (start.row + 1 < doc_line_count(view->target)) {
            doc_join_lines(view->target, start.row);
        }
    }

    view->cursor = start;
    view->hasSelection = false;
    clamp_cursor(view);
}

void document_view_delete_forward(document_view* view) {
    if (view->hasSelection) {
        document_view_delete_selection(view);
        return;
    }

    u32 lineLength = doc_get_line_length(view->target, view->cursor.row);

    if (view->cursor.column < lineLength) {
        doc_delete_range(view->target, view->cursor.row, view->cursor.column, view->cursor.row, view->cursor.column + 1);
    } else if (view->cursor.row + 1 < doc_line_count(view->target)) {
        doc_join_lines(view->target, view->cursor.row);
    }
}

void document_view_delete_backward(document_view* view) {
    if (view->hasSelection) {
        document_view_delete_selection(view);
        return;
    }

    if (view->cursor.column > 0) {
        view->cursor.column--;
        doc_delete_range(view->target, view->cursor.row, view->cursor.column, view->cursor.row, view->cursor.column + 1);
    } else if (view->cursor.row > 0) {
        view->cursor.row--;
        view->cursor.column = doc_get_line_length(view->target, view->cursor.row);
        doc_join_lines(view->target, view->cursor.row);
    }
}

void document_view_move_cursor(document_view* view, i32 rowDelta, i32 colDelta, bool extend_selection) {
    if (!extend_selection && view->hasSelection) {
        view->hasSelection = false;
    }

    if (extend_selection && !view->hasSelection) {
        view->selectionAnchor = view->cursor;
        view->hasSelection = true;
    }

    if (rowDelta != 0) {
        i32 newRow = (i32)view->cursor.row + rowDelta;
        if (newRow < 0) newRow = 0;
        view->cursor.row = (u32)newRow;
    }

    if (colDelta != 0) {
        i32 newCol = (i32)view->cursor.column + colDelta;
        if (newCol < 0) {
            if (view->cursor.row > 0) {
                view->cursor.row--;
                view->cursor.column = doc_get_line_length(view->target, view->cursor.row);
            } else {
                view->cursor.column = 0;
            }
        } else {
            u32 lineLength = doc_get_line_length(view->target, view->cursor.row);
            if ((u32)newCol > lineLength) {
                if (view->cursor.row + 1 < doc_line_count(view->target)) {
                    view->cursor.row++;
                    view->cursor.column = 0;
                } else {
                    view->cursor.column = lineLength;
                }
            } else {
                view->cursor.column = (u32)newCol;
            }
        }
    }

    clamp_cursor(view);
    document_view_ensure_cursor_visible(view);
}

void document_view_move_word_left(document_view* view, bool extend_selection) {
    if (!extend_selection && view->hasSelection) {
        document_cursor start, end;
        normalize_selection(view, &start, &end);
        view->cursor = start;
        view->hasSelection = false;
        return;
    }

    if (extend_selection && !view->hasSelection) {
        view->selectionAnchor = view->cursor;
        view->hasSelection = true;
    }

    u32_string* line = doc_get_line(view->target, view->cursor.row);

    if (view->cursor.column == 0) {
        if (view->cursor.row > 0) {
            view->cursor.row--;
            view->cursor.column = doc_get_line_length(view->target, view->cursor.row);
        }
    } else if (line) {
        u32 pos = view->cursor.column - 1;

        while (pos > 0 && is_whitespace(u32str_get(line, pos))) {
            pos--;
        }

        while (pos > 0 && !is_whitespace(u32str_get(line, pos - 1))) {
            pos--;
        }

        view->cursor.column = pos;
    }

    document_view_ensure_cursor_visible(view);
}

void document_view_move_word_right(document_view* view, bool extend_selection) {
    if (!extend_selection && view->hasSelection) {
        document_cursor start, end;
        normalize_selection(view, &start, &end);
        view->cursor = end;
        view->hasSelection = false;
        return;
    }

    if (extend_selection && !view->hasSelection) {
        view->selectionAnchor = view->cursor;
        view->hasSelection = true;
    }

    u32_string* line = doc_get_line(view->target, view->cursor.row);
    u32 lineLength = line ? u32str_length(line) : 0;

    if (view->cursor.column >= lineLength) {
        if (view->cursor.row + 1 < doc_line_count(view->target)) {
            view->cursor.row++;
            view->cursor.column = 0;
        }
    } else if (line) {
        u32 pos = view->cursor.column;

        while (pos < lineLength && !is_whitespace(u32str_get(line, pos))) {
            pos++;
        }

        while (pos < lineLength && is_whitespace(u32str_get(line, pos))) {
            pos++;
        }

        view->cursor.column = pos;
    }

    document_view_ensure_cursor_visible(view);
}

void document_view_move_to_line_start(document_view* view, bool extend_selection) {
    if (!extend_selection && view->hasSelection) {
        view->hasSelection = false;
    }

    if (extend_selection && !view->hasSelection) {
        view->selectionAnchor = view->cursor;
        view->hasSelection = true;
    }

    view->cursor.column = 0;
    document_view_ensure_cursor_visible(view);
}

void document_view_move_to_line_end(document_view* view, bool extend_selection) {
    if (!extend_selection && view->hasSelection) {
        view->hasSelection = false;
    }

    if (extend_selection && !view->hasSelection) {
        view->selectionAnchor = view->cursor;
        view->hasSelection = true;
    }

    view->cursor.column = doc_get_line_length(view->target, view->cursor.row);
    document_view_ensure_cursor_visible(view);
}

void document_view_ensure_cursor_visible(document_view* view) {
    f32 charWidth = font_get_char_width(view->fnt, 'x');
    f32 lineHeight = font_get_line_height(view->fnt);

    // Get visual column position for cursor
    u32_string* line = doc_get_line(view->target, view->cursor.row);
    u32 visualCol = 0;
    if (line) {
        visualCol = get_visual_column(line, view->cursor.column, view->tabWidth);
    }

    f32 cursorX = visualCol * charWidth;
    f32 cursorY = view->cursor.row * lineHeight;

    f32 viewWidth = view->displayAreaW;
    f32 viewHeight = view->displayAreaH;

    if (view->showLineNumbers) {
        viewWidth -= view->lineNumberWidth;
    }

    if (view->maxScrollX > 0) viewHeight -= SCROLLBAR_WIDTH;
    if (view->maxScrollY > 0) viewWidth -= SCROLLBAR_WIDTH;

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
    f32 cursorY = view->cursor.row * lineHeight;
    f32 viewHeight = view->displayAreaH;

    if (view->maxScrollY > 0) viewHeight -= SCROLLBAR_WIDTH;

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

    u32* buffer = (u32*)allocate_memory(totalSize * sizeof(u32));
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

    u8* buffer = (u8*)allocate_memory(totalSize);
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

void document_view_copy(document_view* view) {
    if (!view->hasSelection) return;

    u32_string* selection = document_view_get_selection(view);
    if (selection) {
        printf("Copy to clipboard: %d characters\n", u32str_length(selection));
        u32str_destroy(selection);
    }
}

void document_view_cut(document_view* view) {
    if (!view->hasSelection) return;

    document_view_copy(view);
    document_view_delete_selection(view);
}

void document_view_paste(document_view* view, const u32* text, u32 length) {
    document_view_insert_text(view, text, length);
}

void document_view_undo(document_view* view) {
    doc_undo(view->target);
    clamp_cursor(view);
}

void document_view_redo(document_view* view) {
    doc_redo(view->target);
    clamp_cursor(view);
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