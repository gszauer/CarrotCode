#include "document.h"
#include "strings.h"
#include "syntax.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

// Forward declaration for GetTimeInMilliseconds
extern long long GetTimeInMilliseconds();

/**
 * Simplified edit action for Lite-style undo system.
 * Only two operations: INSERT and REMOVE.
 */
struct edit_action {
    enum action_type {
        INSERT,    // Text was inserted
        REMOVE     // Text was removed
    } type;

    // Position where the action occurred
    u32 line;
    u32 col;

    // The text that was inserted or removed
    u32_string* text;

    // Timestamp for merging consecutive edits
    u64 timestamp;

    // Cursor state after the action
    document_cursor cursor_after;
    bool had_selection_after;
};

static void free_action_text(edit_action* action) {
    if (action && action->text) {
        u32str_destroy(action->text);
        action->text = nullptr;
    }
}

static void debug_print_text(const char* label, u32_string* text) {
    if (!label) label = "";
    if (!text) {
        printf("%s<null>\n", label);
        return;
    }

    u32 length = u32str_length(text);
    printf("%slen=%u \"", label, length);
    for (u32 i = 0; i < length; ++i) {
        u32 ch = u32str_get(text, i);
        if (ch == '\n') {
            printf("\\n");
        } else if (ch == '\r') {
            printf("\\r");
        } else if (ch == '\t') {
            printf("\\t");
        } else if (ch >= 32 && ch < 127) {
            putchar((char)ch);
        } else {
            printf("?%02X", ch & 0xFF);
        }
    }
    printf("\"\n");
}

static void compute_text_extent(u32 line, u32 col, u32_string* text,
                                u32* out_line, u32* out_col) {
    u32 current_line = line;
    u32 current_col = col;

    if (text) {
        u32 length = u32str_length(text);
        for (u32 i = 0; i < length; ++i) {
            u32 ch = u32str_get(text, i);
            if (ch == '\n') {
                current_line++;
                current_col = 0;
            } else {
                current_col++;
            }
        }
    }

    if (out_line) *out_line = current_line;
    if (out_col) *out_col = current_col;
}

/**
 * Main document structure that manages text content and edit history.
 * Handles both the text lines and the undo/redo system.
 */
struct document {
    // === Document Content ===
    vector_docline* lines;      // Dynamic array of text lines
    bool modified;               // True if document has unsaved changes

    // === Simplified Undo/Redo System (Lite-style) ===
    edit_action* undo_stack;    // Fixed-size array of undo operations
    edit_action* redo_stack;    // Fixed-size array of redo operations
    u32 undo_count;             // Current size of undo stack
    u32 redo_count;             // Current size of redo stack
    u32 max_undo_levels;        // Maximum number of undo levels
    bool in_undo_redo;          // Prevent recording during undo/redo
    u64 last_action_time;       // Timestamp of last action for merging

    // === Cursor and Selection ===
    document_cursor cursor;          // Current cursor position
    document_cursor selection_anchor; // Anchor point for selection
    bool has_selection;              // True if there's an active selection
};

static edit_action* undo_last(document* doc) {
    if (!doc || doc->undo_count == 0) return nullptr;
    return &doc->undo_stack[doc->undo_count - 1];
}

static void clear_stack(edit_action* stack, u32* count) {
    if (!stack || !count) return;
    for (u32 i = 0; i < *count; ++i) {
        free_action_text(&stack[i]);
        stack[i] = {};
    }
    *count = 0;
}

static void ensure_stack_capacity(document* doc) {
    if (!doc || doc->max_undo_levels == 0) return;
    if (!doc->undo_stack) {
        doc->undo_stack = (edit_action*)calloc(doc->max_undo_levels, sizeof(edit_action));
        doc->undo_count = 0;
    }
    if (!doc->redo_stack) {
        doc->redo_stack = (edit_action*)calloc(doc->max_undo_levels, sizeof(edit_action));
        doc->redo_count = 0;
    }
}

static void push_undo(document* doc, edit_action action) {
    if (!doc || doc->max_undo_levels == 0) {
        if (action.text) u32str_destroy(action.text);
        return;
    }

    ensure_stack_capacity(doc);

    if (doc->undo_count >= doc->max_undo_levels) {
        free_action_text(&doc->undo_stack[0]);
        if (doc->undo_count > 1) {
            memmove(&doc->undo_stack[0], &doc->undo_stack[1], sizeof(edit_action) * (doc->undo_count - 1));
        }
        doc->undo_count--;
        doc->undo_stack[doc->undo_count] = {};
    }

    doc->undo_stack[doc->undo_count++] = action;
}

