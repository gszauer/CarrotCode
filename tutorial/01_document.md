# Tutorial: Building a Text Editor's Document Model

This tutorial is for experienced C++ programmers who want to understand how to build a document model for a text editor from the ground up. We will start with a simple vector of strings and progressively add features like syntax highlighting and undo/redo, using the custom data structures found in CarrotCode.

## Part 1: The Core - A Document API with a Vector of Strings

The most fundamental way to represent a document is as a collection of lines. However, a robust design requires that we hide the implementation details from the rest of the editor. The editor should not know *how* the lines are stored, only that it can perform operations like "insert a character at this position." All modifications must go through a dedicated public API.

### The Basic Structure and API

Let's define a `Document` class that encapsulates a `std::vector` of `u32_string` pointers. The vector itself is private; all access is through public methods.

```cpp
#include <vector>
#include "strings.h" // Our custom string library

class Document {
public:
    // --- Public API for Document Manipulation ---

    // Inserts a single character at a specific line and column.
    void insert_char(u32 line, u32 col, u32 codepoint);

    // Deletes a single character. Handles joining lines if a newline is deleted.
    void delete_char(u32 line, u32 col);

    // Inserts a new, empty line at the specified line number.
    void insert_new_line(u32 line_num);

    // Deletes an entire line.
    void delete_line(u32 line_num);

    // Loads a file into the document.
    void load_file(const char* filepath);

    // Destructor to clean up memory.
    ~Document();

private:
    // --- Internal Data Representation ---
    std::vector<u32_string*> lines;
};
```

This API-driven approach is critical. It ensures that any changes to the internal data structure (e.g., moving to a rope or gap buffer later) won't break the rest of the editor code.

### Implementing the Core API

Let's look at how to implement these essential editing functions.

**Inserting a Character:**
This involves finding the correct line and inserting the character into its `u32_string`. Our custom string library handles the memory management, but frequent insertions can still be costly if they require reallocations.

```cpp
void Document::insert_char(u32 line, u32 col, u32 codepoint) {
    if (line >= lines.size()) return;
    u32_string* target_line = lines[line];

    // Ensure the column is valid.
    if (col > u32str_length(target_line)) {
        col = u32str_length(target_line);
    }

    // Create a temporary string for the character to be inserted.
    u32_string* char_to_insert = u32str_create();
    u32str_reserve(char_to_insert, 1);
    u32str_set(char_to_insert, 0, codepoint);

    // Insert the character.
    u32str_insert(target_line, char_to_insert, col, 0, 1);

    u32str_destroy(char_to_insert);
}
```

**Deleting a Character:**
This is the inverse of insertion. We must also handle the special case where the user deletes a newline character, which requires joining two lines.

```cpp
void Document::delete_char(u32 line, u32 col) {
    if (line >= lines.size()) return;
    u32_string* target_line = lines[line];

    if (col < u32str_length(target_line)) {
        // Simple case: delete a character within the line.
        u32str_remove(target_line, col, 1);
    } else if (col == u32str_length(target_line) && line + 1 < lines.size()) {
        // Edge case: The cursor is at the end of a line.
        // Deleting here should join this line with the next one.
        u32_string* next_line = lines[line + 1];

        // Append the next line's content to the current line.
        u32str_insert(target_line, next_line, u32str_length(target_line), 0, u32str_length(next_line));

        // Remove the now-empty next line.
        delete_line(line + 1);
    }
}

void Document::delete_line(u32 line_num) {
    if (line_num >= lines.size()) return;
    u32str_destroy(lines[line_num]);
    lines.erase(lines.begin() + line_num);
}
```

This simple model is now functional for basic editing, and its internal complexity is hidden behind a clean API. The next step is to evolve this model to handle metadata.

## Part 2: Adding Syntax Highlighting with the `Line` Concept

To associate metadata with each line, we need to introduce a new layer of abstraction: the `Line` struct. This is the approach used in CarrotCode, as defined in `syntax.h`.

### The `Line` and `token_span` Structs

First, let's define the `token_span`. This struct represents a contiguous span of text that shares the same classification (e.g., keyword, comment, literal).

```cpp
// From syntax.h
enum token_type {
    TOKEN_NONE,       // Default text
    TOKEN_KEYWORD,
    TOKEN_COMMENT,
    TOKEN_PREPROCESSOR,
    TOKEN_WHITESPACE,
    TOKEN_LITERAL
};

struct token_span {
    u32 start;      // The starting character index of the token within the line
    u32 end;        // The ending character index (exclusive)
    token_type type; // The classification of the token
};
```

Now, we can define our `Line` struct to hold both the text and an array of these tokens.

```cpp
// From syntax.h
struct document_line { // Renamed to Line for tutorial clarity
    u32_string* text;       // The text of the line
    bool dirty;             // Does this line need to be re-tokenized?
    token_span* tokens;     // A dynamic array of syntax highlighting tokens
    u32 token_count;        // Number of tokens currently in the array
    u32 token_capacity;     // Allocated capacity of the tokens array
};
```

Our document model now holds a `std::vector<Line*>`.

### The Tokenization and Rendering Workflow

The `dirty` flag is a crucial optimization. When a line is edited, we mark it as `dirty`. Before rendering, we iterate through the document and only re-tokenize the dirty lines.

