#include "document.h"
#include "strings.h"
#define CARROT_INCLUDE_SYNTAX_DEFS
#include "syntax.h"
#undef CARROT_INCLUDE_SYNTAX_DEFS
#include <cstdlib>
#include <cstring>
#include <cstdio>

/**
 * Represents a single edit operation that can be undone/redone.
 * Each action stores enough information to both apply and reverse the operation.
 */
struct edit_action {
    enum action_type {
        INSERT_CHAR,    // Single character insertion
        DELETE_CHAR,    // Single character deletion
        INSERT_TEXT,    // Multi-character text insertion (includes word-based undo)
        DELETE_RANGE,   // Range deletion (selection delete)
        INSERT_LINE,    // Full line insertion
        DELETE_LINE,    // Full line deletion
        SPLIT_LINE,     // Line split (Enter key)
        JOIN_LINE       // Line join (Delete at end of line)
    } type;

    // Position where the action occurred
    u32 line;           // Starting line number
    u32 col;            // Starting column number
    u32 end_line;       // Ending line (for range operations)
    u32 end_col;        // Ending column (for range operations)

    // Data for the action
    u32_string* text;   // Text content (for INSERT_TEXT, DELETE_RANGE, INSERT_LINE, DELETE_LINE)
    u32 codepoint;      // Single character (for INSERT_CHAR, DELETE_CHAR)

    // Cursor position hints for better UX after undo/redo
    u32 cursor_line_after_redo;  // Where cursor should be after performing the action
    u32 cursor_col_after_redo;   // (e.g., end of inserted text)
    u32 cursor_line_after_undo;  // Where cursor should be after undoing the action
    u32 cursor_col_after_undo;   // (e.g., start of where text was)
};

/**
 * Main document structure that manages text content and edit history.
 * Handles both the text lines and the undo/redo system.
 */
struct document {
    // === Document Content ===
    vector_docline* lines;      // Dynamic array of text lines (each line is a document_line)
    bool modified;               // True if document has unsaved changes

    // === Undo/Redo System ===
    edit_action* undo_stack;    // Array of edit actions for undo/redo
    u32 undo_stack_size;        // Number of actions currently in the stack
    u32 undo_position;          // Current position in undo stack (0 = nothing to undo)
    u32 max_undo_levels;        // Maximum number of undo levels (0 = undo disabled)
    bool in_undo_redo;          // Flag to prevent recording actions during undo/redo operations

    // === Cursor Positioning After Undo/Redo ===
    u32 last_edit_line;         // Line where last undo/redo occurred
    u32 last_edit_col;          // Column where last undo/redo occurred
    bool has_edit_position;     // True if last_edit_line/col contains valid position

    // === Word-Based Undo System ===
    bool building_word;         // True if currently accumulating characters into a word
    u32 word_start_line;        // Line where current word started
    u32 word_start_col;         // Column where current word started
    u32_string* current_word;   // Buffer holding characters of word being built
};

/**
 * Adds an edit action to the undo stack.
 * Handles stack overflow by removing oldest action.
 * Clears redo stack if we're in the middle of it.
 */
static void add_undo_action(document* doc, edit_action action) {
    if (doc->max_undo_levels == 0 || doc->in_undo_redo) {
        doc->modified = true;
        // If there's text in the action that was allocated, free it
        if (action.text) {
            u32str_destroy(action.text);
        }
        return;
    }
    
    // If we're in the middle of the undo stack (after undoing), 
    // we need to clear the redo portion
    if (doc->undo_position < doc->undo_stack_size) {
        // Free any text in the actions we're about to overwrite
        for (u32 i = doc->undo_position; i < doc->undo_stack_size; i++) {
            if (doc->undo_stack[i].text) {
                u32str_destroy(doc->undo_stack[i].text);
            }
        }
        doc->undo_stack_size = doc->undo_position;
    }
    
    // If we're at max capacity, shift everything down and overwrite the oldest
    if (doc->undo_stack_size >= doc->max_undo_levels) {
        // Free the oldest action's text if it exists
        if (doc->undo_stack[0].text) {
            u32str_destroy(doc->undo_stack[0].text);
        }
        
        // Shift all actions down by one
        for (u32 i = 0; i < doc->undo_stack_size - 1; i++) {
            doc->undo_stack[i] = doc->undo_stack[i + 1];
        }
        doc->undo_stack_size--;
        if (doc->undo_position > 0) doc->undo_position--;
    }
    
    doc->undo_stack[doc->undo_stack_size] = action;
    doc->undo_stack_size++;
    doc->undo_position = doc->undo_stack_size;
    doc->modified = true;
}

