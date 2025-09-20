#ifndef _H_DOCUMENT_CARROT_
#define _H_DOCUMENT_CARROT_

#include "types.h"

// Forward declaration
struct document;
struct token_span;

// Simple cursor position structure
struct document_cursor {
    u32 row;
    u32 column;
};

// Document creation and destruction

// Creates an empty document with no lines
// undo_levels: maximum number of undo operations to store (0 = no undo)
document* doc_create(u32 undo_levels);

// Destroys the document and frees all associated memory
void doc_destroy(document* doc);


// Creates a document from a u32 string, splitting on newline characters
// The input string remains owned by the caller
// undo_levels: maximum number of undo operations to store (0 = no undo)
document* doc_from_str32(u32_string* content, u32 undo_levels);

// Returns the entire document as a single u32_string with newlines between lines
// Caller owns the returned string and must free it with u32str_destroy
u32_string* doc_to_str32(document* doc);

// Basic properties

// Returns the total number of lines in the document
// An empty document has 0 lines
u32 doc_line_count(document* doc);

// Returns the length in characters (codepoints) of the specified line
// Returns 0 if line_index is out of bounds
u32 doc_get_line_length(document* doc, u32 line_index);

// Returns true if the document has been modified since creation/load
bool doc_is_modified(document* doc);

// Sets or clears the document's modified flag
// Typically cleared after saving, set automatically on edits
void doc_set_modified(document* doc, bool modified);

// Line access

// Returns a pointer to the internal u32_string for the specified line
// Returns NULL if line_index is out of bounds
// The returned string should not be modified or freed by the caller
u32_string* doc_get_line(document* doc, u32 line_index);

// returns a new u32_string that represents the selection range. It's up to the caller to release the memory
u32_string* doc_get_range(document* doc, u32 start_line, u32 start_col, u32 end_line, u32 end_col);


// Text manipulation

// Inserts a single Unicode codepoint at the specified position
// If col is beyond line length, inserts at end of line
void doc_insert_char(document* doc, u32 line, u32 col, u32 codepoint);

// Inserts text at the specified position
// If text contains newlines, will split into multiple lines
void doc_insert_str32(document* doc, u32 line, u32 col, u32_string* text);

// Deletes a single character at the specified position
// If at end of line, joins with next line
void doc_delete_char(document* doc, u32 line, u32 col);

// Deletes all text in the specified range
// Can span multiple lines
void doc_delete_range(document* doc, u32 start_line, u32 start_col, u32 end_line, u32 end_col);

// Line operations

// Inserts a new line at the specified index
// Existing lines at and after line_index are shifted down
void doc_insert_line_str32(document* doc, u32 line_index, u32_string* content);

// Appends a new line at the end of the document
void doc_append_line_str32(document* doc, u32_string* content);

// Removes the line at the specified index
// Lines after this index shift up
void doc_delete_line(document* doc, u32 line_index);

// Splits the line at the specified column position
// Text after col moves to a new line inserted below
void doc_split_line(document* doc, u32 line, u32 col);

// Joins the specified line with the next line
// The newline between them is removed
void doc_join_lines(document* doc, u32 line);

// === Undo/Redo System ===

// Undoes the last edit operation if possible
// Automatically commits any pending word-based undo before undoing
// Sets last_edit_position for cursor positioning
void doc_undo(document* doc);

// Redoes the previously undone operation if possible
// Automatically commits any pending word-based undo before redoing
// Sets last_edit_position for cursor positioning
void doc_redo(document* doc);

// Returns true if there are operations that can be undone
bool doc_can_undo(document* doc);

// Returns true if there are operations that can be redone
bool doc_can_redo(document* doc);

// === Cursor Position Management for Undo/Redo ===

// Get the last edit position after undo/redo operation
// Used by views to position cursor at the edit location
// Returns false if no position available, true if out_line/out_col were set
bool doc_get_last_edit_position(document* doc, u32* out_line, u32* out_col);

// Clear the stored last edit position
// Should be called after a view uses the position to prevent other views from using stale data
void doc_clear_last_edit_position(document* doc);

// === Word-Based Undo System ===

// Commit any pending word buffer as an INSERT_TEXT undo action
// Should be called when: cursor moves, other operations occur, or word boundary is hit
// This completes the current word grouping and prepares for the next
void doc_commit_pending_undo(document* doc);

// Syntax highlighting

// Marks a line as dirty (needs re-tokenization)
void doc_mark_line_dirty(document* doc, u32 line);

// Tokenizes a specific line if it's dirty
void doc_tokenize_line(document* doc, u32 line);

// Returns true if the line needs re-tokenization
bool doc_is_line_dirty(document* doc, u32 line);

// Returns pointer to the token array for a line (NULL if line is dirty or invalid)
token_span* doc_get_line_tokens(document* doc, u32 line_index);

// Returns the number of tokens for a line (0 if line is dirty or invalid)
u32 doc_get_line_token_count(document* doc, u32 line_index);

// === Cursor and Selection Management ===

// Get the current cursor position
document_cursor doc_get_cursor(document* doc);

// Set the cursor position (validates and clamps to document bounds)
void doc_set_cursor(document* doc, u32 row, u32 column);

// Get individual cursor components
u32 doc_get_cursor_line(document* doc);
u32 doc_get_cursor_column(document* doc);

// Set individual cursor components (validates and clamps to document bounds)
void doc_set_cursor_line(document* doc, u32 line);
void doc_set_cursor_column(document* doc, u32 column);

// Selection management
document_cursor doc_get_selection_anchor(document* doc);
void doc_set_selection_anchor(document* doc, u32 row, u32 column);
bool doc_has_selection(document* doc);
void doc_set_has_selection(document* doc, bool has_selection);
void doc_clear_selection(document* doc);

// Get normalized selection range (start is always before end)
// Returns false if no selection, true if selection exists
bool doc_get_selection_range(document* doc, document_cursor* start, document_cursor* end);

// Validate and clamp cursor to document bounds
void doc_validate_cursor(document* doc);

#endif