```cpp
// Detailed pseudocode for tokenizing a line
void tokenize_line(Line* line) {
    // Reset the token count
    line->token_count = 0;
    u32 len = u32str_length(line->text);
    u32 i = 0;

    while (i < len) {
        u32 start = i;
        u32 current_char = u32str_get(line->text, i);

        if (is_whitespace(current_char)) {
            while (i < len && is_whitespace(u32str_get(line->text, i))) i++;
            add_token(line, start, i, TOKEN_WHITESPACE);
        } else if (current_char == '/' && i + 1 < len && u32str_get(line->text, i + 1) == '/') {
            i = len; // Consume the rest of the line as a comment
            add_token(line, start, i, TOKEN_COMMENT);
        } else if (is_identifier_start(current_char)) {
            while (i < len && is_identifier_char(u32str_get(line->text, i))) i++;
            token_type type = is_keyword(line->text, start, i) ? TOKEN_KEYWORD : TOKEN_NONE;
            add_token(line, start, i, type);
        } else {
            // Handle other cases: numbers, strings, operators...
            i++;
            add_token(line, start, i, TOKEN_NONE);
        }
    }
}

// Rendering a line using its tokens
void render_line(const Line* line, u32 screen_x, u32 screen_y) {
    u32 current_x = screen_x;
    for (u32 i = 0; i < line->token_count; ++i) {
        const token_span& token = line->tokens[i];
        Color color = get_color_for_token(token.type);
        u32_string* token_text = u32str_substr(line->text, token.start, token.end - token.start);
        
        // Draw the substring with the chosen color
        draw_text(token_text, current_x, screen_y, color);
        
        current_x += get_text_width(token_text);
        u32str_destroy(token_text);
    }
}
```

## Part 3: Implementing Undo/Redo

A robust undo/redo system requires storing actions in a way that they can be both undone and redone. This is achieved with a single action stack and a cursor (`undo_position`) that points to the current state within that stack.

### The `EditAction` Struct

An `EditAction` must store everything needed to reverse and re-apply an operation.

```cpp
struct EditAction {
    enum ActionType { INSERT_TEXT, DELETE_TEXT, ... };
    ActionType type;

    u32 line, col;
    // For range-based actions, you might add end_line, end_col

    u32_string* text; // The text that was inserted or deleted
};

struct Document {
    std::vector<Line*> lines;
    std::vector<EditAction> history; // A single stack for all actions
    int history_position; // Cursor into the history stack
};
```

### The Undo/Redo Workflow

When the user performs an action, we add it to the history stack. If the user has previously undone actions, we first discard the "redo" history.

```cpp
// Detailed implementation for inserting text
void insert_text(Document& doc, u32 line, u32 col, u32_string* text) {
    // 1. If we have undone actions, clear the future (redo) history.
    if (doc.history_position < doc.history.size()) {
        // Free memory for discarded actions
        for (size_t i = doc.history_position; i < doc.history.size(); ++i) {
            u32str_destroy(doc.history[i].text);
        }
        doc.history.resize(doc.history_position);
    }

    // 2. Perform the text insertion in the document model
    // (This could involve splitting lines if 'text' contains '\n')
    perform_text_insertion(doc, line, col, text);

    // 3. Create the action and push it to the history stack
    EditAction action;
    action.type = EditAction::INSERT_TEXT;
    action.line = line;
    action.col = col;
    action.text = u32str_substr(text, 0, u32str_length(text)); // Store a copy
    doc.history.push_back(action);
    doc.history_position++;
}
```

### The `undo` Function

Undo moves the `history_position` backward and performs the *inverse* of the action at that position.

```cpp
void undo(Document& doc) {
    if (doc.history_position == 0) return; // Nothing to undo

    doc.history_position--;
    const EditAction& action_to_undo = doc.history[doc.history_position];

    switch (action_to_undo.type) {
        case EditAction::INSERT_TEXT:
            // The inverse of inserting text is deleting it
            perform_text_deletion(doc, action_to_undo.line, action_to_undo.col, action_to_undo.text);
            break;
        case EditAction::DELETE_TEXT:
            // The inverse of deleting text is inserting it
            perform_text_insertion(doc, action_to_undo.line, action_to_undo.col, action_to_undo.text);
            break;
    }
}
```

### The `redo` Function

Redo is possible when the `history_position` is behind the end of the history stack. It moves the cursor forward and re-applies the original action.

```cpp
void redo(Document& doc) {
    if (doc.history_position >= doc.history.size()) return; // Nothing to redo

    const EditAction& action_to_redo = doc.history[doc.history_position];

    switch (action_to_redo.type) {
        case EditAction::INSERT_TEXT:
            // Re-apply the original insertion
            perform_text_insertion(doc, action_to_redo.line, action_to_redo.col, action_to_redo.text);
            break;
        case EditAction::DELETE_TEXT:
            // Re-apply the original deletion
            perform_text_deletion(doc, action_to_redo.line, action_to_redo.col, action_to_redo.text);
            break;
    }

    doc.history_position++;
}
```

## Conclusion

By starting with a simple `std::vector` of our custom `u32_string` objects, we were able to quickly create a basic document model. By progressively adding layers of abstraction—first the `Line` concept for metadata and then the `EditAction` for undo/redo—we built a powerful and extensible foundation for a modern text editor. This layered approach is key to managing complexity and creating maintainable code.