static void push_redo(document* doc, edit_action action) {
    if (!doc || doc->max_undo_levels == 0) {
        if (action.text) u32str_destroy(action.text);
        return;
    }

    ensure_stack_capacity(doc);

    if (doc->redo_count >= doc->max_undo_levels) {
        free_action_text(&doc->redo_stack[0]);
        if (doc->redo_count > 1) {
            memmove(&doc->redo_stack[0], &doc->redo_stack[1], sizeof(edit_action) * (doc->redo_count - 1));
        }
        doc->redo_count--;
        doc->redo_stack[doc->redo_count] = {};
    }

    doc->redo_stack[doc->redo_count++] = action;
}

static edit_action pop_undo(document* doc) {
    edit_action empty = {};
    if (!doc || doc->undo_count == 0) return empty;
    doc->undo_count--;
    edit_action action = doc->undo_stack[doc->undo_count];
    doc->undo_stack[doc->undo_count] = {};
    return action;
}

static edit_action pop_redo(document* doc) {
    edit_action empty = {};
    if (!doc || doc->redo_count == 0) return empty;
    doc->redo_count--;
    edit_action action = doc->redo_stack[doc->redo_count];
    doc->redo_stack[doc->redo_count] = {};
    return action;
}

static void clear_redo_stack(document* doc) {
    if (!doc) return;
    clear_stack(doc->redo_stack, &doc->redo_count);
}

/**
 * Records an edit action for undo/redo.
 * Automatically merges consecutive edits if they're close in time.
 * Much simpler than the old system!
 */
static void record_action(document* doc, edit_action::action_type type, u32 line, u32 col, u32_string* text) {
    printf("[UNDO] record_action: type=%s, line=%u, col=%u, text_len=%u\n",
           type == edit_action::INSERT ? "INSERT" : "REMOVE",
           line, col, text ? u32str_length(text) : 0);

    if (!doc) {
        printf("[UNDO] ERROR: doc is NULL\n");
        if (text) u32str_destroy(text);
        return;
    }

    if (doc->max_undo_levels == 0 || doc->in_undo_redo) {
        printf("[UNDO] Skipping: max_undo_levels=%u, in_undo_redo=%d\n",
               doc->max_undo_levels, doc->in_undo_redo);
        doc->modified = true;
        if (text) u32str_destroy(text);
        return;
    }

    u64 current_time = (u64)GetTimeInMilliseconds();

    // Check if we should merge with the previous action
    ensure_stack_capacity(doc);

    edit_action* last = undo_last(doc);
    bool should_merge = false;
    printf("[UNDO] Undo stack size=%u, last action exists=%d\n", doc->undo_count, last != nullptr);

    if (last && last->type == type && !doc->has_selection) {
        // Merge if within 500ms and adjacent
        if (current_time - doc->last_action_time < 500) {
            if (type == edit_action::INSERT) {
                // Merge if inserting at the end of previous insertion
                u32 text_len = last->text ? u32str_length(last->text) : 0;
                should_merge = (line == last->line && col == last->col + text_len);
            } else {  // REMOVE
                // Merge if removing at the same position or one before
                should_merge = (line == last->line && (col == last->col || col == last->col - 1));
            }
        }
    }

    if (should_merge && last) {
        printf("[UNDO] Merging with previous action\n");
        // Merge with previous action
        if (type == edit_action::INSERT) {
            // Append text
            u32_string* combined = u32str_create();
            if (last->text) {
                u32str_insert(combined, last->text, 0, 0, u32str_length(last->text));
            }
            u32str_insert(combined, text, u32str_length(combined), 0, u32str_length(text));
            if (last->text) u32str_destroy(last->text);
            u32str_destroy(text);
            last->text = combined;
        } else {  // REMOVE
            // Prepend or append based on position
            u32_string* combined = u32str_create();
            if (col < last->col) {
                // Prepending (backspace)
                u32str_insert(combined, text, 0, 0, u32str_length(text));
                if (last->text) {
                    u32str_insert(combined, last->text, u32str_length(combined), 0, u32str_length(last->text));
                }
                last->col = col;
            } else {
                // Appending (delete)
                if (last->text) {
                    u32str_insert(combined, last->text, 0, 0, u32str_length(last->text));
                }
                u32str_insert(combined, text, u32str_length(combined), 0, u32str_length(text));
            }
            if (last->text) u32str_destroy(last->text);
            u32str_destroy(text);
            last->text = combined;
        }
        last->cursor_after = doc->cursor;
        last->timestamp = current_time;
    } else {
        printf("[UNDO] Adding new action (not merging)\n");
        // Clear redo stack when adding new action
        printf("[UNDO] Clearing redo stack (had %u items)\n", doc->redo_count);
        clear_redo_stack(doc);

        // Create new action
        edit_action action;
        action.type = type;
        action.line = line;
        action.col = col;
        action.text = text;
        action.timestamp = current_time;
        action.cursor_after = doc->cursor;
        action.had_selection_after = doc->has_selection;

        // Enforce max undo levels by removing oldest (at index 0)
        push_undo(doc, action);
        printf("[UNDO] Added action to undo stack, new size=%u\n", doc->undo_count);
        debug_print_text("[UNDO] Recorded text: ", action.text);
    }

    doc->last_action_time = current_time;
    doc->modified = true;
}

