#include "renderer.h"
#include "strings.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cmath>

#define FONT8x16_IMPLEMENTATION
#include "font8x16.h"
#undef FONT8x16_IMPLEMENTATION

struct font {
    // Placeholder; font data lives in font8x16.h
};

static_assert(sizeof(canvas_tile_region) == sizeof(int) * 4, "canvas_tile_region layout must stay packed");

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

static inline bool rect_empty(const Rect& r) {
    return r.w <= 0 || r.h <= 0;
}

static inline Rect rect_intersect(const Rect& a, const Rect& b) {
    Rect result;
    result.x = std::max(a.x, b.x);
    result.y = std::max(a.y, b.y);
    int right = std::min(a.x + a.w, b.x + b.w);
    int bottom = std::min(a.y + a.h, b.y + b.h);
    result.w = right - result.x;
    result.h = bottom - result.y;
    if (rect_empty(result)) {
        return {0, 0, 0, 0};
    }
    return result;
}

static inline bool rect_overlaps(const Rect& a, const Rect& b) {
    return !(a.x >= b.x + b.w || b.x >= a.x + a.w ||
             a.y >= b.y + b.h || b.y >= a.y + a.h);
}

struct ClipState {
    bool enabled;
    int x;
    int y;
    int w;
    int h;
};

static inline ClipState make_full_clip(u32 width, u32 height) {
    return ClipState{false, 0, 0, static_cast<int>(width), static_cast<int>(height)};
}

struct TextCommandData {
    int x;
    int y;
    u8 r;
    u8 g;
    u8 b;
    std::vector<u32> glyphs;
};

struct RectCommandData {
    int x;
    int y;
    int w;
    int h;
    u8 r;
    u8 g;
    u8 b;
};

struct ClearCommandData {
    u8 r;
    u8 g;
    u8 b;
};

enum class CommandType {
    Clear,
    Rect,
    Text
};

struct Command {
    CommandType type;
    Rect bounds;          // Area affected after clipping
    ClipState clip;       // Clip active when the command was recorded
    ClearCommandData clear;
    RectCommandData rect;
    TextCommandData text;
};

static double now_seconds() {
    using namespace std::chrono;
    static const steady_clock::time_point base = steady_clock::now();
    return duration<double>(steady_clock::now() - base).count();
}

static const u32 CHAR_WIDTH = 16;
static const u32 CHAR_HEIGHT = 32;
static const u32 TAB_WIDTH = 4 * CHAR_WIDTH;
static const u32 CELL_SIZE = 64;

static const u32 FNV_OFFSET_BASIS = 2166136261u;
static const u32 FNV_PRIME = 16777619u;

static inline void fnv1a_update(u32& hash, const void* data, size_t size) {
    const u8* ptr = static_cast<const u8*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= ptr[i];
        hash *= FNV_PRIME;
    }
}

static inline Rect clip_to_rect(u32 width, u32 height, const ClipState& clip) {
    if (!clip.enabled) {
        return Rect{0, 0, static_cast<int>(width), static_cast<int>(height)};
    }
    return Rect{clip.x, clip.y, clip.w, clip.h};
}

static inline Rect clamp_to_canvas(u32 width, u32 height, const Rect& rect) {
    Rect canvas = {0, 0, static_cast<int>(width), static_cast<int>(height)};
    return rect_intersect(rect, canvas);
}

static inline u32 pack_color(u8 r, u8 g, u8 b) {
    return 0xFF000000u | (static_cast<u32>(b) << 16) |
           (static_cast<u32>(g) << 8) | static_cast<u32>(r);
}

static void fill_rect(u32* pixels, u32 canvas_width, const Rect& rect, u32 color) {
    if (rect_empty(rect)) return;
    for (int y = 0; y < rect.h; ++y) {
        u32* row = pixels + (rect.y + y) * canvas_width + rect.x;
        for (int x = 0; x < rect.w; ++x) {
            row[x] = color;
        }
    }
}

