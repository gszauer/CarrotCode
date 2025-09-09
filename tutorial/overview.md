# CarrotCode Overview

This document provides a high-level overview of the CarrotCode text editor's architecture and components, with a focus on its core algorithms and data structures. It is intended for experienced programmers who want to quickly understand the codebase.

## Project Structure

The project is organized into several C++ files, each responsible for a specific part of the editor's functionality:

- **`linux.cpp`**: The main entry point of the application. It handles window creation, user input (keyboard and drag-and-drop), and the main event loop using the X11 library.
- **`document.h` / `document.cpp`**: Manages the text document, including loading, saving, and editing operations. It also implements an undo/redo system.
- **`renderer.h` / `debug_renderer.cpp`**: Responsible for rendering text and UI elements to the screen. It uses a custom canvas and font rendering system.
- **`syntax.h` / `syntax.cpp`**: Handles syntax highlighting by tokenizing the code into different categories (keywords, comments, etc.).
- **`strings.h` / `strings.cpp`**: A custom string library that supports UTF-8, UTF-16, and UTF-32 encoding. The editor uses UTF-32 internally.
- **`vectors.h` / `vectors.cpp`**: A generic vector implementation for managing dynamic arrays of strings and other data structures.
- **`types.h`**: Defines common data types and forward declarations for the main structs used throughout the project.
- **`font8x16.h`**: Contains the bitmap data for the 8x16 pixel font used by the renderer.
- **`build_and_run.sh`**: A simple script to compile and run the application.

## Core Components and Algorithms

### 1. Main Loop and Event Handling (`linux.cpp`)

The application's entry point is in `linux.cpp`. It uses the X11 library for windowing and event handling.

**Initialization:**
- Opens a connection to the X server using `XOpenDisplay`.
- Creates a window with `XCreateWindow`.
- Sets up event masks for exposure, key presses, and window configuration changes.
- Sets up the `WM_DELETE_WINDOW` protocol to handle window closing.
- Enables drag-and-drop functionality by setting the `XdndAware` property on the window.

### 2. Document Management (`document.cpp`)

The `document` struct is the core data structure for representing a text file. It stores the text as a vector of `document_line` structs.

**Data Structures:**

- **`document`**:
  - `vector_docline* lines`: A dynamic array of document lines.
  - `edit_action* undo_stack`: A stack for undo/redo operations.
  - `undo_position`: The current position in the undo stack.

- **`edit_action`**:
  - `action_type type`: The type of action (e.g., `INSERT_CHAR`, `DELETE_RANGE`).
  - `line`, `col`: The start position of the edit.
  - `end_line`, `end_col`: The end position for range-based edits.
  - `u32_string* text`: The text involved in the edit (e.g., the deleted text).

**Undo/Redo (Pseudocode):**

The undo/redo system is based on a stack of `edit_action`s. When an action is performed, a corresponding inverse action is pushed onto the stack.

```
function add_undo_action(doc, action):
  // If we have undone some actions, clear the "redo" part of the stack
  if (undo_position < undo_stack_size):
    clear_redo_actions()

  // If the stack is full, remove the oldest action
  if (undo_stack_size >= max_undo_levels):
    remove_oldest_action()

  // Add the new action to the stack
  undo_stack[undo_stack_size] = action
  undo_stack_size++
  undo_position = undo_stack_size

function doc_undo(doc):
  if (can_undo):
    undo_position--
    action = undo_stack[undo_position]
    // Perform the inverse of the action
    switch (action.type):
      case INSERT_CHAR:
        delete_char(action.line, action.col)
      case DELETE_CHAR:
        insert_char(action.line, action.col, action.codepoint)
      // ... and so on for other action types
```

### 3. Rendering (`debug_renderer.cpp`)

The rendering engine uses a `canvas` struct to represent a drawing surface.

**Data Structures:**

- **`canvas`**:
  - `u32* pixels`: A raw buffer of pixel data (ARGB format).
  - `width`, `height`: The dimensions of the canvas.
  - `clip_x`, `clip_y`, `clip_w`, `clip_h`: The clipping rectangle for drawing operations.

**Text Rendering (Pseudocode):**

The `canvas_draw_text` function iterates through a `u32_string` and draws each character.

```
function canvas_draw_text(context, font, text, x, y, color):
  current_x = x
  for each character in text:
    if character is '\n':
      // Move to the next line
      y += CHAR_HEIGHT
      current_x = x
    else if character is '\t':
      // Advance to the next tab stop
      spaces_to_tab = 4 - ((current_x - x) / CHAR_WIDTH) % 4
      current_x += spaces_to_tab * CHAR_WIDTH
    else:
      // Draw the character bitmap
      draw_char(context, character, current_x, y, color)
      current_x += CHAR_WIDTH
```

The `draw_char` function uses a built-in 8x16 bitmap font (`font8x16.h`) and scales it up by 2x for a final character size of 16x32.

### 4. Syntax Highlighting (`syntax.cpp`)

Syntax highlighting is performed by the `docline_tokenize` function, which splits a line of text into a series of tokens.

### 5. String and Vector Libraries (`strings.cpp`, `vectors.cpp`)

**Strings:**
- The editor uses `u32_string` (UTF-32) internally for easier indexing and manipulation of Unicode characters.
- The library provides functions for converting between UTF-8, UTF-16, and UTF-32.
- String operations like `insert` and `remove` are implemented using `memmove`.

**Vectors:**
- The vector implementation uses a dynamic array that grows automatically when needed.
- When a vector's capacity is exceeded, it is doubled to reduce the number of reallocations.