document* doc_create(u32 undo_levels) {
    document* doc = (document*)malloc(sizeof(document));
    doc->lines = vec_docline_create();
    doc->modified = false;
    doc->max_undo_levels = undo_levels;
    
    if (undo_levels > 0) {
        doc->undo_stack = (edit_action*)malloc(undo_levels * sizeof(edit_action));
    } else {
        doc->undo_stack = nullptr;
    }
    
    doc->undo_stack_size = 0;
    doc->undo_position = 0;
    doc->in_undo_redo = false;
    doc->last_edit_line = 0;
    doc->last_edit_col = 0;
    doc->has_edit_position = false;
    doc->building_word = false;
    doc->word_start_line = 0;
    doc->word_start_col = 0;
    doc->current_word = u32str_create();
    return doc;
}

void doc_destroy(document* doc) {
    if (!doc) return;

    vec_docline_destroy(doc->lines);

    if (doc->current_word) {
        u32str_destroy(doc->current_word);
    }

    if (doc->undo_stack) {
        for (u32 i = 0; i < doc->undo_stack_size; i++) {
            if (doc->undo_stack[i].text) {
                u32str_destroy(doc->undo_stack[i].text);
            }
        }
        free(doc->undo_stack);
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
        
        u32 newline = '\n';
        u32_string* newline_str = u32str_create();
        u32str_reserve(newline_str, 4);
        u32str_set(newline_str, 0, newline);
        u32str_insert(result, newline_str, u32str_length(result), 0, 1);
        u32str_destroy(newline_str);
        
        for (u32 i = start_line + 1; i < end_line; i++) {
            document_line* line = vec_docline_get(doc->lines, i);
            u32str_insert(result, docline_access_text(line), u32str_length(result), 0, docline_get_text_length(line));
            
            u32_string* nl = u32str_create();
            u32str_reserve(nl, 4);
            u32str_set(nl, 0, newline);
            u32str_insert(result, nl, u32str_length(result), 0, 1);
            u32str_destroy(nl);
        }
        
        document_line* last_line = vec_docline_get(doc->lines, end_line);
        u32_string* last_substr = docline_text_substr(last_line, 0, end_col);
        u32str_insert(result, last_substr, u32str_length(result), 0, u32str_length(last_substr));
        u32str_destroy(last_substr);
    }
    
    return result;
}

/**
 * Determines if a character should break word grouping for undo.
 * Word boundaries include spaces, punctuation, and special characters.
 * @param codepoint Unicode character to check
 * @return true if character is a word boundary
 */
static bool is_word_boundary(u32 codepoint) {
    // Word boundaries: space, tab, newline, and common punctuation
    return codepoint == ' ' || codepoint == '\t' || codepoint == '\n' ||
           codepoint == '.' || codepoint == ',' || codepoint == ';' ||
           codepoint == ':' || codepoint == '!' || codepoint == '?' ||
           codepoint == '(' || codepoint == ')' || codepoint == '[' ||
           codepoint == ']' || codepoint == '{' || codepoint == '}' ||
           codepoint == '"' || codepoint == '\'' || codepoint == '/' ||
           codepoint == '\\' || codepoint == '<' || codepoint == '>' ||
           codepoint == '=' || codepoint == '+' || codepoint == '-' ||
           codepoint == '*' || codepoint == '&' || codepoint == '|' ||
           codepoint == '^' || codepoint == '%' || codepoint == '#';
}

/**
 * Commits the accumulated word buffer as a single INSERT_TEXT undo action.
 * Called when word boundary is reached or cursor moves.
 * Resets the word building state after committing.
 */
