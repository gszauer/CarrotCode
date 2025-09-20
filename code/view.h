#ifndef _H_VIEW__CARROT_
#define _H_VIEW__CARROT_ 

#include "types.h"

struct document;
struct u32_string;
struct document_cursor;
struct document_view;

document_view* document_view_create(document* doc, font* fnt, u32_string* path);
void document_view_update_font(document_view* view, font* fnt);
void document_view_destroy(document_view* view);

document* document_view_get_document(document_view* view);
u32 document_view_get_cursor_row(document_view* view);
void document_view_set_cursor_row(document_view* view, u32 value);
u32 document_view_get_cursor_col(document_view* view);
void document_view_set_cursor_col(document_view* view, u32 value);

u32_string* document_view_get_path(document_view* view); // DO NOT MODIFY RETURN VALUE!
void document_view_clear_path(document_view* view);
void document_view_set_path(document_view* view, u32_string* path); // Creates a copy of path

void document_view_set_display_area(document_view* view, u32 x, u32 y, u32 w, u32 h);
void document_view_set_display_size(document_view* view, u32 w, u32 h);

void document_view_keyboard_input(document_view* view, u32 unicode, u32 virtualKey,
                                  bool isDown, bool alt, bool ctrl, bool shift);
void document_view_mouse_input(document_view* view, u32 x, u32 y,
                               f32 scrollDelta, bool leftDown, bool middleDown, bool rightDown);
void document_view_mouse_moved(document_view* view, u32 x, u32 y, bool leftDown);

void document_view_update(document_view* view, f32 deltaTime);
void document_view_render(document_view* view, struct ImGui* imgui_context, canvas* cnvs, font* fnt, bool showLineNumbers);

void document_view_set_cursor(document_view* view, u32 row, u32 column);
void document_view_set_highlight_syntax(document_view* view, bool highlight);
bool document_view_get_highlight_syntax(document_view* view);
void document_view_select_all(document_view* view);
void document_view_clear_selection(document_view* view);
bool document_view_has_selection(document_view* view);
u32_string* document_view_get_selection(document_view* view);

void document_view_insert_text(document_view* view, const u32* text, u32 length);
void document_view_delete_selection(document_view* view);
void document_view_delete_forward(document_view* view);
void document_view_delete_backward(document_view* view);

void document_view_move_cursor(document_view* view, i32 rowDelta, i32 colDelta, bool extend_selection);
void document_view_move_word_left(document_view* view, bool extend_selection);
void document_view_move_word_right(document_view* view, bool extend_selection);
void document_view_move_to_line_start(document_view* view, bool extend_selection);
void document_view_move_to_line_end(document_view* view, bool extend_selection);

void document_view_ensure_cursor_visible(document_view* view);
void document_view_scroll_to(document_view* view, f32 x, f32 y);
void document_view_center_cursor(document_view* view);

u8* document_view_save_utf32(document_view* view, u32* out_size);
u8* document_view_save_ascii(document_view* view, u32* out_size);

void document_view_copy(document_view* view);
void document_view_cut(document_view* view);
void document_view_paste(document_view* view, const u32* text, u32 length);

void document_view_undo(document_view* view);
void document_view_redo(document_view* view);

// Validate and fix cursor/selection after document changes
void document_view_validate_cursor_and_selection(document_view* view);
void document_view_clear_selection(document_view* view);
void document_view_update_cursor_after_undo_redo(document_view* view);

document_cursor document_view_pixel_to_cursor(document_view* view, u32 x, u32 y);
void document_view_get_cursor_pixel_position(document_view* view, document_cursor cursor, u32* x, u32* y);
#endif