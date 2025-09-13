# Tutorial: An In-Depth Analysis of `debug_renderer.cpp`

This tutorial provides a detailed, function-by-function walkthrough of the `debug_renderer.cpp` source file. Unlike the other tutorials, which focus on the abstract API, this guide dives into the specific C++ code that implements our portable software renderer. We will analyze the implementation of each function in an inline style.

## File-level Constants and Structs

Before the functions, the file defines the concrete structures for `canvas` and `font` and some constants for text rendering.

```cpp
// The canvas struct holds the state for a single drawing surface.
struct canvas {
    u32* pixels; // The buffer of 32-bit pixels (0xAARRGGBB)
    u32 width;
    u32 height;
    // The clipping rectangle, which restricts all drawing operations.
    u32 clip_x;
    u32 clip_y;
    u32 clip_w;
    u32 clip_h;
};

// The font struct is a placeholder, as the font data is hardcoded.
struct font {
    // Font8x16 is built-in, no additional data needed
};

// The hardcoded dimensions of our 8x16 font, scaled up by 2x.
static const u32 CHAR_WIDTH = 16;  // Display width (2x original)
static const u32 CHAR_HEIGHT = 32; // Display height (2x original)
static const u32 TAB_WIDTH = 4 * CHAR_WIDTH; // Tab is 4 spaces wide
```

## Function Implementation Walkthrough

### `canvas* canvas_create(u32 width, u32 height)`

This function allocates and initializes a new `canvas`.

```cpp
canvas* canvas_create(u32 width, u32 height) {
    // First, allocate memory for the canvas struct itself using malloc.
    canvas* cnvs = (canvas*)malloc(sizeof(canvas));
    cnvs->width = width;
    cnvs->height = height;

    // Allocate the main pixel buffer, a contiguous block of memory
    // large enough to hold every 32-bit pixel in the canvas.
    cnvs->pixels = (u32*)malloc(width * height * sizeof(u32));
    
    // By default, the clipping (drawable) area is the entire canvas.
    cnvs->clip_x = 0;
    cnvs->clip_y = 0;
    cnvs->clip_w = width;
    cnvs->clip_h = height;
    
    // For a predictable starting state, initialize every pixel to opaque black.
    for (u32 i = 0; i < width * height; i++) {
        cnvs->pixels[i] = 0xFF000000;
    }
    
    // Return the pointer to the newly created canvas.
    return cnvs;
}
```

### `void canvas_destroy(canvas* cnvs)`

This function frees all memory used by a `canvas`.

```cpp
void canvas_destroy(canvas* cnvs) {
    // It's safe to call free on NULL, but we check the canvas pointer anyway.
    if (cnvs) {
        // It is critical to free the pixel buffer first.
        free(cnvs->pixels);
        // Then, free the struct that contained the pointer.
        free(cnvs);
    }
}
```

### `font* font_create(...)` and `void font_destroy(font* fnt)`

These are placeholder functions to satisfy the API.

```cpp
font* font_create(const void* fontData, u32 fontBytes, u32 fontSize) {
    // Since the font is hardcoded, we just allocate a dummy struct.
    font* fnt = (font*)malloc(sizeof(font));
    return fnt;
}

void font_destroy(font* fnt) {
    // Free the dummy struct.
    if (fnt) {
        free(fnt);
    }
}
```

### `void canvas_clear(canvas* context, u8 r, u8 g, u8 b)`

Fills the entire canvas with a single color.

```cpp
void canvas_clear(canvas* context, u8 r, u8 g, u8 b) {
    // First, construct the 32-bit color value from the r, g, b components.
    // The 0xFF000000 sets the alpha channel to 255 (fully opaque).
    u32 color = 0xFF000000 | (b << 16) | (g << 8) | r;

    // Iterate through the entire pixel buffer and set every pixel.
    for (u32 i = 0; i < context->width * context->height; i++) {
        context->pixels[i] = color;
    }
}
```

### `static inline bool is_pixel_in_clip(...)`

This is a small helper function to check if a coordinate is within the drawable area.

```cpp
static inline bool is_pixel_in_clip(canvas* context, u32 x, u32 y) {
    // A simple bounds check against the canvas's clipping rectangle.
    return x >= context->clip_x && x < context->clip_x + context->clip_w &&
           y >= context->clip_y && y < context->clip_y + context->clip_h;
}
```

### `void canvas_draw_rect(...)`