static void commit_word_undo(document* doc) {
    if (!doc->building_word || u32str_length(doc->current_word) == 0) {
        return;
    }

    edit_action action;
    action.type = edit_action::INSERT_TEXT;
    action.line = doc->word_start_line;
    action.col = doc->word_start_col;
    action.end_line = 0;
    action.end_col = 0;
    action.codepoint = 0;
    action.text = u32str_substr(doc->current_word, 0, u32str_length(doc->current_word));

    // Calculate cursor position after text insertion
    u32 final_line = doc->word_start_line;
    u32 final_col = doc->word_start_col + u32str_length(doc->current_word);

    action.cursor_line_after_redo = final_line;
    action.cursor_col_after_redo = final_col;
    action.cursor_line_after_undo = doc->word_start_line;
    action.cursor_col_after_undo = doc->word_start_col;

    add_undo_action(doc, action);

    // Reset word building state
    doc->building_word = false;
    u32str_clear(doc->current_word);
}

void doc_insert_char(document* doc, u32 line, u32 col, u32 codepoint) {
    if (!doc || line >= vec_docline_size(doc->lines)) {
        return;
    }

    document_line* doc_line = vec_docline_get(doc->lines, line);
    if (col > docline_get_text_length(doc_line)) {
        col = docline_get_text_length(doc_line);
    }

    // Insert the character into the document
    docline_text_insert_char(doc_line, col, codepoint);
    docline_mark_dirty(doc_line);

    // Check if we need to start a new word or continue building one
    bool is_boundary = is_word_boundary(codepoint);

    if (doc->building_word) {
        // Check if we're still typing at the expected position
        u32 expected_col = doc->word_start_col + u32str_length(doc->current_word);
        bool position_matches = (line == doc->word_start_line && col == expected_col);

        if (!position_matches || is_boundary) {
            // Position changed or hit a boundary - commit the current word
            commit_word_undo(doc);
        }
    }

    if (is_boundary) {
        // Word boundary characters get their own undo action
        edit_action action;
        action.type = edit_action::INSERT_CHAR;
        action.line = line;
        action.col = col;
        action.end_line = 0;
        action.end_col = 0;
        action.codepoint = codepoint;
        action.text = nullptr;
        action.cursor_line_after_redo = line;
        action.cursor_col_after_redo = col + 1;
        action.cursor_line_after_undo = line;
        action.cursor_col_after_undo = col;
        add_undo_action(doc, action);
    } else {
        // Regular character - add to current word
        if (!doc->building_word) {
            // Start a new word
            doc->building_word = true;
            doc->word_start_line = line;
            doc->word_start_col = col;
            u32str_clear(doc->current_word);
        }
        // Add character to current word
        u32str_insert_char(doc->current_word, u32str_length(doc->current_word), codepoint);
    }
}