static void draw_rect_border(u32* pixels, u32 canvas_width, const Rect& rect, u32 color) {
    if (rect_empty(rect)) return;
    fill_rect(pixels, canvas_width, Rect{rect.x, rect.y, rect.w, 1}, color);
    fill_rect(pixels, canvas_width, Rect{rect.x, rect.y + rect.h - 1, rect.w, 1}, color);
    fill_rect(pixels, canvas_width, Rect{rect.x, rect.y, 1, rect.h}, color);
    fill_rect(pixels, canvas_width, Rect{rect.x + rect.w - 1, rect.y, 1, rect.h}, color);
}

static inline u8 clamp_to_u8(double value) {
    value = std::clamp(value, 0.0, 255.0);
    return static_cast<u8>(std::lround(value));
}

static inline u32 blend_color(u32 base, u32 overlay, double alpha) {
    double inv = 1.0 - alpha;

    double base_r = static_cast<double>(base & 0xFF);
    double base_g = static_cast<double>((base >> 8) & 0xFF);
    double base_b = static_cast<double>((base >> 16) & 0xFF);

    double overlay_r = static_cast<double>(overlay & 0xFF);
    double overlay_g = static_cast<double>((overlay >> 8) & 0xFF);
    double overlay_b = static_cast<double>((overlay >> 16) & 0xFF);

    u8 r = clamp_to_u8(base_r * inv + overlay_r * alpha);
    u8 g = clamp_to_u8(base_g * inv + overlay_g * alpha);
    u8 b = clamp_to_u8(base_b * inv + overlay_b * alpha);

    return 0xFF000000u | (static_cast<u32>(b) << 16) |
           (static_cast<u32>(g) << 8) | static_cast<u32>(r);
}

static void apply_overlay(u32* pixels, u32 canvas_width, const Rect& rect, u32 color, double alpha) {
    if (rect_empty(rect) || alpha <= 0.0) return;

    for (int y = 0; y < rect.h; ++y) {
        u32* row = pixels + (rect.y + y) * canvas_width + rect.x;
        for (int x = 0; x < rect.w; ++x) {
            row[x] = blend_color(row[x], color, alpha);
        }
    }
}

static void draw_glyph(u32* pixels, u32 canvas_width, u32 canvas_height,
                       u32 character, int dst_x, int dst_y,
                       u32 color, const Rect& clip) {
    if (rect_empty(clip) || character == '\t') return;

    if (character > 127) {
        character = '?';
    }

    const unsigned char* bitmap = font8x16[character];

    for (u32 src_row = 0; src_row < 16; ++src_row) {
        unsigned char bits = bitmap[src_row];
        for (u32 src_col = 0; src_col < 8; ++src_col) {
            if (!(bits & (0x80 >> src_col))) {
                continue;
            }

            int base_x = dst_x + static_cast<int>(src_col) * 2;
            int base_y = dst_y + static_cast<int>(src_row) * 2;

            for (int dy = 0; dy < 2; ++dy) {
                int py = base_y + dy;
                if (py < clip.y || py >= clip.y + clip.h) continue;
                if (py < 0 || py >= static_cast<int>(canvas_height)) continue;

                for (int dx = 0; dx < 2; ++dx) {
                    int px = base_x + dx;
                    if (px < clip.x || px >= clip.x + clip.w) continue;
                    if (px < 0 || px >= static_cast<int>(canvas_width)) continue;

                    pixels[py * canvas_width + px] = color;
                }
            }
        }
    }
}

struct TextMetrics {
    u32 max_width;
    u32 line_count;
};

