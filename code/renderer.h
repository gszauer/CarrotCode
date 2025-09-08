#ifndef _H_RENDERER_CARROT_
#define _H_RENDERER_CARROT_

#include "types.h"

struct canvas;
struct font;

canvas* canvas_create(u32 width, u32 height);
void canvas_destroy(canvas* cnvs);

font* font_create(const void* fontData, u32 fontBytes, u32 fontSize);
void font_destroy(font* fnt);

void canvas_clear(canvas* context, u8 r, u8 g, u8 b);
void canvas_draw_rect(canvas* context, u32 x, u32 y, u32 w, u32 h, u8 r, u8 g, u8 b);
u32 canvas_draw_text(canvas* context, font* fnt, const u32_string* text, u32 x, u32 y, u8 r, u8 g, u8 b); // returns width of text drawn (after clipping)
void canvas_set_clip(canvas* context, u32 x, u32 y, u32 w, u32 h); // set w or h to 0 to disable. disabled clip is 0, 0, context width, context height

u32 font_get_line_height(font* fnt);
u32 font_get_width(font* fnt, const u32_string* text, u32 num_chars); // num_chars = 0 means the whole string
u32 font_get_char_width(font* fnt, u32 character);

// Canvas accessor functions
u32* canvas_get_raw_pixels(canvas* cnvs);
u32 canvas_get_width(canvas* cnvs);
u32 canvas_get_height(canvas* cnvs);

// Debug rendering for document with syntax highlighting
canvas* canvas_debug_doc(document* doc, font* fnt);

#endif