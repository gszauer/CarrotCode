#ifndef _H_RENDERER_CARROT_
#define _H_RENDERER_CARROT_

#include "types.h"

// Forward declarations
struct canvas;
struct font;

// Canvas creation and destruction

// Creates a new canvas with the specified dimensions
// Returns NULL on allocation failure
// Pixels are initialized to black (0xFF000000)
canvas* canvas_create(u32 width, u32 height);

// Destroys the canvas and frees all associated memory
void canvas_destroy(canvas* cnvs);

// Font creation and destruction

// Creates a font from raw font data
// fontData: Pointer to font data (implementation specific)
// fontBytes: Size of font data in bytes
// fontSize: Desired font size (implementation may ignore this)
// Returns NULL on failure
font* font_create(const void* fontData, u32 fontBytes, u32 fontSize);

// Destroys the font and frees all associated memory
void font_destroy(font* fnt);

// Drawing operations

// Clears the entire canvas with the specified RGB color
// The alpha channel is always set to 0xFF (fully opaque)
void canvas_clear(canvas* context, u8 r, u8 g, u8 b);

// Draws a filled rectangle at the specified position
// Rectangle is clipped to canvas bounds and current clip region
void canvas_draw_rect(canvas* context, u32 x, u32 y, u32 w, u32 h, u8 r, u8 g, u8 b);

// Draws text at the specified position
// Handles newlines (\n) and tabs (\t)
// Text is clipped to current clip region
// Returns the maximum width of all lines drawn
u32 canvas_draw_text(canvas* context, font* fnt, const u32_string* text, u32 x, u32 y, u8 r, u8 g, u8 b);

// Draws a substring of text at the specified position
// start_index: Character index to start drawing from
// length: Number of characters to draw
// Avoids memory allocation compared to creating substrings
// Returns the maximum width of all lines drawn
u32 canvas_draw_subtext(canvas* context, font* fnt, const u32_string* text, u32 start_index, u32 length, u32 x, u32 y, u8 r, u8 g, u8 b);

// Draws a C-style string at the specified position
// Handles newlines (\n) and tabs (\t)
// Text is clipped to current clip region
// Returns the maximum width of all lines drawn
u32 canvas_draw_text_cstr(canvas* context, font* fnt, const char* text, u32 x, u32 y, u8 r, u8 g, u8 b);

// Clipping operations

// Sets the clipping rectangle for all drawing operations
// Drawing outside this rectangle will be ignored
// Set w or h to 0 to disable clipping (resets to full canvas)
void canvas_set_clip(canvas* context, u32 x, u32 y, u32 w, u32 h);

// Font metrics

// Returns the height of a single line of text in pixels
// This is constant for a given font
u32 font_get_line_height(font* fnt);

// Returns the width in pixels of the specified text
// num_chars: Number of characters to measure (0 = entire string)
// Accounts for tabs and returns max width for multi-line text
u32 font_get_width(font* fnt, const u32_string* text, u32 num_chars);

// Returns the width in pixels of a substring
// start_index: Character index to start measuring from
// length: Number of characters to measure
// Avoids memory allocation compared to creating substrings
u32 font_get_subwidth(font* fnt, const u32_string* text, u32 start_index, u32 length);

// Returns the width in pixels of a single character
// Tab width is context-dependent; returns a single tab stop width
u32 font_get_char_width(font* fnt, u32 character);

// Returns the width in pixels of a C-style string
// Accounts for tabs and returns max width for multi-line text
u32 font_get_width_cstr(font* fnt, const char* text);

// Canvas accessor functions

// Returns a pointer to the raw pixel buffer (ARGB format)
// Each pixel is a u32 with format 0xAARRGGBB
// Buffer size is width * height pixels
// Returns NULL if canvas is NULL
u32* canvas_get_raw_pixels(canvas* cnvs);

// Returns the width of the canvas in pixels
// Returns 0 if canvas is NULL
u32 canvas_get_width(canvas* cnvs);

// Returns the height of the canvas in pixels
// Returns 0 if canvas is NULL
u32 canvas_get_height(canvas* cnvs);

// Document rendering

// Creates a canvas containing a rendering of a document
// highlight_syntax: If true, tokenizes lines and colors by token type
//                   If false, renders all text in default color
// Canvas size is automatically calculated to fit all content
// Caller owns the returned canvas and must free it with canvas_destroy
// Returns NULL on failure or if document is empty
canvas* canvas_debug_doc(document* doc, font* fnt, bool highlight_syntax);

#endif