static TextMetrics calculate_text_metrics(const std::vector<u32>& glyphs) {
    u32 max_width = 0;
    u32 current_width = 0;
    u32 line_count = 0;
    bool line_has_glyph = false;

    for (u32 ch : glyphs) {
        if (ch == '\n') {
            if (line_has_glyph || current_width > 0) {
                ++line_count;
                max_width = std::max(max_width, current_width);
            }
            current_width = 0;
            line_has_glyph = false;
            continue;
        }

        if (ch == '\t') {
            u32 spaces_to_tab = 4 - ((current_width / CHAR_WIDTH) % 4);
            current_width += spaces_to_tab * CHAR_WIDTH;
            line_has_glyph = true;
            continue;
        }

        current_width += CHAR_WIDTH;
        line_has_glyph = true;
    }

    if (line_has_glyph || current_width > 0) {
        ++line_count;
        max_width = std::max(max_width, current_width);
    }

    return TextMetrics{max_width, line_count};
}

struct canvas {
    u32 width;
    u32 height;
    std::vector<u32> pixels;

    ClipState current_clip;
    ClipState default_clip;

    std::vector<Command> commands;

    u32 cell_size;
    u32 cells_x;
    u32 cells_y;
    std::vector<u32> cell_hashes;
    std::vector<u32> cell_hashes_prev;

    bool frame_dirty;
    bool first_frame;

    bool show_tile_debug;
    bool force_full_redraw;

    std::vector<int> last_overlay_cells;
    std::vector<canvas_tile_region> redraw_regions;
    std::vector<double> cell_last_update_time;
    std::vector<double> cell_flash_start_time;
    double start_time_seconds;
    double last_overlay_time;
    double flash_duration_seconds;
};

static void ensure_frame_rendered(canvas* ctx);
static void render_region(canvas* ctx, const Rect& region);
static void compute_hash_grid(canvas* ctx);
static std::vector<int> collect_dirty_cells(canvas* ctx);
static std::vector<int> collect_overlay_cells(canvas* ctx, double current_time);
static Rect cell_index_to_rect(const canvas* ctx, int index);
static void execute_text_command(const TextCommandData& data, canvas* ctx, const Rect& clip);
static void draw_debug_overlay(canvas* ctx, double current_time, const std::vector<int>& overlay_cells);

static Rect trim_rect_to_clip(canvas* ctx, const Rect& rect) {
    Rect clip_rect = clip_to_rect(ctx->width, ctx->height, ctx->current_clip);
    Rect clamped = rect_intersect(rect, clip_rect);
    return clamp_to_canvas(ctx->width, ctx->height, clamped);
}

static u32 push_text_command(canvas* ctx, std::vector<u32>&& glyphs,
                             u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!ctx) return 0;

    TextMetrics metrics = calculate_text_metrics(glyphs);
    Rect text_rect{static_cast<int>(x), static_cast<int>(y),
                   static_cast<int>(metrics.max_width),
                   static_cast<int>(metrics.line_count * CHAR_HEIGHT)};

    Rect bounds = trim_rect_to_clip(ctx, text_rect);

    if (!rect_empty(bounds)) {
        Command cmd;
        cmd.type = CommandType::Text;
        cmd.clip = ctx->current_clip;
        cmd.bounds = bounds;
        cmd.text = TextCommandData{static_cast<int>(x), static_cast<int>(y), r, g, b, std::move(glyphs)};
        ctx->commands.push_back(std::move(cmd));
        ctx->frame_dirty = true;
    }

    return metrics.max_width;
}

canvas* canvas_create(u32 width, u32 height) {
    canvas* ctx = new (std::nothrow) canvas();
    if (!ctx) {
        return nullptr;
    }

    ctx->width = width;
    ctx->height = height;
    ctx->pixels.resize(static_cast<size_t>(width) * height, 0xFF000000u);

    ctx->default_clip = make_full_clip(width, height);
    ctx->current_clip = ctx->default_clip;

    ctx->cell_size = CELL_SIZE;
    ctx->cells_x = (width + ctx->cell_size - 1) / ctx->cell_size;
    ctx->cells_y = (height + ctx->cell_size - 1) / ctx->cell_size;
    size_t cell_count = static_cast<size_t>(ctx->cells_x) * ctx->cells_y;
    ctx->cell_hashes.resize(cell_count, FNV_OFFSET_BASIS);
    ctx->cell_hashes_prev.resize(cell_count, FNV_OFFSET_BASIS);

    ctx->frame_dirty = true;
    ctx->first_frame = true;
    ctx->show_tile_debug = false;
    ctx->force_full_redraw = false;
    ctx->commands.clear();
    ctx->last_overlay_cells.clear();
    ctx->cell_last_update_time.assign(cell_count, 0.0);
    ctx->cell_flash_start_time.assign(cell_count, -1.0);
    ctx->start_time_seconds = now_seconds();
    ctx->last_overlay_time = ctx->start_time_seconds;
    ctx->flash_duration_seconds = 0.5;

    return ctx;
}