document* doc_create(u32 undo_levels) {
    document* doc = (document*)malloc(sizeof(document));
    doc->lines = vec_docline_create();
    doc->modified = false;
    doc->max_undo_levels = undo_levels;

    // Create dual stacks for undo/redo
    if (doc->max_undo_levels > 0) {
        doc->undo_stack = (edit_action*)calloc(doc->max_undo_levels, sizeof(edit_action));
        doc->redo_stack = (edit_action*)calloc(doc->max_undo_levels, sizeof(edit_action));
    } else {
        doc->undo_stack = nullptr;
        doc->redo_stack = nullptr;
    }
    doc->undo_count = 0;
    doc->redo_count = 0;
    doc->in_undo_redo = false;
    doc->last_action_time = 0;

    // Initialize cursor and selection
    doc->cursor.row = 0;
    doc->cursor.column = 0;
    doc->selection_anchor.row = 0;
    doc->selection_anchor.column = 0;
    doc->has_selection = false;

    return doc;
}

void doc_destroy(document* doc) {
    if (!doc) return;

    vec_docline_destroy(doc->lines);
    if (doc->undo_stack) {
        for (u32 i = 0; i < doc->max_undo_levels; ++i) {
            free_action_text(&doc->undo_stack[i]);
        }
        free(doc->undo_stack);
    }
    if (doc->redo_stack) {
        for (u32 i = 0; i < doc->max_undo_levels; ++i) {
            free_action_text(&doc->redo_stack[i]);
        }
        free(doc->redo_stack);
    }
    free(doc);
}

document* doc_from_str32(u32_string* content, u32 undo_levels) {
    if (!content) return doc_create(undo_levels);
    
    document* doc = doc_create(undo_levels);
    
    u32 start = 0;
    for (u32 i = 0; i <= u32str_length(content); i++) {
        if (i == u32str_length(content) || u32str_get(content, i) == '\n') {
            u32 length = i - start;
            u32_string* line_text = u32str_substr(content, start, length);
            document_line* line = docline_create_with_text(line_text);
            vec_docline_push(doc->lines, line);
            u32str_destroy(line_text);
            start = i + 1;
        }
    }
    
    return doc;
}

u32_string* doc_to_str32(document* doc) {
    if (!doc) return u32str_create();
    
    u32 total_length = 0;
    u32 line_count = vec_docline_size(doc->lines);
    
    for (u32 i = 0; i < line_count; i++) {
        document_line* line = vec_docline_get(doc->lines, i);
        total_length += docline_get_text_length(line);
        if (i < line_count - 1) {
            total_length++;
        }
    }
    
    // Allocate buffer for the entire content
    u32* buffer = (u32*)malloc((total_length + 1) * sizeof(u32));
    u32 pos = 0;

    for (u32 i = 0; i < line_count; i++) {
        document_line* line = vec_docline_get(doc->lines, i);
        u32 line_len = docline_get_text_length(line);

        // Copy line content
        for (u32 j = 0; j < line_len; j++) {
            buffer[pos++] = u32str_get(docline_access_text(line), j);
        }

        // Add newline after each line except the last
        if (i < line_count - 1) {
            buffer[pos++] = '\n';
        }
    }
    buffer[pos] = 0; // null terminator

    u32_string* result = u32str_init(buffer);
    free(buffer);
    
    return result;
}

u32 doc_line_count(document* doc) {
    if (!doc) return 0;
    return vec_docline_size(doc->lines);
}

u32 doc_get_line_length(document* doc, u32 line_index) {
    if (!doc || line_index >= vec_docline_size(doc->lines)) return 0;
    document_line* line = vec_docline_get(doc->lines, line_index);
    if (!line || !docline_access_text(line)) return 0;
    return docline_get_text_length(line);
}

bool doc_is_modified(document* doc) {
    return doc ? doc->modified : false;
}

void doc_set_modified(document* doc, bool modified) {
    if (doc) doc->modified = modified;
}

u32_string* doc_get_line(document* doc, u32 line_index) {
    if (!doc || line_index >= vec_docline_size(doc->lines)) return nullptr;
    document_line* line = vec_docline_get(doc->lines, line_index);
    return line ? docline_access_text(line) : nullptr;
}