void doc_insert_str32(document* doc, u32 line, u32 col, u32_string* text) {
    if (!doc || !text || line >= vec_docline_size(doc->lines)) return;

    // Commit any pending word before inserting text
    commit_word_undo(doc);
    
    document_line* doc_line = vec_docline_get(doc->lines, line);
    if (col > docline_get_text_length(doc_line)) {
        col = docline_get_text_length(doc_line);
    }
    
    i32 newline_pos = u32str_indexOf(text, '\n');
    
    if (newline_pos == -1) {
        docline_text_insert(doc_line, text, col, 0, u32str_length(text));
        docline_mark_dirty(doc_line);
    } else {
        u32_string* remainder = docline_text_substr(doc_line, col, docline_get_text_length(doc_line) - col);
        docline_text_remove(doc_line, col, docline_get_text_length(doc_line) - col);
        docline_mark_dirty(doc_line);
        
        u32 text_start = 0;
        u32 current_line = line;
        
        while (text_start < u32str_length(text)) {
            i32 next_newline = -1;
            for (u32 i = text_start; i < u32str_length(text); i++) {
                if (u32str_get(text, i) == '\n') {
                    next_newline = i;
                    break;
                }
            }
            
            if (next_newline == -1) {
                u32_string* last_part = u32str_substr(text, text_start, u32str_length(text) - text_start);
                if (current_line == line) {
                    docline_text_insert(doc_line, last_part, docline_get_text_length(doc_line), 0, u32str_length(last_part));
                    docline_mark_dirty(doc_line);
                } else {
                    u32_string* new_text = u32str_create();
                    u32str_insert(new_text, last_part, 0, 0, u32str_length(last_part));
                    document_line* new_line = docline_create_with_text(new_text);
                    vec_docline_insert(doc->lines, current_line, new_line);
                    u32str_destroy(new_text);
                }
                u32str_destroy(last_part);
                break;
            } else {
                u32 part_length = next_newline - text_start;
                u32_string* part = u32str_substr(text, text_start, part_length);
                
                if (current_line == line) {
                    docline_text_insert(doc_line, part, docline_get_text_length(doc_line), 0, u32str_length(part));
                    docline_mark_dirty(doc_line);
                    current_line++;
                    document_line* new_line = docline_create();
                    vec_docline_insert(doc->lines, current_line, new_line);
                } else {
                    u32_string* new_text = u32str_create();
                    u32str_insert(new_text, part, 0, 0, u32str_length(part));
                    document_line* new_line = docline_create_with_text(new_text);
                    vec_docline_insert(doc->lines, current_line, new_line);
                    u32str_destroy(new_text);
                    current_line++;
                }
                
                u32str_destroy(part);
                text_start = next_newline + 1;
            }
        }
        
        document_line* last_line = vec_docline_get(doc->lines, current_line);
        docline_text_insert(last_line, remainder, docline_get_text_length(last_line), 0, u32str_length(remainder));
        docline_mark_dirty(last_line);
        u32str_destroy(remainder);
    }
    
    edit_action action;
    action.type = edit_action::INSERT_TEXT;
    action.line = line;
    action.col = col;
    action.end_line = 0;
    action.end_col = 0;
    action.codepoint = 0;
    action.text = u32str_substr(text, 0, u32str_length(text));
    // Calculate cursor position after text insertion
    u32 final_line = line;
    u32 final_col = col + u32str_length(text);
    for (u32 i = 0; i < u32str_length(text); i++) {
        if (u32str_get(text, i) == '\n') {
            final_line++;
            final_col = 0;
        } else {
            final_col++;
        }
    }
    action.cursor_line_after_redo = final_line;
    action.cursor_col_after_redo = final_col;
    action.cursor_line_after_undo = line;
    action.cursor_col_after_undo = col;
    add_undo_action(doc, action);
}

void doc_delete_char(document* doc, u32 line, u32 col) {
    if (!doc || line >= vec_docline_size(doc->lines)) return;

    // Commit any pending word before deleting
    commit_word_undo(doc);
    
    document_line* doc_line = vec_docline_get(doc->lines, line);
    
    if (col < docline_get_text_length(doc_line)) {
        u32 deleted_char = u32str_get(docline_access_text(doc_line), col);
        u32str_remove(docline_access_text(doc_line), col, 1);
        docline_mark_dirty(doc_line);
        
        edit_action action;
        action.type = edit_action::DELETE_CHAR;
        action.line = line;
        action.col = col;
        action.end_line = 0;
        action.end_col = 0;
        action.codepoint = deleted_char;
        action.text = nullptr;
        action.cursor_line_after_redo = line;
        action.cursor_col_after_redo = col;
        action.cursor_line_after_undo = line;
        action.cursor_col_after_undo = col + 1;
        add_undo_action(doc, action);
    } else if (col == docline_get_text_length(doc_line) && line + 1 < vec_docline_size(doc->lines)) {
        document_line* next_line = vec_docline_get(doc->lines, line + 1);
        docline_text_insert(doc_line, docline_access_text(next_line), docline_get_text_length(doc_line), 0, docline_get_text_length(next_line));
        docline_mark_dirty(doc_line);
        vec_docline_remove(doc->lines, line + 1);
        
        edit_action action;
        action.type = edit_action::JOIN_LINE;
        action.line = line;
        action.col = col;
        action.end_line = 0;
        action.end_col = 0;
        action.codepoint = 0;
        action.text = nullptr;
        action.cursor_line_after_redo = line;
        action.cursor_col_after_redo = col;
        action.cursor_line_after_undo = line + 1;
        action.cursor_col_after_undo = 0;
        add_undo_action(doc, action);
    }
}