void canvas_destroy(canvas* ctx) {
    delete ctx;
}

void canvas_clear(canvas* ctx, u8 r, u8 g, u8 b) {
    if (!ctx) return;

    ctx->commands.clear();
    ctx->current_clip = ctx->default_clip;

    Command cmd;
    cmd.type = CommandType::Clear;
    cmd.clip = ctx->current_clip;
    cmd.bounds = Rect{0, 0, static_cast<int>(ctx->width), static_cast<int>(ctx->height)};
    cmd.clear = ClearCommandData{r, g, b};

    ctx->commands.push_back(std::move(cmd));
    ctx->frame_dirty = true;
}

void canvas_draw_rect(canvas* ctx, u32 x, u32 y, u32 w, u32 h, u8 r, u8 g, u8 b) {
    if (!ctx || w == 0 || h == 0) return;

    Rect raw{static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h)};
    Rect bounds = trim_rect_to_clip(ctx, raw);
    if (rect_empty(bounds)) return;

    Command cmd;
    cmd.type = CommandType::Rect;
    cmd.clip = ctx->current_clip;
    cmd.bounds = bounds;
    cmd.rect = RectCommandData{static_cast<int>(x), static_cast<int>(y),
                               static_cast<int>(w), static_cast<int>(h), r, g, b};

    ctx->commands.push_back(std::move(cmd));
    ctx->frame_dirty = true;
}

u32 canvas_draw_text(canvas* ctx, font*, const u32_string* text,
                     u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!ctx || !text) return 0;

    u32 length = u32str_length(const_cast<u32_string*>(text));
    std::vector<u32> glyphs;
    glyphs.reserve(length);
    for (u32 i = 0; i < length; ++i) {
        glyphs.push_back(u32str_get(const_cast<u32_string*>(text), i));
    }

    return push_text_command(ctx, std::move(glyphs), x, y, r, g, b);
}

u32 canvas_draw_subtext(canvas* ctx, font*, const u32_string* text, u32 start_index,
                        u32 length, u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!ctx || !text) return 0;

    u32 total = u32str_length(const_cast<u32_string*>(text));
    u32 end = std::min(start_index + length, total);
    if (start_index >= end) return 0;

    std::vector<u32> glyphs;
    glyphs.reserve(end - start_index);
    for (u32 i = start_index; i < end; ++i) {
        glyphs.push_back(u32str_get(const_cast<u32_string*>(text), i));
    }

    return push_text_command(ctx, std::move(glyphs), x, y, r, g, b);
}

u32 canvas_draw_text_cstr(canvas* ctx, font*, const char* text,
                          u32 x, u32 y, u8 r, u8 g, u8 b) {
    if (!ctx || !text) return 0;

    std::vector<u32> glyphs;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        glyphs.push_back(static_cast<u32>(*p));
    }

    return push_text_command(ctx, std::move(glyphs), x, y, r, g, b);
}

void canvas_set_clip(canvas* ctx, u32 x, u32 y, u32 w, u32 h) {
    if (!ctx) return;

    if (w == 0 && h == 0) {
        ctx->current_clip = ctx->default_clip;
        return;
    }

    u32 clipped_x = std::min(x, ctx->width);
    u32 clipped_y = std::min(y, ctx->height);
    u32 max_w = (clipped_x < ctx->width) ? ctx->width - clipped_x : 0;
    u32 max_h = (clipped_y < ctx->height) ? ctx->height - clipped_y : 0;

    u32 clipped_w = std::min(w, max_w);
    u32 clipped_h = std::min(h, max_h);

    ctx->current_clip.enabled = true;
    ctx->current_clip.x = static_cast<int>(clipped_x);
    ctx->current_clip.y = static_cast<int>(clipped_y);
    ctx->current_clip.w = static_cast<int>(clipped_w);
    ctx->current_clip.h = static_cast<int>(clipped_h);
}