u32_string* doc_get_range(document* doc, u32 start_line, u32 start_col, u32 end_line, u32 end_col) {
    if (!doc) return u32str_create();

    u32 line_count = vec_docline_size(doc->lines);
    if (start_line >= line_count) return u32str_create();

    if (end_line >= line_count) {
        end_line = line_count - 1;
        document_line* last_line = vec_docline_get(doc->lines, end_line);
        end_col = docline_get_text_length(last_line);
    }


    u32_string* result = u32str_create();
    
    if (start_line == end_line) {
        document_line* line = vec_docline_get(doc->lines, start_line);
        u32 length = end_col - start_col;
        u32_string* substr = docline_text_substr(line, start_col, length);
        u32str_insert(result, substr, 0, 0, u32str_length(substr));
        u32str_destroy(substr);
    } else {
        document_line* first_line = vec_docline_get(doc->lines, start_line);
        u32 first_length = docline_get_text_length(first_line) - start_col;
        u32_string* first_substr = docline_text_substr(first_line, start_col, first_length);
        u32str_insert(result, first_substr, 0, 0, u32str_length(first_substr));
        u32str_destroy(first_substr);
        
        // Insert newline character
        u32str_insert_char(result, u32str_length(result), '\n');
        
        for (u32 i = start_line + 1; i < end_line; i++) {
            document_line* line = vec_docline_get(doc->lines, i);
            u32str_insert(result, docline_access_text(line), u32str_length(result), 0, docline_get_text_length(line));
            
            // Insert newline after each middle line
            u32str_insert_char(result, u32str_length(result), '\n');
        }
        
        document_line* last_line = vec_docline_get(doc->lines, end_line);
        u32_string* last_substr = docline_text_substr(last_line, 0, end_col);
        u32str_insert(result, last_substr, u32str_length(result), 0, u32str_length(last_substr));
        u32str_destroy(last_substr);
    }
    
    return result;
}

// Removed word-building functions - no longer needed in simplified system

void doc_insert_char(document* doc, u32 line, u32 col, u32 codepoint) {
    printf("[EDIT] doc_insert_char: line=%u, col=%u, char=%c\n", line, col, codepoint < 128 ? (char)codepoint : '?');
    if (!doc || line >= vec_docline_size(doc->lines)) return;

    document_line* doc_line = vec_docline_get(doc->lines, line);
    if (col > docline_get_text_length(doc_line)) {
        col = docline_get_text_length(doc_line);
    }

    // Record for undo BEFORE modifying anything
    u32_string* text = u32str_create();
    u32str_reserve(text, 1);
    u32str_insert_char(text, 0, codepoint);
    record_action(doc, edit_action::INSERT, line, col, text);

    // Insert the character
    docline_text_insert_char(doc_line, col, codepoint);
    docline_mark_dirty(doc_line);

    // Don't update cursor here - let the caller do it
    // This avoids double-incrementing the cursor
    doc->has_selection = false;
}

void doc_insert_str32(document* doc, u32 line, u32 col, u32_string* text) {
    if (!doc || !text || line >= vec_docline_size(doc->lines)) return;

    printf("[DEBUG] doc_insert_str32: line=%u, col=%u, text_len=%u\n", line, col, u32str_length(text));

    // Count newlines in text to insert
    u32 newline_count = 0;
    for (u32 i = 0; i < u32str_length(text); i++) {
        if (u32str_get(text, i) == '\n') newline_count++;
    }
    printf("[DEBUG] doc_insert_str32: newlines in text=%u, last char=0x%X\n",
           newline_count,
           u32str_length(text) > 0 ? u32str_get(text, u32str_length(text) - 1) : 0);

    document_line* doc_line = vec_docline_get(doc->lines, line);
    if (col > docline_get_text_length(doc_line)) {
        col = docline_get_text_length(doc_line);
    }

    i32 newline_pos = u32str_indexOf(text, '\n');
    printf("[DEBUG] doc_insert_str32: first newline at position %d\n", newline_pos);

    if (newline_pos == -1) {
        docline_text_insert(doc_line, text, col, 0, u32str_length(text));
        docline_mark_dirty(doc_line);
    } else {
        u32_string* remainder = docline_text_substr(doc_line, col, docline_get_text_length(doc_line) - col);
        docline_text_remove(doc_line, col, docline_get_text_length(doc_line) - col);
        docline_mark_dirty(doc_line);

        u32 total_length = u32str_length(text);
        u32 segment_start = 0;
        u32 current_line_index = line;
        document_line* current_line_ptr = doc_line;

        while (segment_start <= total_length) {
            i32 next_newline = -1;
            for (u32 i = segment_start; i < total_length; ++i) {
                if (u32str_get(text, i) == '\n') {
                    next_newline = i;
                    break;
                }
            }

            u32 segment_end = (next_newline == -1) ? total_length : (u32)next_newline;
            u32 segment_length = segment_end - segment_start;

            if (segment_length > 0) {
                u32_string* segment = u32str_substr(text, segment_start, segment_length);
                docline_text_insert(current_line_ptr, segment, docline_get_text_length(current_line_ptr), 0, u32str_length(segment));
                docline_mark_dirty(current_line_ptr);
                u32str_destroy(segment);
            }

            if (next_newline == -1) {
                break;
            }

            document_line* new_line = docline_create();
            vec_docline_insert(doc->lines, current_line_index + 1, new_line);
            printf("[DEBUG] Created new line at index %u\n", current_line_index + 1);
            current_line_index++;
            current_line_ptr = new_line;
            segment_start = (u32)next_newline + 1;
        }

        docline_text_insert(current_line_ptr, remainder, docline_get_text_length(current_line_ptr), 0, u32str_length(remainder));
        docline_mark_dirty(current_line_ptr);
        u32str_destroy(remainder);
    }

    // Calculate final cursor position
    u32 final_line = line;
    u32 final_col = col;
    compute_text_extent(line, col, text, &final_line, &final_col);

    // Update cursor
    doc->cursor.row = final_line;
    doc->cursor.column = final_col;
    doc->has_selection = false;

    // Record for undo (copy the text)
    u32_string* text_copy = u32str_substr(text, 0, u32str_length(text));
    record_action(doc, edit_action::INSERT, line, col, text_copy);
}

