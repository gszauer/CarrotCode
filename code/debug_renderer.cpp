#include "renderer.h"
#include "strings.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cstdio>

// Include font8x16 with implementation
#define FONT8x16_IMPLEMENTATION
#include "font8x16.h"
#undef FONT8x16_IMPLEMENTATION

struct canvas {
    u32* pixels;
    u32 width;
    u32 height;
    u32 clip_x;
    u32 clip_y;
    u32 clip_w;
    u32 clip_h;
};

struct font {
    // Font8x16 is built-in, no additional data needed
};

// Font8x16 dimensions - doubled to 16x32 for display
static const u32 CHAR_WIDTH = 16;  // Display width (2x original)
static const u32 CHAR_HEIGHT = 32; // Display height (2x original)
static const u32 TAB_WIDTH = 4 * CHAR_WIDTH; // Tab is 4 spaces wide

// Removed old bitmap_font array - now using font8x16

canvas* canvas_create(u32 width, u32 height) {
    canvas* cnvs = (canvas*)malloc(sizeof(canvas));
    cnvs->width = width;
    cnvs->height = height;
    cnvs->pixels = (u32*)malloc(width * height * sizeof(u32));
    
    cnvs->clip_x = 0;
    cnvs->clip_y = 0;
    cnvs->clip_w = width;
    cnvs->clip_h = height;
    
    for (u32 i = 0; i < width * height; i++) {
        cnvs->pixels[i] = 0xFF000000;
    }
    
    return cnvs;
}

void canvas_destroy(canvas* cnvs) {
    if (cnvs) {
        free(cnvs->pixels);
        free(cnvs);
    }
}

font* font_create(const void* fontData, u32 fontBytes, u32 fontSize) {
    return 0;
}

void font_destroy(font* fnt) {
}

void canvas_clear(canvas* context, u8 r, u8 g, u8 b) {
    u32 color = 0xFF000000 | (b << 16) | (g << 8) | r;
    for (u32 i = 0; i < context->width * context->height; i++) {
        context->pixels[i] = color;
    }
}

static inline bool is_pixel_in_clip(canvas* context, u32 x, u32 y) {
    return x >= context->clip_x && x < context->clip_x + context->clip_w &&
           y >= context->clip_y && y < context->clip_y + context->clip_h;
}

void canvas_draw_rect(canvas* context, u32 x, u32 y, u32 w, u32 h, u8 r, u8 g, u8 b) {
    u32 color = 0xFF000000 | (b << 16) | (g << 8) | r;
    
    u32 start_x = std::max(x, context->clip_x);
    u32 start_y = std::max(y, context->clip_y);
    u32 end_x = std::min(x + w, context->clip_x + context->clip_w);
    u32 end_y = std::min(y + h, context->clip_y + context->clip_h);
    
    if (start_x >= end_x || start_y >= end_y) {
        return;
    }
    
    for (u32 py = start_y; py < end_y; py++) {
        for (u32 px = start_x; px < end_x; px++) {
            context->pixels[py * context->width + px] = color;
        }
    }
}

static void draw_char(canvas* context, u32 character, u32 x, u32 y, u8 r, u8 g, u8 b) {
    u32 color = 0xFF000000 | (b << 16) | (g << 8) | r;
    
    // Handle special characters
    if (character == '\t') {
        // Tab is handled by the caller (canvas_draw_text)
        return;
    }
    
    // If character is out of font8x16 range (0-255), use '?'
    if (character > 255) {
        character = '?';
    }
    
    // Get the character bitmap from font8x16
    const unsigned char* char_bitmap = font8x16[character];
    
    // Draw the character with 2x nearest neighbor scaling (16x32 pixels)
    for (u32 src_row = 0; src_row < 16; src_row++) {
        unsigned char bitmap_row = char_bitmap[src_row];
        for (u32 src_col = 0; src_col < 8; src_col++) {
            // font8x16 uses MSB first (0x80 is leftmost pixel)
            if (bitmap_row & (0x80 >> src_col)) {
                // Draw 2x2 pixel block for each source pixel (nearest neighbor)
                for (u32 dy = 0; dy < 2; dy++) {
                    for (u32 dx = 0; dx < 2; dx++) {
                        u32 px = x + (src_col * 2) + dx;
                        u32 py = y + (src_row * 2) + dy;
                        if (is_pixel_in_clip(context, px, py) && px < context->width && py < context->height) {
                            context->pixels[py * context->width + px] = color;
                        }
                    }
                }
            }
        }
    }
}

u32 canvas_draw_text(canvas* context, font* fnt, const u32_string* text, u32 x, u32 y, u8 r, u8 g, u8 b) {
    u32 max_width = 0;
    u32 current_x = x;
    u32 length = u32str_length((u32_string*)text);
    
    for (u32 i = 0; i < length; i++) {
        u32 character = u32str_get((u32_string*)text, i);
        
        if (character == '\n') {
            max_width = std::max(max_width, current_x - x);
            current_x = x;
            y += CHAR_HEIGHT;
            continue;
        }
        
        if (character == '\t') {
            // Handle tab - advance to next tab stop (4 spaces)
            u32 spaces_to_tab = 4 - ((current_x - x) / CHAR_WIDTH) % 4;
            current_x += spaces_to_tab * CHAR_WIDTH;
            continue;
        }
        
        // Draw the character if it fits in the clip region
        if (current_x + CHAR_WIDTH <= context->clip_x + context->clip_w) {
            draw_char(context, character, current_x, y, r, g, b);
        }
        
        // Advance cursor by character width
        current_x += CHAR_WIDTH;
    }
    
    max_width = std::max(max_width, current_x - x);
    return max_width;
}

void canvas_set_clip(canvas* context, u32 x, u32 y, u32 w, u32 h) {
    if (w == 0 && h == 0) {
        context->clip_x = 0;
        context->clip_y = 0;
        context->clip_w = context->width;
        context->clip_h = context->height;
    } else {
        context->clip_x = std::min(x, context->width);
        context->clip_y = std::min(y, context->height);
        context->clip_w = std::min(w, context->width - context->clip_x);
        context->clip_h = std::min(h, context->height - context->clip_y);
    }
}

u32 font_get_line_height(font* fnt) {
    return CHAR_HEIGHT;
}

u32 font_get_width(font* fnt, const u32_string* text, u32 num_chars) {
    u32 length = num_chars;
    if (num_chars == 0) {
        length = u32str_length((u32_string*)text);
    }
    
    u32 max_width = 0;
    u32 current_width = 0;
    u32 current_x = 0; // Track position for tab calculation
    
    for (u32 i = 0; i < length; i++) {
        u32 character = u32str_get((u32_string*)text, i);
        if (character == '\n') {
            max_width = std::max(max_width, current_width);
            current_width = 0;
            current_x = 0;
        } else if (character == '\t') {
            // Handle tab - advance to next tab stop (4 spaces)
            u32 spaces_to_tab = 4 - (current_x / CHAR_WIDTH) % 4;
            u32 tab_width = spaces_to_tab * CHAR_WIDTH;
            current_width += tab_width;
            current_x += tab_width;
        } else {
            current_width += CHAR_WIDTH;
            current_x += CHAR_WIDTH;
        }
    }
    
    max_width = std::max(max_width, current_width);
    return max_width;
}

u32 font_get_char_width(font* fnt, u32 character) {
    if (character == '\t') {
        // Tab width is context-dependent, return single tab stop for simplicity
        return TAB_WIDTH;
    }
    return CHAR_WIDTH;
}