u32 font_get_line_height(font*) {
    return CHAR_HEIGHT;
}

u32 font_get_width(font*, const u32_string* text, u32 num_chars) {
    if (!text) return 0;

    u32 length = num_chars ? num_chars : u32str_length(const_cast<u32_string*>(text));
    u32 max_width = 0;
    u32 current_width = 0;

    for (u32 i = 0; i < length; ++i) {
        u32 ch = u32str_get(const_cast<u32_string*>(text), i);
        if (ch == '\n') {
            max_width = std::max(max_width, current_width);
            current_width = 0;
        } else if (ch == '\t') {
            u32 spaces_to_tab = 4 - ((current_width / CHAR_WIDTH) % 4);
            current_width += spaces_to_tab * CHAR_WIDTH;
        } else {
            current_width += CHAR_WIDTH;
        }
    }

    max_width = std::max(max_width, current_width);
    return max_width;
}

u32 font_get_subwidth(font*, const u32_string* text, u32 start_index, u32 length) {
    if (!text) return 0;

    u32 total = u32str_length(const_cast<u32_string*>(text));
    u32 end = std::min(start_index + length, total);
    if (start_index >= end) return 0;

    u32 max_width = 0;
    u32 current_width = 0;

    for (u32 i = start_index; i < end; ++i) {
        u32 ch = u32str_get(const_cast<u32_string*>(text), i);
        if (ch == '\n') {
            max_width = std::max(max_width, current_width);
            current_width = 0;
        } else if (ch == '\t') {
            u32 spaces_to_tab = 4 - ((current_width / CHAR_WIDTH) % 4);
            current_width += spaces_to_tab * CHAR_WIDTH;
        } else {
            current_width += CHAR_WIDTH;
        }
    }

    max_width = std::max(max_width, current_width);
    return max_width;
}

u32 font_get_char_width(font*, u32 character) {
    if (character == '\t') {
        return TAB_WIDTH;
    }
    return CHAR_WIDTH;
}

u32 font_get_width_cstr(font*, const char* text) {
    if (!text) return 0;

    u32 max_width = 0;
    u32 current_width = 0;

    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            max_width = std::max(max_width, current_width);
            current_width = 0;
        } else if (*p == '\t') {
            u32 spaces_to_tab = 4 - ((current_width / CHAR_WIDTH) % 4);
            current_width += spaces_to_tab * CHAR_WIDTH;
        } else {
            current_width += CHAR_WIDTH;
        }
    }

    max_width = std::max(max_width, current_width);
    return max_width;
}

u32* canvas_get_raw_pixels(canvas* ctx) {
    if (!ctx) return nullptr;
    ensure_frame_rendered(ctx);
    return ctx->pixels.data();
}

u32 canvas_get_width(canvas* ctx) {
    return ctx ? ctx->width : 0;
}

u32 canvas_get_height(canvas* ctx) {
    return ctx ? ctx->height : 0;
}

font* font_create(const void*, u32, u32) {
    return new (std::nothrow) font();
}

void font_destroy(font* fnt) {
    delete fnt;
}

static Rect cell_index_to_rect(const canvas* ctx, int index) {
    int cell_x = index % static_cast<int>(ctx->cells_x);
    int cell_y = index / static_cast<int>(ctx->cells_x);

    Rect rect;
    rect.x = cell_x * static_cast<int>(ctx->cell_size);
    rect.y = cell_y * static_cast<int>(ctx->cell_size);
    rect.w = std::min(static_cast<int>(ctx->cell_size),
                      static_cast<int>(ctx->width) - rect.x);
    rect.h = std::min(static_cast<int>(ctx->cell_size),
                      static_cast<int>(ctx->height) - rect.y);
    return rect;
}

