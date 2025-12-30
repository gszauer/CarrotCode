# Carrot Code

![Screenshot](carrotcode.png)

* [Run Carrot Code V2](https://gabormakesgames.com/Prototypes/Carrot/index.html)
* [Run Carrot Code V1](https://gabormakesgames.com/Prototypes/CarrotV1/index.html)

CarrotCode was inspired by [lite](https://github.com/rxi/lite). V1 focused on full Unicode rendering with OpenGL. V2 embeds [font8x16](https://github.com/hubenchang0515/font8x16/tree/master) for ASCII-only display with a tiled software renderer.

- Software rasterizer with tiled rendering
- Multi-document editing with tabbed interface
- Syntax highlighting for C/C++, JavaScript, and C#
- Undo/redo with automatic action merging
- Selection, copy/cut/paste, word navigation
- Zoom levels (50%, 100%, 200%)
- Cross-platform (Linux native, WebAssembly for browsers)

## Building and Running

### Prerequisites (Linux)

- g++ with C++17 support
- X11 development libraries

On Debian/Ubuntu:
```bash
sudo apt install build-essential libx11-dev
```

On Fedora:
```bash
sudo dnf install gcc-c++ libX11-devel
```

### Build Commands

```bash
./build_linux.sh
./carrotcode
```


## Web Version (WebAssembly)

Carrot Code runs in modern browsers via WebAssembly with no feature loss.

### Building for Web

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):

```bash
# Install and activate Emscripten (one-time setup)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

# Build Carrot Code for web
./build_emscripten.sh
```

This creates an `emscripten/` folder with:
- `index.html` - the web app
- `carrotcode.js` - JavaScript glue code
- `carrotcode.wasm` - compiled application


## Architecture Overview

```
+------------------+     +------------------+     +------------------+
|   Application    |---->|  Document View   |---->|     Document     |
|   (lifecycle)    |     |   (rendering)    |     |   (text model)   |
+------------------+     +------------------+     +------------------+
         |                        |                        |
         v                        v                        v
+------------------+     +------------------+     +------------------+
|     ImGui        |     |     Canvas       |     | Vector<DocLine>  |
|  (menu/tabs)     |     | (software rend)  |     |   (line storage) |
+------------------+     +------------------+     +------------------+
         |                                                 |
         v                                                 v
+------------------+                              +------------------+
|    Platform      |                              |    u32_string    |
| (Linux or WASM)  |                              |   (UTF-32 text)  |
+------------------+                              +------------------+
```

## How Text Editing Works:

At its core, a text editor is surprisingly simple. The document is just a **vector of strings** - one string per line. All editing operations boil down to manipulating this vector.

### The Document Structure

```cpp
struct document {
    vector_docline* lines;      // Dynamic array of text lines
    bool modified;               // True if document has unsaved changes
    // ... undo/redo state, cursor, selection ...
};
```

Each line is a `document_line` containing:
- A `u32_string` (UTF-32 encoded text)
- Cached syntax tokens for highlighting
- A "dirty" flag for re-tokenization

### Basic Operations

**Creating a document:**
```cpp
document* doc = doc_create(100, true);  // 100 undo levels, start with one empty line
```

**Getting line count and content:**
```cpp
u32 count = doc_line_count(doc);           // How many lines?
u32_string* line = doc_get_line(doc, 5);   // Get line at index 5
u32 length = doc_get_line_length(doc, 5);  // Length in characters
```

### Inserting Text

To insert a character, you specify the line and column:

```cpp
doc_insert_char(doc, line, col, 'A');  // Insert 'A' at position
```

To insert a string (which might contain newlines):

```cpp
doc_insert_str32(doc, line, col, text);  // Handles multi-line inserts
```

When inserting text with newlines, the function:
1. Finds the first newline in the text
2. Appends text before newline to the current line
3. Creates a new line for text after the newline
4. Repeats until all text is inserted

### Deleting Text

Delete a single character:
```cpp
doc_delete_char(doc, line, col);
```

If you delete at the end of a line, it joins with the next line. Delete a range:
```cpp
doc_delete_range(doc, start_line, start_col, end_line, end_col);
```

### Line Operations

```cpp
doc_split_line(doc, line, col);     // Press Enter - splits line at column
doc_join_lines(doc, line);          // Delete at end of line - joins with next
doc_delete_line(doc, line_index);   // Remove entire line
doc_insert_line_str32(doc, index, content);  // Insert new line at index
```

### The Cursor

The cursor is a row and a column.

```cpp
struct document_cursor {
    u32 row;
    u32 column;
};
```

When you move the cursor, the view updates. When you type, text is inserted at the cursor position, then the cursor advances. The document stores cursor state:

```cpp
document_cursor doc_get_cursor(document* doc);
void doc_set_cursor(document* doc, u32 row, u32 column);
```

### Selection

Selection is defined by two cursors: the current cursor position and an "anchor" point. Text between them is selected.

```cpp
bool doc_has_selection(document* doc);
void doc_set_selection_anchor(document* doc, u32 row, u32 column);
bool doc_get_selection_range(document* doc, document_cursor* start, document_cursor* end);
```

When you Shift+Arrow, the anchor stays put while the cursor moves, extending the selection.

### Undo/Redo

Every edit creates an `edit_action`:

```cpp
struct edit_action {
    enum action_type { INSERT, REMOVE } type;
    u32 line, col;           // Where it happened
    u32_string* text;        // What was inserted/removed
    u64 timestamp;           // For merging consecutive edits
};
```

Undo reverses the action (insert becomes delete, delete becomes insert). Consecutive edits within 500ms are merged - so typing "hello" creates one undo action, not five.

```cpp
void doc_undo(document* doc);
void doc_redo(document* doc);
bool doc_can_undo(document* doc);
bool doc_can_redo(document* doc);
```

## How the View System Works

The `document_view` connects the document model to rendering and user interaction. Each open tab has its own view.

```cpp
struct document_view {
    document* target;     // The document being edited
    font* fnt;            // Font for rendering
    u32_string* path;     // File path (or nullptr for unsaved)

    f32 scrollX, scrollY;           // Current scroll position
    f32 maxScrollX, maxScrollY;     // Maximum scroll values

    u32 displayAreaX, displayAreaY; // Where to render on screen
    u32 displayAreaW, displayAreaH; // Size of the editing area

    bool highlightSyntax;           // Syntax highlighting on/off
    u32 tabWidth;                   // Tab stop width (4 spaces)
};
```

### Input Handling

When you press a key:

1. `document_view_keyboard_input()` receives the key event
2. For navigation keys (arrows, Home, End), it calls cursor movement functions
3. For printable characters, it calls `document_view_insert_text()`
4. For Backspace/Delete, it calls the appropriate delete function

Mouse input follows a similar pattern - clicks are converted to cursor positions using `document_view_pixel_to_cursor()`.

### Rendering

Each frame, the view:

1. Calculates which lines are visible based on scroll position
2. For each visible line:
   - Draws selection background if selected
   - Tokenizes the line if dirty (for syntax highlighting)
   - Draws each character with the appropriate color
   - Draws the cursor if on this line
3. Draws scrollbars if content overflows

## How the Renderer Works

Carrot Code uses a software renderer that draws to an in-memory pixel buffer.

### Canvas

The canvas is a simple framebuffer:

```cpp
canvas* canvas_create(u32 width, u32 height);
void canvas_clear(canvas* cnvs, u8 r, u8 g, u8 b);
void canvas_draw_rect(canvas* cnvs, u32 x, u32 y, u32 w, u32 h, u8 r, u8 g, u8 b);
void canvas_draw_text_cstr(canvas* cnvs, font* fnt, const char* text, u32 x, u32 y, u8 r, u8 g, u8 b);
```

### Tiled Rendering

The canvas is divided into tiles. When something is drawn, only affected tiles are marked dirty. During display, only dirty tiles are uploaded to the screen. This optimization means typing a character only updates a small region, not the entire window.

Toggle the tile debug visualizer with Ctrl+Q (or in the Help menu) to see which regions redraw.

### Font

The embedded 8x16 bitmap font renders ASCII characters. Each character is a fixed 8x16 pixel glyph, making text layout trivial - character N is at position N * 8 pixels.


## How the UI Works

The UI uses an immediate-mode pattern (similar to Dear ImGui). Each frame, you call functions to process input and render controls:

```cpp
ImGuiBeginFrame(context);

// Process menu bar
ImGuiBeginMenuBar(context, 0, 0, 360, 50, activeMenu);
ImGuiMenuBarItem(context, "FILE");
ImGuiMenuBarItem(context, "EDIT");
activeMenu = ImGuiEndMenuBar(context);

// Process tabs
ImGuiBeginTabBar(context, x, y, w, h, numTabs, activeTab);
for (each tab) {
    bool open = ImGuiTab(context, tabName, isSaved);
}
activeTab = ImGuiEndTabBar(context);

ImGuiEndFrame(context);
```

There's no retained widget tree - the UI is rebuilt every frame from scratch. This sounds expensive but is actually very simple and fast for small UIs.

## How Syntax Highlighting Works

Each line stores syntax tokens:

```cpp
struct token_span {
    u32 start, end;       // Character range
    token_type type;      // KEYWORD, COMMENT, LITERAL, etc.
};
```

When a line is modified, it's marked "dirty". Before rendering, dirty lines are re-tokenized. The tokenizer is a simple state machine that recognizes:

- Keywords (if, else, for, while, etc.)
- Comments (// and /* */)
- Preprocessor directives (#include, #define)
- String and character literals
- Numbers

Each token type has a color defined in the view rendering code.


## Platform Abstraction

The `platform.h` interface abstracts OS-specific functionality:

```cpp
void platform_clipboard_copy_text(u32_string* content, callback, userData);
void platform_clipboard_paste_text(callback, userData);
void platform_open_file(callback, userData);
void platform_save_file_as(data, size, callback, userData);
void platform_write_file(path, data, size, callback, userData);
void platform_exit();
void platform_launch_browser(const char* url);
```

All operations use callbacks because they're asynchronous on the web (browser file dialogs are non-blocking).

Implementations:
- `linux.cpp` - X11 window, native file dialogs via zenity
- `emscripten.cpp` - Canvas element, browser File API


## File Organization

| Area | Files |
|------|-------|
| Entry point | `linux.cpp`, `emscripten.cpp` |
| App lifecycle | `application.h/cpp` |
| Document model | `document.h/cpp` |
| View/rendering | `view.h/cpp` |
| Syntax tokens | `syntax.h/cpp` |
| String utilities | `strings.h/cpp` |
| Software renderer | `renderer.h`, `software_renderer.cpp` |
| Immediate UI | `imgui.h/cpp` |
| Platform abstraction | `platform.h`, `linux.cpp`, `emscripten.cpp` |
| Type definitions | `types.h` |
| Embedded font | `font8x16.h` |

## MIT License

Copyright (c) 2025 [Gabor Szauer](https://gabormakesgames.com/)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