Draws a filled rectangle, respecting the clipping area.

```cpp
void canvas_draw_rect(canvas* context, u32 x, u32 y, u32 w, u32 h, u8 r, u8 g, u8 b) {
    u32 color = 0xFF000000 | (b << 16) | (g << 8) | r;
    
    // Determine the actual drawing area by finding the intersection of the
    // requested rectangle and the canvas's clipping rectangle.
    u32 start_x = std::max(x, context->clip_x);
    u32 start_y = std::max(y, context->clip_y);
    u32 end_x = std::min(x + w, context->clip_x + context->clip_w);
    u32 end_y = std::min(y + h, context->clip_y + context->clip_h);
    
    // If the rectangle is entirely outside the clip region, there's nothing to do.
    if (start_x >= end_x || start_y >= end_y) {
        return;
    }
    
    // Use nested loops to iterate over the clipped drawing area.
    for (u32 py = start_y; py < end_y; py++) {
        for (u32 px = start_x; px < end_x; px++) {
            // Set the pixel at the correct memory location.
            context->pixels[py * context->width + px] = color;
        }
    }
}
```

### `static void draw_char(...)`

This is the core text-rendering helper function. It draws one character.

```cpp
static void draw_char(canvas* context, u32 character, u32 x, u32 y, u8 r, u8 g, u8 b) {
    u32 color = 0xFF000000 | (b << 16) | (g << 8) | r;
    
    // If the character is outside the font's range (0-255), use a placeholder.
    if (character > 255) {
        character = '?';
    }
    
    // Get the 16-byte bitmap for this character from the hardcoded font data.
    const unsigned char* char_bitmap = font8x16[character];
    
    // Loop through each row of the 8x16 source bitmap.
    for (u32 src_row = 0; src_row < 16; src_row++) {
        unsigned char bitmap_row = char_bitmap[src_row];
        // Loop through each of the 8 bits in the current row.
        for (u32 src_col = 0; src_col < 8; src_col++) {
            // Check if the bit is set. 0x80 is 10000000 in binary.
            if (bitmap_row & (0x80 >> src_col)) {
                // If the bit is set, draw a 2x2 pixel block on the canvas.
                // This performs a 2x nearest-neighbor scale on the font.
                for (u32 dy = 0; dy < 2; dy++) {
                    for (u32 dx = 0; dx < 2; dx++) {
                        u32 px = x + (src_col * 2) + dx;
                        u32 py = y + (src_row * 2) + dy;
                        // Before drawing, ensure the pixel is within the clip region.
                        if (is_pixel_in_clip(context, px, py) && px < context->width && py < context->height) {
                            context->pixels[py * context->width + px] = color;
                        }
                    }
                }
            }
        }
    }
}
```

### `u32 canvas_draw_text(...)` and `u32 canvas_draw_subtext(...)`

These functions orchestrate the drawing of strings.

```cpp
u32 canvas_draw_text(canvas* context, font* fnt, const u32_string* text, u32 x, u32 y, u8 r, u8 g, u8 b) {
    u32 max_width = 0;
    u32 current_x = x;
    u32 length = u32str_length((u32_string*)text);
    
    // Loop through each character in the string.
    for (u32 i = 0; i < length; i++) {
        u32 character = u32str_get((u32_string*)text, i);
        
        // Handle newline characters by advancing the y-cursor.
        if (character == '\n') {
            max_width = std::max(max_width, current_x - x);
            current_x = x;
            y += CHAR_HEIGHT;
            continue;
        }
        
        // Handle tabs by advancing the x-cursor to the next tab stop.
        if (character == '\t') {
            u32 spaces_to_tab = 4 - ((current_x - x) / CHAR_WIDTH) % 4;
            current_x += spaces_to_tab * CHAR_WIDTH;
            continue;
        }
        
        // For any other character, call the helper function to draw it.
        draw_char(context, character, current_x, y, r, g, b);
        
        // Advance the x-cursor by the character width.
        current_x += CHAR_WIDTH;
    }
    
    max_width = std::max(max_width, current_x - x);
    return max_width;
}

// canvas_draw_subtext is identical, but its loop runs over a smaller range
// of the input string, making it more efficient than creating a new substring.
```

### `void canvas_set_clip(...)`

This function updates the clipping rectangle.