static void execute_text_command(const TextCommandData& data, canvas* ctx, const Rect& clip) {
    if (rect_empty(clip)) return;

    u32 color = pack_color(data.r, data.g, data.b);

    int current_x = data.x;
    int current_y = data.y;
    const int base_x = data.x;

    for (u32 ch : data.glyphs) {
        if (ch == '\n') {
            current_x = base_x;
            current_y += CHAR_HEIGHT;
            continue;
        }

        if (ch == '\t') {
            u32 columns = (current_x - base_x) / CHAR_WIDTH;
            u32 spaces_to_tab = 4 - (columns % 4);
            current_x += spaces_to_tab * CHAR_WIDTH;
            continue;
        }

        draw_glyph(ctx->pixels.data(), ctx->width, ctx->height, ch,
                   current_x, current_y, color, clip);
        current_x += CHAR_WIDTH;
    }
}

static void render_region(canvas* ctx, const Rect& region) {
    for (const Command& cmd : ctx->commands) {
        if (rect_empty(cmd.bounds)) continue;
        if (!rect_overlaps(cmd.bounds, region)) continue;

        Rect clip_rect = clip_to_rect(ctx->width, ctx->height, cmd.clip);
        Rect clipped = rect_intersect(clip_rect, region);
        if (rect_empty(clipped)) continue;

        switch (cmd.type) {
            case CommandType::Clear: {
                Rect target = rect_intersect(cmd.bounds, clipped);
                fill_rect(ctx->pixels.data(), ctx->width, target,
                          pack_color(cmd.clear.r, cmd.clear.g, cmd.clear.b));
                break;
            }
            case CommandType::Rect: {
                Rect raw{cmd.rect.x, cmd.rect.y, cmd.rect.w, cmd.rect.h};
                Rect target = rect_intersect(raw, clipped);
                fill_rect(ctx->pixels.data(), ctx->width, target,
                          pack_color(cmd.rect.r, cmd.rect.g, cmd.rect.b));
                break;
            }
            case CommandType::Text: {
                Rect text_clip = rect_intersect(cmd.bounds, clipped);
                execute_text_command(cmd.text, ctx, text_clip);
                break;
            }
        }
    }
}

static void compute_hash_grid(canvas* ctx) {
    std::fill(ctx->cell_hashes.begin(), ctx->cell_hashes.end(), FNV_OFFSET_BASIS);

    for (const Command& cmd : ctx->commands) {
        if (rect_empty(cmd.bounds)) continue;

        u32 command_hash = FNV_OFFSET_BASIS;
        u32 type = static_cast<u32>(cmd.type);
        fnv1a_update(command_hash, &type, sizeof(type));
        fnv1a_update(command_hash, &cmd.clip.enabled, sizeof(cmd.clip.enabled));
        fnv1a_update(command_hash, &cmd.clip.x, sizeof(cmd.clip.x));
        fnv1a_update(command_hash, &cmd.clip.y, sizeof(cmd.clip.y));
        fnv1a_update(command_hash, &cmd.clip.w, sizeof(cmd.clip.w));
        fnv1a_update(command_hash, &cmd.clip.h, sizeof(cmd.clip.h));

        switch (cmd.type) {
            case CommandType::Clear: {
                fnv1a_update(command_hash, &cmd.clear, sizeof(cmd.clear));
                break;
            }
            case CommandType::Rect: {
                fnv1a_update(command_hash, &cmd.rect, sizeof(cmd.rect));
                break;
            }
            case CommandType::Text: {
                fnv1a_update(command_hash, &cmd.text.x, sizeof(cmd.text.x));
                fnv1a_update(command_hash, &cmd.text.y, sizeof(cmd.text.y));
                fnv1a_update(command_hash, &cmd.text.r, sizeof(cmd.text.r));
                fnv1a_update(command_hash, &cmd.text.g, sizeof(cmd.text.g));
                fnv1a_update(command_hash, &cmd.text.b, sizeof(cmd.text.b));
                u32 glyph_count = static_cast<u32>(cmd.text.glyphs.size());
                fnv1a_update(command_hash, &glyph_count, sizeof(glyph_count));
                if (!cmd.text.glyphs.empty()) {
                    fnv1a_update(command_hash, cmd.text.glyphs.data(),
                                 cmd.text.glyphs.size() * sizeof(u32));
                }
                break;
            }
        }

        Rect bounds = cmd.bounds;
        int start_x = bounds.x / static_cast<int>(ctx->cell_size);
        int end_x = (bounds.x + bounds.w - 1) / static_cast<int>(ctx->cell_size);
        int start_y = bounds.y / static_cast<int>(ctx->cell_size);
        int end_y = (bounds.y + bounds.h - 1) / static_cast<int>(ctx->cell_size);

        for (int cy = start_y; cy <= end_y; ++cy) {
            for (int cx = start_x; cx <= end_x; ++cx) {
                int index = cy * static_cast<int>(ctx->cells_x) + cx;
                fnv1a_update(ctx->cell_hashes[static_cast<size_t>(index)],
                             &command_hash, sizeof(command_hash));
            }
        }
    }
}