void doc_delete_char(document* doc, u32 line, u32 col) {
    printf("[EDIT] doc_delete_char: line=%u, col=%u\n", line, col);
    if (!doc || line >= vec_docline_size(doc->lines)) return;

    document_line* doc_line = vec_docline_get(doc->lines, line);

    if (col < docline_get_text_length(doc_line)) {
        // Delete character at position
        u32 deleted_char = u32str_get(docline_access_text(doc_line), col);

        // Record for undo
        u32_string* text = u32str_create();
        u32str_reserve(text, 1);
        u32str_insert_char(text, 0, deleted_char);
        record_action(doc, edit_action::REMOVE, line, col, text);

        // Delete the character
        u32str_remove(docline_access_text(doc_line), col, 1);
        docline_mark_dirty(doc_line);

        // Cursor stays at same position
        doc->cursor.row = line;
        doc->cursor.column = col;
        doc->has_selection = false;
    } else if (col == docline_get_text_length(doc_line) && line + 1 < vec_docline_size(doc->lines)) {
        // Join lines - record newline as removed
        u32_string* text = u32str_create();
        u32str_reserve(text, 1);
        u32str_insert_char(text, 0, '\n');
        record_action(doc, edit_action::REMOVE, line, col, text);

        // Join the lines
        document_line* next_line = vec_docline_get(doc->lines, line + 1);
        docline_text_insert(doc_line, docline_access_text(next_line), docline_get_text_length(doc_line), 0, docline_get_text_length(next_line));
        docline_mark_dirty(doc_line);
        vec_docline_remove(doc->lines, line + 1);

        // Cursor stays at join position
        doc->cursor.row = line;
        doc->cursor.column = col;
        doc->has_selection = false;
    }
}

void doc_delete_range(document* doc, u32 start_line, u32 start_col, u32 end_line, u32 end_col) {
    if (!doc || start_line >= vec_docline_size(doc->lines)) return;

    // Save text for undo
    u32_string* deleted_text = doc_get_range(doc, start_line, start_col, end_line, end_col);

    printf("[DEBUG] doc_delete_range: start=%u:%u, end=%u:%u\n",
           start_line, start_col, end_line, end_col);
    printf("[DEBUG] doc_delete_range: captured text length=%u, last char=0x%X\n",
           deleted_text ? u32str_length(deleted_text) : 0,
           deleted_text && u32str_length(deleted_text) > 0 ?
           u32str_get(deleted_text, u32str_length(deleted_text) - 1) : 0);

    // Count newlines in captured text
    if (deleted_text) {
        u32 newline_count = 0;
        for (u32 i = 0; i < u32str_length(deleted_text); i++) {
            if (u32str_get(deleted_text, i) == '\n') newline_count++;
        }
        printf("[DEBUG] doc_delete_range: newlines in captured text=%u\n", newline_count);
    }

    debug_print_text("[DELETE_RANGE] captured: ", deleted_text);

    // Record for undo (takes ownership of deleted_text)
    record_action(doc, edit_action::REMOVE, start_line, start_col, deleted_text);

    // Perform deletion
    if (start_line == end_line) {
        document_line* line = vec_docline_get(doc->lines, start_line);
        u32str_remove(docline_access_text(line), start_col, end_col - start_col);
        docline_mark_dirty(line);
    } else {
        document_line* first_line = vec_docline_get(doc->lines, start_line);
        document_line* last_line = vec_docline_get(doc->lines, end_line);

        u32_string* remainder = docline_text_substr(last_line, end_col, docline_get_text_length(last_line) - end_col);

        u32str_remove(docline_access_text(first_line), start_col, docline_get_text_length(first_line) - start_col);
        u32str_insert(docline_access_text(first_line), remainder, docline_get_text_length(first_line), 0, u32str_length(remainder));
        docline_mark_dirty(first_line);
        u32str_destroy(remainder);

        for (u32 i = end_line; i > start_line; i--) {
            vec_docline_remove(doc->lines, i);
        }
    }

    // Update cursor
    doc->cursor.row = start_line;
    doc->cursor.column = start_col;
    doc->has_selection = false;
}