```cpp
void canvas_set_clip(canvas* context, u32 x, u32 y, u32 w, u32 h) {
    // A special case: w=0 and h=0 means reset the clipping rectangle
    // to the full size of the canvas.
    if (w == 0 && h == 0) {
        context->clip_x = 0;
        context->clip_y = 0;
        context->clip_w = context->width;
        context->clip_h = context->height;
    } else {
        // Otherwise, set the clip rectangle, ensuring it does not
        // exceed the canvas's own boundaries.
        context->clip_x = std::min(x, context->width);
        context->clip_y = std::min(y, context->height);
        context->clip_w = std::min(w, context->width - context->clip_x);
        context->clip_h = std::min(h, context->height - context->clip_y);
    }
}
```

### Font Metric Functions (`font_get_*`)

These functions return information about the font's dimensions.

```cpp
// Since the font is a fixed-size bitmap, these functions simply
// return the hardcoded constants defined at the top of the file.
u32 font_get_line_height(font* fnt) {
    return CHAR_HEIGHT;
}

u32 font_get_char_width(font* fnt, u32 character) {
    if (character == '\t') {
        return TAB_WIDTH;
    }
    return CHAR_WIDTH;
}

// The width functions iterate through a string and sum the results
// of font_get_char_width for each character to get the total width.
u32 font_get_width(font* fnt, const u32_string* text, u32 num_chars) { ... }
u32 font_get_subwidth(font* fnt, const u32_string* text, u32 start_index, u32 length) { ... }
```

### Canvas Accessors (`canvas_get_*`)

These functions provide read-only access to the canvas's properties.

```cpp
// These are simple accessor functions that return the corresponding
// field from the canvas struct. They provide a safe, read-only
// interface to the canvas's state.
u32* canvas_get_raw_pixels(canvas* cnvs) {
    if (!cnvs) return nullptr;
    return cnvs->pixels;
}

u32 canvas_get_width(canvas* cnvs) {
    if (!cnvs) return 0;
    return cnvs->width;
}

u32 canvas_get_height(canvas* cnvs) {
    if (!cnvs) return 0;
    return cnvs->height;
}
```

### `canvas* canvas_debug_doc(...)`

This is a high-level function that combines many of the previous functions to render an entire document.

```cpp
canvas* canvas_debug_doc(document* doc, font* fnt, bool highlight_syntax) {
    // 1. Calculate required canvas size by finding the longest line
    //    and multiplying the number of lines by the line height.
    u32 max_width = 0;
    for (u32 i = 0; i < doc_line_count(doc); i++) {
        max_width = std::max(max_width, font_get_width(fnt, doc_get_line(doc, i), 0));
    }
    u32 canvas_height = doc_line_count(doc) * font_get_line_height(fnt);

    // 2. Create the canvas and clear it to a background color.
    canvas* cnvs = canvas_create(max_width, canvas_height);
    canvas_clear(cnvs, 30, 30, 40);

    // 3. Loop through each line of the document.
    for (u32 line_idx = 0; line_idx < doc_line_count(doc); line_idx++) {
        u32_string* line_text = doc_get_line(doc, line_idx);
        u32 y = line_idx * font_get_line_height(fnt);

        if (!highlight_syntax) {
            // 4a. If syntax highlighting is off, draw the whole line with a default color.
            canvas_draw_text(cnvs, fnt, line_text, 0, y, 200, 200, 200);
        } else {
            // 4b. If syntax highlighting is on, tokenize the line.
            doc_tokenize_line(doc, line_idx);
            token_span* tokens = doc_get_line_tokens(doc, line_idx);
            u32 token_count = doc_get_line_token_count(doc, line_idx);
            u32 x = 0;

            // 5. Loop through the tokens for the current line.
            for (u32 tok_idx = 0; tok_idx < token_count; tok_idx++) {
                // 6. Select a color based on the token's type.
                const u8* color = get_color_for_token(tokens[tok_idx].type);
                // 7. Draw just this token's substring.
                canvas_draw_subtext(cnvs, fnt, line_text, tokens[tok_idx].start, 
                                  tokens[tok_idx].end - tokens[tok_idx].start, x, y, 
                                  color[0], color[1], color[2]);
                // 8. Advance the x-cursor for the next token.
                x += font_get_subwidth(fnt, line_text, tokens[tok_idx].start, 
                                     tokens[tok_idx].end - tokens[tok_idx].start);
            }
        }
    }
    
    return cnvs;
}
```