static std::vector<int> collect_dirty_cells(canvas* ctx) {
    std::vector<int> dirty;
    const size_t count = ctx->cell_hashes.size();
    dirty.reserve(count / 4 + 1);

    for (size_t i = 0; i < count; ++i) {
        if (ctx->first_frame || ctx->cell_hashes[i] != ctx->cell_hashes_prev[i]) {
            dirty.push_back(static_cast<int>(i));
        }
    }

    return dirty;
}

static std::vector<int> collect_overlay_cells(canvas* ctx, double current_time) {
    std::vector<int> result;
    if (!ctx->show_tile_debug) {
        std::fill(ctx->cell_flash_start_time.begin(), ctx->cell_flash_start_time.end(), -1.0);
        return result;
    }

    double duration = ctx->flash_duration_seconds;
    result.reserve(ctx->cell_flash_start_time.size() / 4 + 1);

    for (size_t i = 0; i < ctx->cell_flash_start_time.size(); ++i) {
        double start = ctx->cell_flash_start_time[i];
        if (start < 0.0) {
            continue;
        }

        double elapsed = current_time - start;
        if (elapsed >= duration) {
            ctx->cell_flash_start_time[i] = -1.0;
            continue;
        }

        result.push_back(static_cast<int>(i));
    }

    return result;
}

static void draw_debug_overlay(canvas* ctx, double current_time, const std::vector<int>& overlay_cells) {
    if (!ctx->show_tile_debug) {
        return;
    }

    const u32 overlay_color = pack_color(0xFF, 0xFF, 0x00);
    const double duration = ctx->flash_duration_seconds;

    int cell_count = static_cast<int>(ctx->cell_flash_start_time.size());

    for (int index : overlay_cells) {
        if (index < 0 || index >= cell_count) {
            continue;
        }

        double start = ctx->cell_flash_start_time[static_cast<size_t>(index)];
        if (start < 0.0) {
            continue;
        }

        double elapsed = current_time - start;
        if (elapsed < 0.0) {
            elapsed = 0.0;
        }

        double t = elapsed / duration;
        if (t >= 1.0) {
            ctx->cell_flash_start_time[static_cast<size_t>(index)] = -1.0;
            continue;
        }

        double alpha = 1.0 - t;
        Rect cell_rect = cell_index_to_rect(ctx, index);
        apply_overlay(ctx->pixels.data(), ctx->width, cell_rect, overlay_color, alpha);
    }
}