void doc_delete_range(document* doc, u32 start_line, u32 start_col, u32 end_line, u32 end_col) {
    if (!doc || start_line >= vec_docline_size(doc->lines)) return;

    // Commit any pending word before deleting range
    commit_word_undo(doc);
    
    u32_string* deleted_text = doc_get_range(doc, start_line, start_col, end_line, end_col);
    
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
    
    edit_action action;
    action.type = edit_action::DELETE_RANGE;
    action.line = start_line;
    action.col = start_col;
    action.end_line = end_line;
    action.end_col = end_col;
    action.codepoint = 0;
    action.text = deleted_text;
    action.cursor_line_after_redo = start_line;
    action.cursor_col_after_redo = start_col;
    action.cursor_line_after_undo = end_line;
    action.cursor_col_after_undo = end_col;
    add_undo_action(doc, action);
}

void doc_insert_line_str32(document* doc, u32 line_index, u32_string* content) {
    if (!doc || !content) return;
    
    if (line_index > vec_docline_size(doc->lines)) {
        line_index = vec_docline_size(doc->lines);
    }
    
    document_line* new_line = docline_create_with_text(content);
    vec_docline_insert(doc->lines, line_index, new_line);
    
    edit_action action;
    action.type = edit_action::INSERT_LINE;
    action.line = line_index;
    action.col = 0;
    action.end_line = 0;
    action.end_col = 0;
    action.codepoint = 0;
    action.text = u32str_substr(content, 0, u32str_length(content));
    action.cursor_line_after_redo = line_index;
    action.cursor_col_after_redo = u32str_length(content);
    action.cursor_line_after_undo = line_index;
    action.cursor_col_after_undo = 0;
    add_undo_action(doc, action);
}

void doc_append_line_str32(document* doc, u32_string* content) {
    if (!doc || !content) return;
    doc_insert_line_str32(doc, vec_docline_size(doc->lines), content);
}

void doc_delete_line(document* doc, u32 line_index) {
    printf("[doc_delete_line] Called with line_index=%u\n", line_index);
    if (!doc || line_index >= vec_docline_size(doc->lines)) {
        printf("[doc_delete_line] Early return: doc=%p, line_index=%u, vec_size=%u\n",
               doc, line_index, doc ? vec_docline_size(doc->lines) : 0);
        return;
    }

    printf("[doc_delete_line] Getting line at index %u\n", line_index);
    document_line* deleted_line = vec_docline_get(doc->lines, line_index);
    if (!deleted_line || !docline_access_text(deleted_line)) {
        // If line is invalid, just remove it from the vector
        printf("[doc_delete_line] Line is invalid (deleted_line=%p, text=%p), removing from vector\n",
               deleted_line, deleted_line ? docline_access_text(deleted_line) : nullptr);
        vec_docline_remove(doc->lines, line_index);
        return;
    }

    printf("[doc_delete_line] Creating copy of line text (length=%u)\n", docline_get_text_length(deleted_line));
    u32_string* line_copy = docline_text_substr(deleted_line, 0, docline_get_text_length(deleted_line));

    printf("[doc_delete_line] Removing line from vector\n");
    vec_docline_remove(doc->lines, line_index);
    
    edit_action action;
    action.type = edit_action::DELETE_LINE;
    action.line = line_index;
    action.col = 0;
    action.end_line = 0;
    action.end_col = 0;
    action.codepoint = 0;
    action.text = line_copy;
    action.cursor_line_after_redo = line_index > 0 ? line_index - 1 : 0;
    action.cursor_col_after_redo = 0;
    action.cursor_line_after_undo = line_index;
    action.cursor_col_after_undo = 0;

    printf("[doc_delete_line] Adding undo action\n");
    add_undo_action(doc, action);
    printf("[doc_delete_line] Complete. Document now has %u lines\n", vec_docline_size(doc->lines));
}