void doc_insert_line_str32(document* doc, u32 line_index, u32_string* content) {
    if (!doc || !content) return;
    
    if (line_index > vec_docline_size(doc->lines)) {
        line_index = vec_docline_size(doc->lines);
    }
    
    document_line* new_line = docline_create_with_text(content);
    vec_docline_insert(doc->lines, line_index, new_line);
    
    // Create text with newline for undo
    u32_string* text_with_newline = u32str_substr(content, 0, u32str_length(content));

    // Record for undo - treating line as text followed by newline
    record_action(doc, edit_action::INSERT, line_index, 0, text_with_newline);

    // Update cursor
    doc->cursor.row = line_index;
    doc->cursor.column = u32str_length(content);
    doc->has_selection = false;
}

void doc_append_line_str32(document* doc, u32_string* content) {
    if (!doc || !content) return;
    doc_insert_line_str32(doc, vec_docline_size(doc->lines), content);
}

void doc_delete_line(document* doc, u32 line_index) {
    if (!doc || line_index >= vec_docline_size(doc->lines)) {
        return;
    }

    document_line* deleted_line = vec_docline_get(doc->lines, line_index);
    if (!deleted_line || !docline_access_text(deleted_line)) {
        // If line is invalid, just remove it from the vector
        vec_docline_remove(doc->lines, line_index);
        return;
    }

    u32 line_length = docline_get_text_length(deleted_line);
    printf("[DEBUG] doc_delete_line: line_index=%u, line_length=%u\n", line_index, line_length);

    // Capture the line text AND a newline (unless it's the last line)
    u32_string* line_copy = docline_text_substr(deleted_line, 0, line_length);
    printf("[DEBUG] doc_delete_line: copied text length=%u\n", line_copy ? u32str_length(line_copy) : 0);

    // Only add newline if this isn't the last line in the document
    // OR if there's a line after this one (meaning this line has a line break)
    if (line_index < vec_docline_size(doc->lines) - 1) {
        u32str_insert_char(line_copy, u32str_length(line_copy), '\n');
    }

    // Record for undo
    record_action(doc, edit_action::REMOVE, line_index, 0, line_copy);

    vec_docline_remove(doc->lines, line_index);

    // Update cursor
    doc->cursor.row = line_index > 0 ? line_index - 1 : 0;
    doc->cursor.column = 0;
    doc->has_selection = false;
}

void doc_split_line(document* doc, u32 line, u32 col) {
    if (!doc || line >= vec_docline_size(doc->lines)) return;

    document_line* doc_line = vec_docline_get(doc->lines, line);
    if (col > docline_get_text_length(doc_line)) {
        col = docline_get_text_length(doc_line);
    }

    // Record newline insertion for undo
    u32_string* newline = u32str_create();
    u32str_insert_char(newline, 0, '\n');
    record_action(doc, edit_action::INSERT, line, col, newline);

    // Perform the split
    u32_string* new_text = docline_text_substr(doc_line, col, docline_get_text_length(doc_line) - col);
    docline_text_remove(doc_line, col, docline_get_text_length(doc_line) - col);
    docline_mark_dirty(doc_line);

    document_line* new_line = docline_create_with_text(new_text);
    vec_docline_insert(doc->lines, line + 1, new_line);
    u32str_destroy(new_text);

    // Update cursor
    doc->cursor.row = line + 1;
    doc->cursor.column = 0;
    doc->has_selection = false;
}

void doc_join_lines(document* doc, u32 line) {
    if (!doc || line >= vec_docline_size(doc->lines) - 1) return;
    
    document_line* doc_line = vec_docline_get(doc->lines, line);
    document_line* next_line = vec_docline_get(doc->lines, line + 1);
    
    u32 join_pos = docline_get_text_length(doc_line);
    docline_text_insert(doc_line, docline_access_text(next_line), docline_get_text_length(doc_line), 0, docline_get_text_length(next_line));
    docline_mark_dirty(doc_line);
    vec_docline_remove(doc->lines, line + 1);
    
    // Record newline removal for undo
    u32_string* newline = u32str_create();
    u32str_insert_char(newline, 0, '\n');
    record_action(doc, edit_action::REMOVE, line, join_pos, newline);

    // Update cursor
    doc->cursor.row = line;
    doc->cursor.column = join_pos;
    doc->has_selection = false;
}