static void ensure_frame_rendered(canvas* ctx) {
    if (!ctx) return;
    if (!ctx->frame_dirty && !ctx->force_full_redraw) return;

    double current_time = now_seconds();

    compute_hash_grid(ctx);
    std::vector<int> dirty_cells = collect_dirty_cells(ctx);

    if (ctx->force_full_redraw) {
        dirty_cells.clear();
        int cell_count = static_cast<int>(ctx->cell_hashes.size());
        dirty_cells.reserve(static_cast<size_t>(cell_count));
        for (int i = 0; i < cell_count; ++i) {
            dirty_cells.push_back(i);
        }
    }

    std::vector<int> overlay_cells = collect_overlay_cells(ctx, current_time);

    std::vector<int> cells_to_redraw = dirty_cells;
    cells_to_redraw.insert(cells_to_redraw.end(), overlay_cells.begin(), overlay_cells.end());
    cells_to_redraw.insert(cells_to_redraw.end(), ctx->last_overlay_cells.begin(), ctx->last_overlay_cells.end());

    std::sort(cells_to_redraw.begin(), cells_to_redraw.end());
    cells_to_redraw.erase(std::unique(cells_to_redraw.begin(), cells_to_redraw.end()), cells_to_redraw.end());

    ctx->redraw_regions.clear();
    ctx->redraw_regions.reserve(cells_to_redraw.size());

    std::vector<int> dirty_sorted = dirty_cells;
    std::sort(dirty_sorted.begin(), dirty_sorted.end());

    for (int index : cells_to_redraw) {
        Rect region = cell_index_to_rect(ctx, index);
        render_region(ctx, region);

        canvas_tile_region exposed_region{region.x, region.y, region.w, region.h};
        ctx->redraw_regions.push_back(exposed_region);

        bool is_dirty = std::binary_search(dirty_sorted.begin(), dirty_sorted.end(), index);
        if (!is_dirty) {
            continue;
        }

        double relative_time = current_time - ctx->start_time_seconds;
        if (relative_time < 0.0) {
            relative_time = 0.0;
        }
        if (index >= 0 && index < static_cast<int>(ctx->cell_last_update_time.size())) {
            ctx->cell_last_update_time[static_cast<size_t>(index)] = relative_time;
        }
        if (index >= 0 && index < static_cast<int>(ctx->cell_flash_start_time.size())) {
            ctx->cell_flash_start_time[static_cast<size_t>(index)] = current_time;
        }
    }

    if (ctx->show_tile_debug) {
        overlay_cells.insert(overlay_cells.end(), dirty_cells.begin(), dirty_cells.end());
        std::sort(overlay_cells.begin(), overlay_cells.end());
        overlay_cells.erase(std::unique(overlay_cells.begin(), overlay_cells.end()), overlay_cells.end());
    }

    draw_debug_overlay(ctx, current_time, overlay_cells);

    ctx->last_overlay_cells = overlay_cells;

    std::swap(ctx->cell_hashes, ctx->cell_hashes_prev);
    ctx->frame_dirty = false;
    ctx->first_frame = false;
    ctx->force_full_redraw = false;
    ctx->last_overlay_time = current_time;

    if (!ctx->show_tile_debug) {
        ctx->last_overlay_cells.clear();
    }
}

void canvas_set_tile_debug_enabled(canvas* ctx, bool enabled) {
    if (!ctx) return;
    ctx->show_tile_debug = enabled;
    ctx->force_full_redraw = true;
    ctx->frame_dirty = true;
    if (!enabled) {
        std::fill(ctx->cell_flash_start_time.begin(), ctx->cell_flash_start_time.end(), -1.0);
        ctx->last_overlay_cells.clear();
    }
}

const canvas_tile_region* canvas_get_redraw_regions(canvas* ctx, u32* out_count) {
    if (!ctx) {
        if (out_count) {
            *out_count = 0;
        }
        return nullptr;
    }

    ensure_frame_rendered(ctx);

    if (out_count) {
        *out_count = static_cast<u32>(ctx->redraw_regions.size());
    }

    if (ctx->redraw_regions.empty()) {
        return nullptr;
    }

    return ctx->redraw_regions.data();
}