void doc_split_line(document* doc, u32 line, u32 col) {
    if (!doc || line >= vec_docline_size(doc->lines)) return;

    // Commit any pending word before splitting line
    commit_word_undo(doc);
    
    document_line* doc_line = vec_docline_get(doc->lines, line);
    if (col > docline_get_text_length(doc_line)) {
        col = docline_get_text_length(doc_line);
    }
    
    u32_string* new_text = docline_text_substr(doc_line, col, docline_get_text_length(doc_line) - col);
    docline_text_remove(doc_line, col, docline_get_text_length(doc_line) - col);
    docline_mark_dirty(doc_line);
    
    document_line* new_line = docline_create_with_text(new_text);
    vec_docline_insert(doc->lines, line + 1, new_line);
    u32str_destroy(new_text);
    
    edit_action action;
    action.type = edit_action::SPLIT_LINE;
    action.line = line;
    action.col = col;
    action.end_line = 0;
    action.end_col = 0;
    action.codepoint = 0;
    action.text = nullptr;
    action.cursor_line_after_redo = line + 1;
    action.cursor_col_after_redo = 0;
    action.cursor_line_after_undo = line;
    action.cursor_col_after_undo = col;
    add_undo_action(doc, action);
}

void doc_join_lines(document* doc, u32 line) {
    if (!doc || line >= vec_docline_size(doc->lines) - 1) return;
    
    document_line* doc_line = vec_docline_get(doc->lines, line);
    document_line* next_line = vec_docline_get(doc->lines, line + 1);
    
    u32 join_pos = docline_get_text_length(doc_line);
    docline_text_insert(doc_line, docline_access_text(next_line), docline_get_text_length(doc_line), 0, docline_get_text_length(next_line));
    docline_mark_dirty(doc_line);
    vec_docline_remove(doc->lines, line + 1);
    
    edit_action action;
    action.type = edit_action::JOIN_LINE;
    action.line = line;
    action.col = join_pos;
    action.end_line = 0;
    action.end_col = 0;
    action.codepoint = 0;
    action.text = nullptr;
    action.cursor_line_after_redo = line;
    action.cursor_col_after_redo = join_pos;
    action.cursor_line_after_undo = line + 1;
    action.cursor_col_after_undo = 0;
    add_undo_action(doc, action);
}

void doc_undo(document* doc) {
    if (!doc || doc->max_undo_levels == 0 || doc->undo_position == 0) return;

    // Commit any pending word before undoing
    commit_word_undo(doc);

    doc->in_undo_redo = true;  // Prevent recording undo actions
    doc->undo_position--;
    edit_action* action = &doc->undo_stack[doc->undo_position];
    
    switch (action->type) {
        case edit_action::INSERT_CHAR:
            {
                document_line* line = vec_docline_get(doc->lines, action->line);
                docline_text_remove(line, action->col, 1);
                docline_mark_dirty(line);
            }
            break;
            
        case edit_action::DELETE_CHAR:
            {
                document_line* line = vec_docline_get(doc->lines, action->line);
                docline_text_insert_char(line, action->col, action->codepoint);
                docline_mark_dirty(line);
            }
            break;
            
        case edit_action::INSERT_TEXT:
            {
                i32 newline_count = 0;
                for (u32 i = 0; i < u32str_length(action->text); i++) {
                    if (u32str_get(action->text, i) == '\n') {
                        newline_count++;
                    }
                }
                
                if (newline_count == 0) {
                    document_line* line = vec_docline_get(doc->lines, action->line);
                    docline_text_remove(line, action->col, u32str_length(action->text));
                    docline_mark_dirty(line);
                } else {
                    document_line* first_line = vec_docline_get(doc->lines, action->line);
                    document_line* last_line = vec_docline_get(doc->lines, action->line + newline_count);
                    
                    i32 last_newline = -1;
                    for (i32 i = u32str_length(action->text) - 1; i >= 0; i--) {
                        if (u32str_get(action->text, i) == '\n') {
                            last_newline = i;
                            break;
                        }
                    }
                    
                    u32 text_after_last_newline = u32str_length(action->text) - last_newline - 1;
                    u32_string* remainder = docline_text_substr(last_line, text_after_last_newline, 
                                                         docline_get_text_length(last_line) - text_after_last_newline);
                    
                    docline_text_remove(first_line, action->col, docline_get_text_length(first_line) - action->col);
                    docline_text_insert(first_line, remainder, docline_get_text_length(first_line), 0, u32str_length(remainder));
                    docline_mark_dirty(first_line);
                    u32str_destroy(remainder);
                    
                    for (i32 i = 0; i < newline_count; i++) {
                        vec_docline_remove(doc->lines, action->line + 1);
                    }
                }
            }
            break;
            
        case edit_action::DELETE_RANGE:
            doc_insert_str32(doc, action->line, action->col, action->text);
            break;
            
        case edit_action::INSERT_LINE:
            vec_docline_remove(doc->lines, action->line);
            break;
            
        case edit_action::DELETE_LINE:
            {
                document_line* new_line = docline_create_with_text(action->text);
                vec_docline_insert(doc->lines, action->line, new_line);
            }
            break;
            
        case edit_action::SPLIT_LINE:
            doc_join_lines(doc, action->line);
            break;

        case edit_action::JOIN_LINE:
            doc_split_line(doc, action->line, action->col);
            break;
    }

    // Set the last edit position for cursor positioning
    doc->last_edit_line = action->cursor_line_after_undo;
    doc->last_edit_col = action->cursor_col_after_undo;
    doc->has_edit_position = true;

    doc->in_undo_redo = false;  // Re-enable undo recording
}