void doc_undo(document* doc) {
    printf("[UNDO] doc_undo called, stack size=%u\n", doc ? doc->undo_count : 0);
    if (!doc || doc->undo_count == 0) {
        printf("[UNDO] Cannot undo: doc=%p, stack_size=%u\n", (void*)doc, doc ? doc->undo_count : 0);
        return;
    }

    doc->in_undo_redo = true;

    // Pop action from undo stack
    edit_action action = pop_undo(doc);
    printf("[UNDO] Popped action: type=%s, line=%u, col=%u, text_len=%u\n",
           action.type == edit_action::INSERT ? "INSERT" : "REMOVE",
           action.line, action.col, action.text ? u32str_length(action.text) : 0);

    // Apply inverse operation
    if (action.type == edit_action::INSERT) {
        // Was inserted, so remove it
        u32 end_line = action.line;
        u32 end_col = action.col;
        compute_text_extent(action.line, action.col, action.text, &end_line, &end_col);
        printf("[UNDO] Removing range %u:%u -> %u:%u\n", action.line, action.col, end_line, end_col);
        debug_print_text("[UNDO] Text to remove: ", action.text);
        doc_delete_range(doc, action.line, action.col, end_line, end_col);
    } else {  // REMOVE
        // Was removed, so insert it back
        printf("[UNDO] Reinserting at %u:%u\n", action.line, action.col);
        debug_print_text("[UNDO] Text to insert: ", action.text);
        doc_insert_str32(doc, action.line, action.col, action.text);
    }

    // Move to redo stack (swap type for redo)
    // IMPORTANT: We need to copy the text, not reuse the same pointer
    edit_action redo_action;
    redo_action.type = (action.type == edit_action::INSERT) ?
                       edit_action::REMOVE : edit_action::INSERT;
    redo_action.line = action.line;
    redo_action.col = action.col;
    redo_action.text = action.text ? u32str_substr(action.text, 0, u32str_length(action.text)) : nullptr;
    redo_action.timestamp = action.timestamp;
    redo_action.cursor_after = action.cursor_after;
    redo_action.had_selection_after = action.had_selection_after;
    push_redo(doc, redo_action);

    // Clean up the original action's text since we made a copy
    if (action.text) u32str_destroy(action.text);

    doc->in_undo_redo = false;
}

void doc_redo(document* doc) {
    printf("[REDO] doc_redo called, stack size=%u\n", doc ? doc->redo_count : 0);
    if (!doc || doc->redo_count == 0) {
        printf("[REDO] Cannot redo: doc=%p, stack_size=%u\n", (void*)doc, doc ? doc->redo_count : 0);
        return;
    }

    doc->in_undo_redo = true;

    // Pop action from redo stack
    edit_action action = pop_redo(doc);

    // Apply inverse operation (redo stack has inverted types)
    if (action.type == edit_action::INSERT) {
        // Was removed in undo, so remove it again
        u32 end_line = action.line;
        u32 end_col = action.col;
        compute_text_extent(action.line, action.col, action.text, &end_line, &end_col);
        printf("[REDO] Removing range %u:%u -> %u:%u\n", action.line, action.col, end_line, end_col);
        debug_print_text("[REDO] Text to remove: ", action.text);
        doc_delete_range(doc, action.line, action.col, end_line, end_col);
    } else {  // REMOVE
        // Was inserted in undo, so insert it again
        printf("[REDO] Reinserting at %u:%u\n", action.line, action.col);
        debug_print_text("[REDO] Text to insert: ", action.text);
        doc_insert_str32(doc, action.line, action.col, action.text);
    }

    // Move back to undo stack (swap type back)
    // IMPORTANT: We need to copy the text, not reuse the same pointer
    edit_action undo_action;
    undo_action.type = (action.type == edit_action::INSERT) ?
                       edit_action::REMOVE : edit_action::INSERT;
    undo_action.line = action.line;
    undo_action.col = action.col;
    undo_action.text = action.text ? u32str_substr(action.text, 0, u32str_length(action.text)) : nullptr;
    undo_action.timestamp = action.timestamp;
    undo_action.cursor_after = action.cursor_after;
    undo_action.had_selection_after = action.had_selection_after;
    push_undo(doc, undo_action);

    // Clean up the original action's text since we made a copy
    if (action.text) u32str_destroy(action.text);

    doc->in_undo_redo = false;
}

bool doc_can_undo(document* doc) {
    return doc && doc->undo_count > 0;
}

bool doc_can_redo(document* doc) {
    return doc && doc->redo_count > 0;
}

bool doc_get_last_edit_position(document* doc, u32* out_line, u32* out_col) {
    // Simplified system doesn't track this separately
    // Could look at last action on undo stack if needed
    return false;
}

void doc_clear_last_edit_position(document* doc) {
    // No-op in simplified system
}

