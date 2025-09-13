# Tutorial: The CarrotCode Renderer API

This tutorial explains the design and philosophy of the CarrotCode renderer. The API, defined in `renderer.h`, provides an abstraction layer for 2D drawing operations. This separates the core editor logic from the specifics of how rendering is actually performed, allowing for different backends (software, OpenGL, etc.) without changing the editor code.

## Core Concepts

### The Canvas: An Abstract Drawing Surface

A `canvas` should be thought of as an abstract drawing surface or a render target. It is a handle to a resource where pixel data can be drawn. The rest of the application does not need to know how the canvas is stored or managed in memory; it simply uses the API to create a canvas and draw on it.

All drawing operations, such as `canvas_draw_rect`, are commands sent to the rendering system to modify a specific canvas.

### The Font: An Abstract Typeface

A `font` is a handle to a loaded typeface at a specific size. The API is designed to abstract away the complexities of font file parsing, rasterization, and glyph management.

Conceptually, `font_create` would take the raw data of a font file (e.g., a `.ttf` or `.otf` file), process it using a library like FreeType, and prepare it for rendering. The current CarrotCode backend uses a simple built-in bitmap font, but the API is designed to support this more advanced, true font system. The rest of the application simply requests to draw text using a `font` handle, and the renderer takes care of the details.

## Function Reference

### Canvas Management

**`canvas* canvas_create(u32 width, u32 height)`**
- **Description**: Requests a new canvas (drawing surface) of a given size.
- **Conceptual Implementation**: The rendering backend allocates a new pixel buffer or texture of the specified dimensions. It initializes the canvas state (e.g., setting a default clipping rectangle) and returns a handle to this new resource.
- **Example**:
```c
// Request an 800x600 drawing surface from the renderer.
canvas* main_canvas = canvas_create(800, 600);
```

**`void canvas_destroy(canvas* cnvs)`**
- **Description**: Releases all resources associated with a canvas.
- **Conceptual Implementation**: This tells the rendering backend to free the pixel buffer or texture associated with the canvas handle, along with any other related resources.
- **Example**:
```c
canvas* c = canvas_create(100, 100);
canvas_destroy(c); // Release the canvas resources.
```

### Font Management

**`font* font_create(const void* fontData, u32 fontBytes, u32 fontSize)`**
- **Description**: Creates a font resource from font file data.
- **Conceptual Implementation**: This function would parse the raw `fontData` using a font library. It would rasterize the glyphs needed at the specified `fontSize` (or prepare a vector format) and store them in a texture atlas or other internal format. It then returns a handle to this font resource.
- **Example**:
```c
// In a full implementation, you would load a .ttf file into a buffer.
// char* font_file_data = read_file("my_font.ttf");
// font* my_font = font_create(font_file_data, file_size, 16);
font* my_font = font_create(NULL, 0, 16); // Using the current backend.
```

**`void font_destroy(font* fnt)`**
- **Description**: Releases all resources associated with a font.
- **Conceptual Implementation**: This tells the rendering backend to free any textures, glyph caches, and other memory associated with the font handle.
- **Example**:
```c
font* f = font_create(NULL, 0, 0);
font_destroy(f);
```

### Drawing Primitives

**`void canvas_clear(canvas* context, u8 r, u8 g, u8 b)`**
- **Description**: Issues a command to fill a canvas with a solid color.
- **Conceptual Implementation**: The renderer receives this command and performs the most efficient operation to clear the target buffer. For a software renderer, this is a loop; for a hardware renderer, this would be a highly optimized GPU clear command.
- **Example**:
```c
// Clear the canvas to a dark blue color.
canvas_clear(main_canvas, 10, 20, 50);
```

**`void canvas_draw_rect(canvas* context, u32 x, u32 y, u32 w, u32 h, u8 r, u8 g, u8 b)`**
- **Description**: Issues a command to draw a filled rectangle.
- **Conceptual Implementation**: The renderer receives the rectangle's dimensions and color. It translates these into the appropriate drawing commands for the backend, respecting the canvas's current clipping rectangle. On a GPU, this would be a command to draw two triangles that form the rectangle.
- **Example**:
```c
// Draw a 50x30 red rectangle at position (10, 20).
canvas_draw_rect(main_canvas, 10, 20, 50, 30, 255, 0, 0);
```

### Text Rendering

**`u32 canvas_draw_text(canvas* context, font* fnt, const u32_string* text, u32 x, u32 y, u8 r, u8 g, u8 b)`**
- **Description**: Renders a string of text onto the canvas using a specified font.
- **Conceptual Implementation**: The function iterates through the string. For each character, it queries the `font` resource to get the glyph information (bitmap, size, and metrics). It then issues a command to the renderer to draw that glyph's texture/bitmap at the correct position on the `canvas`. The cursor position is advanced based on the font metrics.
- **Example**:
```c
u32_string* hello = u32str_init((u32[]){L'H', L'e', L'l', L'l', L'o', 0});
canvas_draw_text(main_canvas, main_font, hello, 10, 10, 255, 255, 255);
u32str_destroy(hello);
```

### Font Metrics

**`u32 font_get_line_height(font* fnt)`**
- **Description**: Queries the font resource for its default line height.
- **Conceptual Implementation**: This function accesses the font's internal metrics, which were calculated when the font was created. It would typically return the sum of the font's ascent and descent.

**`u32 font_get_width(font* fnt, const u32_string* text, u32 num_chars)`**
- **Description**: Calculates the rendered width of a string of text.
- **Conceptual Implementation**: This function simulates a draw operation without actually rendering. It iterates through the string, querying the font for each character's advance width and summing them up. It would also account for kerning between character pairs for more advanced implementations.

### High-Level Rendering

**`canvas* canvas_debug_doc(document* doc, font* fnt, bool highlight_syntax)`**
- **Description**: A high-level utility function that renders an entire document to a new canvas.
- **Conceptual Implementation**: This function acts as a client of the renderer API. It calculates the total size of the document, creates a new canvas of that size, and then iterates through the document's lines and tokens, using the renderer's drawing functions (`canvas_draw_text`, `canvas_draw_subtext`) to draw the document content onto the new canvas. It is a higher-level composition of the primitive drawing functions.
- **Example**:
```c
// Create a complete image of the document with syntax highlighting.
canvas* doc_image = canvas_debug_doc(my_doc, my_font, true);
```