void doc_redo(document* doc) {
    if (!doc || doc->max_undo_levels == 0 || doc->undo_position >= doc->undo_stack_size) return;

    // Commit any pending word before redoing
    commit_word_undo(doc);

    doc->in_undo_redo = true;  // Prevent recording undo actions
    edit_action* action = &doc->undo_stack[doc->undo_position];
    doc->undo_position++;
    
    switch (action->type) {
        case edit_action::INSERT_CHAR:
            {
                document_line* line = vec_docline_get(doc->lines, action->line);
                docline_text_insert_char(line, action->col, action->codepoint);
                docline_mark_dirty(line);
            }
            break;
            
        case edit_action::DELETE_CHAR:
            {
                document_line* line = vec_docline_get(doc->lines, action->line);
                docline_text_remove(line, action->col, 1);
                docline_mark_dirty(line);
            }
            break;
            
        case edit_action::INSERT_TEXT:
            doc_insert_str32(doc, action->line, action->col, action->text);
            break;

        case edit_action::DELETE_RANGE:
            doc_delete_range(doc, action->line, action->col, action->end_line, action->end_col);
            break;
            
        case edit_action::INSERT_LINE:
            {
                document_line* new_line = docline_create_with_text(action->text);
                vec_docline_insert(doc->lines, action->line, new_line);
            }
            break;
            
        case edit_action::DELETE_LINE:
            vec_docline_remove(doc->lines, action->line);
            break;
            
        case edit_action::SPLIT_LINE:
            doc_split_line(doc, action->line, action->col);
            break;

        case edit_action::JOIN_LINE:
            doc_join_lines(doc, action->line);
            break;
    }

    // Set the last edit position for cursor positioning
    doc->last_edit_line = action->cursor_line_after_redo;
    doc->last_edit_col = action->cursor_col_after_redo;
    doc->has_edit_position = true;

    doc->in_undo_redo = false;  // Re-enable undo recording
}

bool doc_can_undo(document* doc) {
    return doc && doc->max_undo_levels > 0 && doc->undo_position > 0;
}

bool doc_can_redo(document* doc) {
    return doc && doc->max_undo_levels > 0 && doc->undo_position < doc->undo_stack_size;
}

bool doc_get_last_edit_position(document* doc, u32* out_line, u32* out_col) {
    if (!doc || !doc->has_edit_position) return false;
    if (out_line) *out_line = doc->last_edit_line;
    if (out_col) *out_col = doc->last_edit_col;
    return true;
}

void doc_clear_last_edit_position(document* doc) {
    if (!doc) return;
    doc->has_edit_position = false;
}

void doc_commit_pending_undo(document* doc) {
    if (!doc) return;
    commit_word_undo(doc);
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