void doc_commit_pending_undo(document* doc) {
    // No-op in simplified system - no word building to commit
}

void doc_mark_line_dirty(document* doc, u32 line) {
    if (!doc || line >= vec_docline_size(doc->lines)) return;
    
    document_line* doc_line = vec_docline_get(doc->lines, line);
    if (doc_line) {
        docline_mark_dirty(doc_line);
    }
}

void doc_tokenize_line(document* doc, u32 line) {
    if (!doc || line >= vec_docline_size(doc->lines)) return;
    
    document_line* doc_line = vec_docline_get(doc->lines, line);
    if (doc_line && docline_is_dirty(doc_line)) {
        docline_tokenize(doc_line);
    }
}

bool doc_is_line_dirty(document* doc, u32 line) {
    if (!doc || line >= vec_docline_size(doc->lines)) return false;
    
    document_line* doc_line = vec_docline_get(doc->lines, line);
    return doc_line ? docline_is_dirty(doc_line) : false;
}

token_span* doc_get_line_tokens(document* doc, u32 line_index) {
    if (!doc || line_index >= vec_docline_size(doc->lines)) return nullptr;
    
    document_line* doc_line = vec_docline_get(doc->lines, line_index);
    if (!doc_line || docline_is_dirty(doc_line)) return nullptr;
    
    return docline_access_tokens(doc_line);
}

u32 doc_get_line_token_count(document* doc, u32 line_index) {
    if (!doc || line_index >= vec_docline_size(doc->lines)) return 0;

    document_line* doc_line = vec_docline_get(doc->lines, line_index);
    if (!doc_line || docline_is_dirty(doc_line)) return 0;

    return docline_get_token_count(doc_line);
}

// === Cursor and Selection Management Implementation ===

document_cursor doc_get_cursor(document* doc) {
    if (!doc) {
        document_cursor empty = {0, 0};
        return empty;
    }
    return doc->cursor;
}

void doc_set_cursor(document* doc, u32 row, u32 column) {
    if (!doc) return;
    doc->cursor.row = row;
    doc->cursor.column = column;
    doc_validate_cursor(doc);
}

u32 doc_get_cursor_line(document* doc) {
    if (!doc) return 0;
    return doc->cursor.row;
}

u32 doc_get_cursor_column(document* doc) {
    if (!doc) return 0;
    return doc->cursor.column;
}

void doc_set_cursor_line(document* doc, u32 line) {
    if (!doc) return;
    doc->cursor.row = line;
    doc_validate_cursor(doc);
}

void doc_set_cursor_column(document* doc, u32 column) {
    if (!doc) return;
    doc->cursor.column = column;
    doc_validate_cursor(doc);
}

document_cursor doc_get_selection_anchor(document* doc) {
    if (!doc) {
        document_cursor empty = {0, 0};
        return empty;
    }
    return doc->selection_anchor;
}

void doc_set_selection_anchor(document* doc, u32 row, u32 column) {
    if (!doc) return;
    doc->selection_anchor.row = row;
    doc->selection_anchor.column = column;

    // Validate selection anchor to document bounds
    u32 line_count = vec_docline_size(doc->lines);
    if (doc->selection_anchor.row >= line_count) {
        doc->selection_anchor.row = line_count > 0 ? line_count - 1 : 0;
    }

    u32 line_length = doc_get_line_length(doc, doc->selection_anchor.row);
    if (doc->selection_anchor.column > line_length) {
        doc->selection_anchor.column = line_length;
    }
}

bool doc_has_selection(document* doc) {
    if (!doc) return false;
    return doc->has_selection;
}

void doc_set_has_selection(document* doc, bool has_selection) {
    if (!doc) return;
    doc->has_selection = has_selection;
}

void doc_clear_selection(document* doc) {
    if (!doc) return;
    doc->has_selection = false;
}

bool doc_get_selection_range(document* doc, document_cursor* start, document_cursor* end) {
    if (!doc || !doc->has_selection || !start || !end) return false;

    // Normalize selection so start is always before end
    if (doc->cursor.row < doc->selection_anchor.row ||
        (doc->cursor.row == doc->selection_anchor.row && doc->cursor.column < doc->selection_anchor.column)) {
        *start = doc->cursor;
        *end = doc->selection_anchor;
    } else {
        *start = doc->selection_anchor;
        *end = doc->cursor;
    }

    return true;
}

void doc_validate_cursor(document* doc) {
    if (!doc) return;

    u32 line_count = vec_docline_size(doc->lines);

    // Clamp row to valid range
    if (doc->cursor.row >= line_count) {
        doc->cursor.row = line_count > 0 ? line_count - 1 : 0;
    }

    // Clamp column to line length
    u32 line_length = doc_get_line_length(doc, doc->cursor.row);
    if (doc->cursor.column > line_length) {
        doc->cursor.column = line_length;
    }
}
