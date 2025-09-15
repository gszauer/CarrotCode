#include "imgui.h"
#include "renderer.h"
#include "strings.h"
#include <stdlib.h>
#include <string.h>

// Adobe Spectrum Dark color theme
namespace Colors {
    // Corresponds to Spectrum's "gray-75"
    const u8 BACKGROUND_R = 31, BACKGROUND_G = 31, BACKGROUND_B = 31;

    // Corresponds to Spectrum's "gray-100"
    const u8 SURFACE_R = 39, SURFACE_G = 39, SURFACE_B = 39;

    // Based on Spectrum's gray tones for component backgrounds/borders
    const u8 CONTROL_R = 80, CONTROL_G = 80, CONTROL_B = 80;
    const u8 CONTROL_HOVER_R = 92, CONTROL_HOVER_G = 92, CONTROL_HOVER_B = 92;
    const u8 CONTROL_ACTIVE_R = 69, CONTROL_ACTIVE_G = 69, CONTROL_ACTIVE_B = 69;

    // Corresponds to Spectrum's primary Call To Action (CTA) blue
    const u8 PRIMARY_R = 38, PRIMARY_G = 128, PRIMARY_B = 235;
    const u8 PRIMARY_HOVER_R = 20, PRIMARY_HOVER_G = 115, PRIMARY_HOVER_B = 230;
    const u8 PRIMARY_ACTIVE_R = 13, PRIMARY_ACTIVE_G = 102, PRIMARY_ACTIVE_B = 208;

    // Corresponds to Spectrum's "gray-900"
    const u8 TEXT_R = 226, TEXT_G = 226, TEXT_B = 226;

    // Corresponds to Spectrum's "gray-700"
    const u8 TEXT_DISABLED_R = 155, TEXT_DISABLED_G = 155, TEXT_DISABLED_B = 155;

    // Corresponds to Spectrum's "gray-400"
    const u8 BORDER_R = 80, BORDER_G = 80, BORDER_B = 80;

    // The primary blue, used for checkmarks and other selected states
    const u8 CHECK_R = 38, CHECK_G = 128, CHECK_B = 235;

    // Shadow color for popup menus (darker than background)
    const u8 SHADOW_R = 20, SHADOW_G = 20, SHADOW_B = 20;
}

struct ImGui {
    canvas* cnvs;
    font* fnt;

    // Mouse state
    u32 mouseX, mouseY;
    f32 mouseNormX, mouseNormY;
    f32 scrollDelta;
    bool mouseLeftDown, mouseMiddleDown, mouseRightDown;
    bool mouseLeftPressed, mouseLeftReleased;
    bool mouseMiddlePressed;

    // Previous frame mouse state
    bool prevMouseLeftDown;
    bool prevMouseMiddleDown;

    // Keyboard state
    u32 lastChar;
    u32 lastVirtualKey;
    bool keyDown;
    bool altDown, ctrlDown, shiftDown;

    // UI state
    u32 hotItem;
    u32 activeItem;
    u32 disabledDepth;

    // ID generation
    u32 nextId;

    // Input blocking - when true, mouse input has been consumed by a higher-priority element
    bool mouseInputConsumed;

    // Tab bar state
    struct TabBarState {
        u32 x, y, w, h;
        u32 numTabs;
        u32 activeTab;
        u32 currentTabIndex;
        bool inTabBar;
        u32 scrollOffset;
        bool hasOverflow;        // Whether tabs overflow the available space
        u32 desiredActiveTab;    // Tab to scroll to (for frame-delayed scrolling)
        u32 currentTabX;         // Current X position while drawing tabs
    } tabBar;

    // Menu bar state
    struct MenuBarState {
        u32 x, y, w, h;
        i32 openMenuItem;        // Index of currently open dropdown menu (-1 = none)
        u32 currentItemIndex;    // Current item being processed
        u32 currentItemX;        // Current X position for next menu item
        bool inMenuBar;
        u32 itemCount;
        u32 menuBarId;           // ID to track if menu bar owns the active item
    } menuBar;

    // Popup menu state
    struct PopupMenuState {
        u32 x, y;               // Position of the popup menu
        u32 width;              // Width of the popup (calculated based on widest item)
        u32 currentY;           // Current Y position for next menu item
        bool isOpen;
        u32 itemHeight;         // Height of each menu item
    } popupMenu;
};

static u32 GenerateId(ImGui* context) {
    return context->nextId++;
}

static bool IsMouseInRect(ImGui* context, u32 x, u32 y, u32 w, u32 h) {
    return context->mouseX >= x && context->mouseX < x + w &&
           context->mouseY >= y && context->mouseY < y + h;
}

// Helper function to calculate centered text Y position, handling cases where control is smaller than text
static u32 GetCenteredTextY(u32 controlY, u32 controlHeight, u32 textHeight) {
    if (controlHeight >= textHeight) {
        return controlY + (controlHeight - textHeight) / 2;
    } else {
        // If control is smaller than text, just align to top
        return controlY;
    }
}

ImGui* ImGuiInit(canvas* cnvs, font* fnt) {
    ImGui* context = (ImGui*)malloc(sizeof(ImGui));
    if (!context) return nullptr;

    memset(context, 0, sizeof(ImGui));

    // Store provided canvas and font (not owned by ImGui)
    context->cnvs = cnvs;
    context->fnt = fnt;

    return context;
}

void ImGuiSetTargets(ImGui* context, canvas* cnvs, font* fnt) {
    if (!context) return;
    context->cnvs = cnvs;
    context->fnt = fnt;
}

canvas* ImGuiGetCanvas(ImGui* context) {
    return context ? context->cnvs : nullptr;
}

font* ImGuiGetFont(ImGui* context) {
    return context ? context->fnt : nullptr;
}

void ImGuiBeginFrame(ImGui* context) {
    // Update mouse pressed/released states
    context->mouseLeftPressed = context->mouseLeftDown && !context->prevMouseLeftDown;
    context->mouseLeftReleased = !context->mouseLeftDown && context->prevMouseLeftDown;
    context->mouseMiddlePressed = context->mouseMiddleDown && !context->prevMouseMiddleDown;

    // Clear the canvas
    canvas_clear(context->cnvs, Colors::BACKGROUND_R, Colors::BACKGROUND_G, Colors::BACKGROUND_B);

    // Reset ID counter for the new frame
    context->nextId = 1;

    // Reset input consumption flag for the new frame
    context->mouseInputConsumed = false;

    // Reset hot item - will be set by controls if hovered
    context->hotItem = 0;

    // Note: scrollDelta is now reset in EndFrame so controls can use it
}

void ImGuiKeyboardInput(ImGui* context, u32 characterCodeUnicode, u32 virtualKeyCode,
                        bool isKeyDown, bool altDown, bool ctrlDown, bool shiftDown) {
    context->lastChar = characterCodeUnicode;
    context->lastVirtualKey = virtualKeyCode;
    context->keyDown = isKeyDown;
    context->altDown = altDown;
    context->ctrlDown = ctrlDown;
    context->shiftDown = shiftDown;
}

void ImGuiMouseInput(ImGui* context, u32 windowRelativeXPos, u32 windowRelativeYPos,
                    f32 windowNormalizedXPos, f32 windowNormalizedYPos, f32 scrollDirection,
                    bool leftDown, bool middleDown, bool rightDown) {
    context->mouseX = windowRelativeXPos;
    context->mouseY = windowRelativeYPos;
    context->mouseNormX = windowNormalizedXPos;
    context->mouseNormY = windowNormalizedYPos;

    // Accumulate scroll delta

    context->scrollDelta += scrollDirection;
    context->mouseLeftDown = leftDown;
    context->mouseMiddleDown = middleDown;
    context->mouseRightDown = rightDown;
}

void ImGuiEndFrame(ImGui* context) {
    // Update previous mouse state
    context->prevMouseLeftDown = context->mouseLeftDown;
    context->prevMouseMiddleDown = context->mouseMiddleDown;

    // Reset hot item if mouse is not pressed
    if (!context->mouseLeftDown) {
        context->activeItem = 0;
    }

    // Reset scroll delta after all controls have had a chance to use it
    context->scrollDelta = 0;
}

void ImGuiShutdown(ImGui* context) {
    if (!context) return;
    // Don't destroy canvas or font - we don't own them
    free(context);
}

void ImGuiPushDisabled(ImGui* context) {
    context->disabledDepth++;
}

void ImGuiPopDisabled(ImGui* context) {
    if (context->disabledDepth > 0) {
        context->disabledDepth--;
    }
}

bool ImGuiButton(ImGui* context, u32 x, u32 y, u32 w, u32 h, const char* text) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;
    bool isHovered = !context->mouseInputConsumed && IsMouseInRect(context, x, y, w, h) && !isDisabled;
    bool isActive = context->activeItem == id;
    bool clicked = false;

    // Handle mouse interaction
    if (isHovered && (context->activeItem == 0 || context->activeItem == id)) {
        context->hotItem = id;
        if (context->mouseLeftPressed) {
            context->activeItem = id;
        }
        if (context->mouseLeftReleased && isActive) {
            clicked = true;
        }
    }

    // Draw button background
    bool canShowHover = (context->activeItem == 0 || context->activeItem == id);
    u8 bgR, bgG, bgB;
    if (isDisabled) {
        bgR = Colors::CONTROL_R;
        bgG = Colors::CONTROL_G;
        bgB = Colors::CONTROL_B;
    } else if (isActive && isHovered) {
        bgR = Colors::CONTROL_ACTIVE_R;
        bgG = Colors::CONTROL_ACTIVE_G;
        bgB = Colors::CONTROL_ACTIVE_B;
    } else if (isHovered && canShowHover) {
        bgR = Colors::CONTROL_HOVER_R;
        bgG = Colors::CONTROL_HOVER_G;
        bgB = Colors::CONTROL_HOVER_B;
    } else {
        bgR = Colors::CONTROL_R;
        bgG = Colors::CONTROL_G;
        bgB = Colors::CONTROL_B;
    }

    canvas_draw_rect(context->cnvs, x, y, w, h, bgR, bgG, bgB);

    // Draw border
    canvas_draw_rect(context->cnvs, x, y, w, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y + h - 1, w, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y, 1, h, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x + w - 1, y, 1, h, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);

    // Draw text centered
    if (text) {
        u32 textWidth = font_get_width_cstr(context->fnt, text);
        u32 textHeight = font_get_line_height(context->fnt);
        u32 textX = x + (w - textWidth) / 2;
        u32 textY = GetCenteredTextY(y, h, textHeight);

        u8 textR = isDisabled ? Colors::TEXT_DISABLED_R : Colors::TEXT_R;
        u8 textG = isDisabled ? Colors::TEXT_DISABLED_G : Colors::TEXT_G;
        u8 textB = isDisabled ? Colors::TEXT_DISABLED_B : Colors::TEXT_B;

        canvas_draw_text_cstr(context->cnvs, context->fnt, text, textX, textY, textR, textG, textB);
    }

    return clicked;
}

bool ImGuiCheckbox(ImGui* context, u32 x, u32 y, u32 w, u32 h, const char* text, bool* checked) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;

    // Use the smaller of width or height for the checkbox box size
    u32 boxSize = (w < h) ? w : h;
    const u32 textPadding = 8;

    // Calculate total hit area (includes text if present)
    u32 textWidth = text ? font_get_width_cstr(context->fnt, text) : 0;
    u32 totalWidth = text ? (boxSize + textPadding + textWidth) : boxSize;
    u32 totalHeight = boxSize;

    bool isHovered = !context->mouseInputConsumed && IsMouseInRect(context, x, y, totalWidth, totalHeight) && !isDisabled;
    bool isActive = context->activeItem == id;
    bool clicked = false;

    // Handle mouse interaction
    if (isHovered && (context->activeItem == 0 || context->activeItem == id)) {
        context->hotItem = id;
        if (context->mouseLeftPressed) {
            context->activeItem = id;
        }
        if (context->mouseLeftReleased && isActive) {
            clicked = true;
            if (checked) *checked = !(*checked);
        }
    }

    // Draw checkbox background
    bool canShowHover = (context->activeItem == 0 || context->activeItem == id);
    u8 bgR, bgG, bgB;
    if (isDisabled) {
        bgR = Colors::CONTROL_R;
        bgG = Colors::CONTROL_G;
        bgB = Colors::CONTROL_B;
    } else if (isActive && isHovered) {
        bgR = Colors::CONTROL_ACTIVE_R;
        bgG = Colors::CONTROL_ACTIVE_G;
        bgB = Colors::CONTROL_ACTIVE_B;
    } else if (isHovered && canShowHover) {
        bgR = Colors::CONTROL_HOVER_R;
        bgG = Colors::CONTROL_HOVER_G;
        bgB = Colors::CONTROL_HOVER_B;
    } else {
        bgR = Colors::CONTROL_R;
        bgG = Colors::CONTROL_G;
        bgB = Colors::CONTROL_B;
    }

    canvas_draw_rect(context->cnvs, x, y, boxSize, boxSize, bgR, bgG, bgB);

    // Draw border
    canvas_draw_rect(context->cnvs, x, y, boxSize, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y + boxSize - 1, boxSize, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y, 1, boxSize, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x + boxSize - 1, y, 1, boxSize, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);

    // Draw checkmark if checked
    if (checked && *checked) {
        const u32 checkPadding = 4;
        canvas_draw_rect(context->cnvs, x + checkPadding, y + checkPadding,
                        boxSize - checkPadding * 2, boxSize - checkPadding * 2,
                        Colors::CHECK_R, Colors::CHECK_G, Colors::CHECK_B);
    }

    // Draw label text
    if (text) {
        u32 textHeight = font_get_line_height(context->fnt);
        u32 textY = GetCenteredTextY(y, boxSize, textHeight);
        u8 textR = isDisabled ? Colors::TEXT_DISABLED_R : Colors::TEXT_R;
        u8 textG = isDisabled ? Colors::TEXT_DISABLED_G : Colors::TEXT_G;
        u8 textB = isDisabled ? Colors::TEXT_DISABLED_B : Colors::TEXT_B;

        canvas_draw_text_cstr(context->cnvs, context->fnt, text, x + boxSize + textPadding, textY, textR, textG, textB);
    }

    return clicked;
}

f32 ImGuiHorizontalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, f32 value, f32 minValue, f32 maxValue, bool* valueChanged) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;
    bool isHovered = !context->mouseInputConsumed && IsMouseInRect(context, x, y, w, h) && !isDisabled;
    bool isActive = context->activeItem == id;

    // Initialize valueChanged to false if provided
    if (valueChanged) *valueChanged = false;

    // Handle mouse interaction
    if (isHovered && (context->activeItem == 0 || context->activeItem == id)) {
        context->hotItem = id;
        if (context->mouseLeftPressed) {
            context->activeItem = id;
        }
    }

    // Update value if active
    if (isActive && context->mouseLeftDown) {
        f32 oldValue = value;
        // Cast to signed int first to handle negative values correctly
        i32 relativeX = (i32)context->mouseX - (i32)x;
        f32 t = (f32)relativeX / (f32)w;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        value = minValue + t * (maxValue - minValue);

        // Check if value changed and report it
        if (valueChanged && value != oldValue) {
            *valueChanged = true;
        }
    }

    // Handle scroll wheel when hovering over scrollbar
    if (isHovered && context->scrollDelta != 0) {
        f32 oldValue = value;
        // Calculate scroll step (5% of range per scroll tick)
        f32 scrollStep = (maxValue - minValue) * 0.05f;
        // For horizontal scrollbar, invert the scroll direction
        // Scrolling down (negative delta) should increase value (move right)
        value -= context->scrollDelta * scrollStep;

        // Clamp to range
        if (value < minValue) value = minValue;
        if (value > maxValue) value = maxValue;

        // Check if value changed and report it
        if (valueChanged && value != oldValue) {
            *valueChanged = true;
        }
    }

    // Draw track
    canvas_draw_rect(context->cnvs, x, y, w, h, Colors::SURFACE_R, Colors::SURFACE_G, Colors::SURFACE_B);

    // Draw border
    canvas_draw_rect(context->cnvs, x, y, w, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y + h - 1, w, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y, 1, h, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x + w - 1, y, 1, h, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);

    // Draw thumb
    f32 t = (value - minValue) / (maxValue - minValue);
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    const u32 thumbWidth = 20;
    u32 thumbX = x + (u32)(t * (w - thumbWidth));

    u8 thumbR, thumbG, thumbB;
    if (isDisabled) {
        thumbR = Colors::CONTROL_R;
        thumbG = Colors::CONTROL_G;
        thumbB = Colors::CONTROL_B;
    } else if (isActive) {
        thumbR = Colors::CONTROL_ACTIVE_R;
        thumbG = Colors::CONTROL_ACTIVE_G;
        thumbB = Colors::CONTROL_ACTIVE_B;
    } else if (isHovered && (context->activeItem == 0 || context->activeItem == id)) {
        thumbR = Colors::CONTROL_HOVER_R;
        thumbG = Colors::CONTROL_HOVER_G;
        thumbB = Colors::CONTROL_HOVER_B;
    } else {
        thumbR = Colors::CONTROL_R;
        thumbG = Colors::CONTROL_G;
        thumbB = Colors::CONTROL_B;
    }

    canvas_draw_rect(context->cnvs, thumbX, y, thumbWidth, h, thumbR, thumbG, thumbB);

    return value;
}

f32 ImGuiVerticalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, f32 value, f32 minValue, f32 maxValue, bool* valueChanged) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;
    bool isHovered = !context->mouseInputConsumed && IsMouseInRect(context, x, y, w, h) && !isDisabled;
    bool isActive = context->activeItem == id;

    // Initialize valueChanged to false if provided
    if (valueChanged) *valueChanged = false;

    // Handle mouse interaction
    if (isHovered && (context->activeItem == 0 || context->activeItem == id)) {
        context->hotItem = id;
        if (context->mouseLeftPressed) {
            context->activeItem = id;
        }
    }

    // Update value if active
    if (isActive && context->mouseLeftDown) {
        f32 oldValue = value;
        // Cast to signed int first to handle negative values correctly
        i32 relativeY = (i32)context->mouseY - (i32)y;
        f32 t = (f32)relativeY / (f32)h;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        value = minValue + t * (maxValue - minValue);

        // Check if value changed and report it
        if (valueChanged && value != oldValue) {
            *valueChanged = true;
        }
    }

    // Handle scroll wheel when hovering over scrollbar
    if (isHovered && context->scrollDelta != 0) {
        f32 oldValue = value;
        // Calculate scroll step (5% of range per scroll tick)
        f32 scrollStep = (maxValue - minValue) * 0.05f;
        // For vertical scrollbar, invert the scroll direction
        // Scrolling down (negative delta) should increase value (move down)
        value -= context->scrollDelta * scrollStep;

        // Clamp to range
        if (value < minValue) value = minValue;
        if (value > maxValue) value = maxValue;

        // Check if value changed and report it
        if (valueChanged && value != oldValue) {
            *valueChanged = true;
        }
    }

    // Draw track
    canvas_draw_rect(context->cnvs, x, y, w, h, Colors::SURFACE_R, Colors::SURFACE_G, Colors::SURFACE_B);

    // Draw border
    canvas_draw_rect(context->cnvs, x, y, w, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y + h - 1, w, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y, 1, h, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x + w - 1, y, 1, h, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);

    // Draw thumb
    f32 t = (value - minValue) / (maxValue - minValue);
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    const u32 thumbHeight = 20;
    u32 thumbY = y + (u32)(t * (h - thumbHeight));

    u8 thumbR, thumbG, thumbB;
    if (isDisabled) {
        thumbR = Colors::CONTROL_R;
        thumbG = Colors::CONTROL_G;
        thumbB = Colors::CONTROL_B;
    } else if (isActive) {
        thumbR = Colors::CONTROL_ACTIVE_R;
        thumbG = Colors::CONTROL_ACTIVE_G;
        thumbB = Colors::CONTROL_ACTIVE_B;
    } else if (isHovered && (context->activeItem == 0 || context->activeItem == id)) {
        thumbR = Colors::CONTROL_HOVER_R;
        thumbG = Colors::CONTROL_HOVER_G;
        thumbB = Colors::CONTROL_HOVER_B;
    } else {
        thumbR = Colors::CONTROL_R;
        thumbG = Colors::CONTROL_G;
        thumbB = Colors::CONTROL_B;
    }

    canvas_draw_rect(context->cnvs, x, thumbY, w, thumbHeight, thumbR, thumbG, thumbB);

    return value;
}

bool ImGuiCollapsableHeader(ImGui* context, u32 x, u32 y, u32 w, u32 h, const char* text, bool* isOpen) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;
    bool isHovered = !context->mouseInputConsumed && IsMouseInRect(context, x, y, w, h) && !isDisabled;
    bool isActive = context->activeItem == id;
    bool clicked = false;

    // Handle mouse interaction
    if (isHovered && (context->activeItem == 0 || context->activeItem == id)) {
        context->hotItem = id;
        if (context->mouseLeftPressed) {
            context->activeItem = id;
        }
        if (context->mouseLeftReleased && isActive) {
            clicked = true;
            if (isOpen) *isOpen = !(*isOpen);
        }
    }

    // Draw header background
    bool canShowHover = (context->activeItem == 0 || context->activeItem == id);
    u8 bgR, bgG, bgB;
    if (isDisabled) {
        bgR = Colors::SURFACE_R;
        bgG = Colors::SURFACE_G;
        bgB = Colors::SURFACE_B;
    } else if (isActive && isHovered) {
        bgR = Colors::CONTROL_ACTIVE_R;
        bgG = Colors::CONTROL_ACTIVE_G;
        bgB = Colors::CONTROL_ACTIVE_B;
    } else if (isHovered && canShowHover) {
        bgR = Colors::CONTROL_HOVER_R;
        bgG = Colors::CONTROL_HOVER_G;
        bgB = Colors::CONTROL_HOVER_B;
    } else {
        bgR = Colors::CONTROL_R;
        bgG = Colors::CONTROL_G;
        bgB = Colors::CONTROL_B;
    }

    canvas_draw_rect(context->cnvs, x, y, w, h, bgR, bgG, bgB);

    // Draw border
    canvas_draw_rect(context->cnvs, x, y, w, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y + h - 1, w, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y, 1, h, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x + w - 1, y, 1, h, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);

    // Draw arrow indicator
    const u32 arrowSize = 8;
    const u32 arrowPadding = (h - arrowSize) / 2;
    u32 arrowX = x + arrowPadding;
    u32 arrowY = y + arrowPadding;

    if (isOpen && *isOpen) {
        // Draw down arrow (open state)
        for (u32 i = 0; i < arrowSize / 2; ++i) {
            canvas_draw_rect(context->cnvs, arrowX + i, arrowY + i, arrowSize - i * 2, 1,
                           Colors::TEXT_R, Colors::TEXT_G, Colors::TEXT_B);
        }
    } else {
        // Draw right arrow (closed state)
        for (u32 i = 0; i < arrowSize / 2; ++i) {
            canvas_draw_rect(context->cnvs, arrowX + i, arrowY + i, 1, arrowSize - i * 2,
                           Colors::TEXT_R, Colors::TEXT_G, Colors::TEXT_B);
        }
    }

    // Draw text
    if (text) {
        u32 textX = x + arrowSize + arrowPadding * 2;
        u32 textHeight = font_get_line_height(context->fnt);
        u32 textY = GetCenteredTextY(y, h, textHeight);

        u8 textR = isDisabled ? Colors::TEXT_DISABLED_R : Colors::TEXT_R;
        u8 textG = isDisabled ? Colors::TEXT_DISABLED_G : Colors::TEXT_G;
        u8 textB = isDisabled ? Colors::TEXT_DISABLED_B : Colors::TEXT_B;

        canvas_draw_text_cstr(context->cnvs, context->fnt, text, textX, textY, textR, textG, textB);
    }

    return clicked;
}

// Tab bar implementation
void ImGuiBeginTabBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, u32 numTabs, u32 activeTab) {
    // Initialize tab bar state
    context->tabBar.x = x;
    context->tabBar.y = y;
    context->tabBar.w = w;
    context->tabBar.h = h;
    context->tabBar.numTabs = numTabs;
    context->tabBar.activeTab = activeTab;
    context->tabBar.currentTabIndex = 0;
    context->tabBar.inTabBar = true;
    context->tabBar.hasOverflow = false;
    context->tabBar.currentTabX = 0;

    // Store the desired active tab for frame-delayed scrolling
    // The actual scrolling will happen when we know the actual tab positions
    context->tabBar.desiredActiveTab = activeTab;

    // Draw tab bar background
    canvas_draw_rect(context->cnvs, x, y, w, h, Colors::SURFACE_R, Colors::SURFACE_G, Colors::SURFACE_B);

    // Set initial clip rectangle for the full tab bar
    // This will be adjusted in ImGuiTab if overflow is detected
    canvas_set_clip(context->cnvs, x, y, w, h);
}

bool ImGuiTab(ImGui* context, const char* text) {
    if (!context->tabBar.inTabBar) return true;

    u32 tabIndex = context->tabBar.currentTabIndex++;
    bool isActiveTab = (tabIndex == context->tabBar.activeTab);

    // Calculate tab dimensions
    u32 textWidth = text ? font_get_width_cstr(context->fnt, text) : 40;
    const u32 padding = 10;
    const u32 closeButtonSize = 32;
    const u32 closeButtonPadding = 5;

    // Calculate actual tab width based on content
    u32 tabWidth = textWidth + padding * 2 + closeButtonSize + closeButtonPadding;
    if (tabWidth < 50) tabWidth = 50;
    if (tabWidth > 300) tabWidth = 300;

    // Calculate tab position with scroll offset
    // Note: We use signed integers here because tabX can be negative when scrolled
    i32 tabXSigned = (i32)context->tabBar.currentTabX - (i32)context->tabBar.scrollOffset;
    i32 actualTabX = (i32)context->tabBar.x + tabXSigned;
    u32 tabY = context->tabBar.y;
    u32 tabH = context->tabBar.h;

    // Check if this tab would overflow the available area
    u32 availableWidth = context->tabBar.w;
    i32 tabEndX = actualTabX + (i32)tabWidth;

    // Check for overflow conditions:
    // 1. Tab extends beyond the available width minus the more button
    // 2. Tab is partially cut off on the left side (scrolled)
    bool isClippedRight = tabEndX > (i32)(context->tabBar.x + context->tabBar.w - context->tabBar.h);
    bool isClippedLeft = actualTabX < (i32)context->tabBar.x;

    if (isClippedRight || isClippedLeft) {
        if (!context->tabBar.hasOverflow) {
            // First tab to overflow - adjust the clip area
            context->tabBar.hasOverflow = true;
            canvas_set_clip(context->cnvs, context->tabBar.x, context->tabBar.y,
                          context->tabBar.w - context->tabBar.h, context->tabBar.h);
        }
    }

    // Don't render tabs that are completely outside the visible area
    u32 visibleEndX = context->tabBar.x + context->tabBar.w;
    if (context->tabBar.hasOverflow) {
        visibleEndX -= context->tabBar.h;  // Square button width equals height
    }

    // Only skip rendering if tab is COMPLETELY outside visible area
    // (tabEndX <= context->tabBar.x means the entire tab is to the left of visible area)
    bool skipRendering = (actualTabX >= (i32)visibleEndX) || (tabEndX <= (i32)context->tabBar.x);

    if (skipRendering) {
        // Update position for next tab even if not rendered
        context->tabBar.currentTabX += tabWidth;
        return true;
    }

    // Handle scrolling to active tab (frame-delayed)
    if (isActiveTab && context->tabBar.desiredActiveTab == tabIndex) {
        u32 visibleWidth = context->tabBar.w;
        if (context->tabBar.hasOverflow) {
            visibleWidth -= context->tabBar.h;  // Account for more button
        }

        // If active tab is not fully visible, adjust scroll offset for next frame
        if (actualTabX < (i32)context->tabBar.x) {
            context->tabBar.scrollOffset -= (context->tabBar.x - actualTabX);
            if ((i32)context->tabBar.scrollOffset < 0) context->tabBar.scrollOffset = 0;
        } else if (tabEndX > (i32)(context->tabBar.x + visibleWidth)) {
            context->tabBar.scrollOffset += (tabEndX - (context->tabBar.x + visibleWidth));
        }
    }

    // Generate unique IDs for tab and close button
    u32 tabId = GenerateId(context);
    u32 closeId = GenerateId(context);

    // Keep the actual tab position for rendering (can be negative)
    i32 tabX = actualTabX;

    // For rendering, we'll cast to u32 (wraps negative values, but clip rect handles it)
    u32 drawX = (u32)tabX;

    // Check if mouse is in the "more" button area (if overflow is active)
    bool mouseInMoreButtonArea = false;
    if (context->tabBar.hasOverflow) {
        u32 moreButtonX = context->tabBar.x + context->tabBar.w - context->tabBar.h;
        mouseInMoreButtonArea = IsMouseInRect(context, moreButtonX, context->tabBar.y,
                                             context->tabBar.h, context->tabBar.h);
    }

    // For hit testing the tab itself, check if mouse is in the visible portion
    // But only if mouse input hasn't been consumed by a higher-priority element
    // AND not in the more button area
    bool isTabHovered = false;
    if (!context->mouseInputConsumed && !mouseInMoreButtonArea) {
        if (actualTabX >= 0) {
            // Tab starts in visible area
            isTabHovered = IsMouseInRect(context, (u32)actualTabX, tabY,
                                        tabWidth - closeButtonSize - closeButtonPadding, tabH) &&
                           context->disabledDepth == 0;
        } else if (actualTabX + (i32)tabWidth > 0) {
            // Tab is partially visible on the left
            isTabHovered = IsMouseInRect(context, 0, tabY,
                                        (u32)(actualTabX + (i32)tabWidth - closeButtonSize - closeButtonPadding), tabH) &&
                           context->disabledDepth == 0;
        }
    }

    // For close button hit testing, calculate actual screen position
    // Also check if input hasn't been consumed AND not in more button area
    i32 closeButtonX = actualTabX + (i32)tabWidth - (i32)closeButtonSize - (i32)closeButtonPadding;
    bool isCloseHovered = false;
    if (!context->mouseInputConsumed && !mouseInMoreButtonArea &&
        closeButtonX >= 0 && closeButtonX + (i32)closeButtonSize <= (i32)(context->tabBar.x + context->tabBar.w)) {
        isCloseHovered = IsMouseInRect(context, (u32)closeButtonX,
                                      tabY + (tabH - closeButtonSize) / 2,
                                      closeButtonSize, closeButtonSize) &&
                        context->disabledDepth == 0;
    }

    // Handle tab click
    if (isTabHovered && context->mouseLeftPressed) {
        context->tabBar.activeTab = tabIndex;
        context->activeItem = tabId;  // Tab takes active control
    }

    // Draw tab background
    u8 bgR, bgG, bgB;
    if (isActiveTab) {
        bgR = Colors::PRIMARY_R;
        bgG = Colors::PRIMARY_G;
        bgB = Colors::PRIMARY_B;
    } else if (isTabHovered) {
        bgR = Colors::CONTROL_HOVER_R;
        bgG = Colors::CONTROL_HOVER_G;
        bgB = Colors::CONTROL_HOVER_B;
    } else {
        bgR = Colors::CONTROL_R;
        bgG = Colors::CONTROL_G;
        bgB = Colors::CONTROL_B;
    }

    // Draw with actual position - use the actual tabX position for everything
    // The clipping rectangle will handle cutting off parts outside the visible area
    canvas_draw_rect(context->cnvs, drawX, tabY, tabWidth, tabH, bgR, bgG, bgB);

    // Draw tab border
    canvas_draw_rect(context->cnvs, drawX, tabY, tabWidth, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, drawX, tabY, 1, tabH, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, drawX + tabWidth - 1, tabY, 1, tabH, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);

    // Draw active tab indicator (bottom border removal)
    if (isActiveTab) {
        canvas_draw_rect(context->cnvs, drawX + 1, tabY + tabH - 1, tabWidth - 2, 1, bgR, bgG, bgB);
    } else {
        canvas_draw_rect(context->cnvs, drawX, tabY + tabH - 1, tabWidth, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    }

    // Draw tab text
    if (text) {
        u32 textHeight = font_get_line_height(context->fnt);
        u32 textY = GetCenteredTextY(tabY, tabH, textHeight);
        // Use drawX for text position so it's properly clipped along with the tab
        canvas_draw_text_cstr(context->cnvs, context->fnt, text, drawX + padding, textY,
                        Colors::TEXT_R, Colors::TEXT_G, Colors::TEXT_B);
    }

    // Draw close button
    u32 closeX = drawX + tabWidth - closeButtonSize - closeButtonPadding;
    u32 closeY = tabY + (tabH - closeButtonSize) / 2;

    // Close button background
    if (isCloseHovered) {
        canvas_draw_rect(context->cnvs, closeX, closeY, closeButtonSize, closeButtonSize,
                        Colors::CONTROL_HOVER_R, Colors::CONTROL_HOVER_G, Colors::CONTROL_HOVER_B);
    }

    // Draw X using a single 'x' character
    u8 xR = isCloseHovered ? Colors::TEXT_R : Colors::TEXT_DISABLED_R;
    u8 xG = isCloseHovered ? Colors::TEXT_G : Colors::TEXT_DISABLED_G;
    u8 xB = isCloseHovered ? Colors::TEXT_B : Colors::TEXT_DISABLED_B;

    // Calculate position to center the 'x' in the close button
    u32 textHeight = font_get_line_height(context->fnt);
    u32 xPosX = closeX + (closeButtonSize - font_get_width_cstr(context->fnt, "x")) / 2;
    u32 xPosY = closeY + (closeButtonSize - textHeight) / 2;

    // Draw the 'x' character
    canvas_draw_text_cstr(context->cnvs, context->fnt, "x", xPosX, xPosY, xR, xG, xB);

    // Handle close button click or middle-click on tab
    bool tabClosed = false;
    if (isCloseHovered && context->mouseLeftPressed) {
        tabClosed = true;
        // Consume the click so it doesn't affect other tabs
        context->activeItem = closeId;
    } else if (isTabHovered && context->mouseMiddlePressed) {
        // Middle-click anywhere on the tab closes it
        tabClosed = true;
    }

    // Update position for next tab
    context->tabBar.currentTabX += tabWidth;

    return !tabClosed; // Return false if tab was closed
}

u32 ImGuiEndTabBar(ImGui* context) {
    if (!context->tabBar.inTabBar) return 0;

    // Reset clip rectangle before drawing more button
    canvas_set_clip(context->cnvs, 0, 0, 0, 0);

    // Draw "more" button if tabs overflowed
    if (context->tabBar.hasOverflow) {
        u32 moreButtonSize = context->tabBar.h;  // Square button
        u32 moreButtonX = context->tabBar.x + context->tabBar.w - moreButtonSize;
        u32 moreButtonY = context->tabBar.y;

        // Check if more button is hovered (only if input hasn't been consumed)
        bool isMoreHovered = !context->mouseInputConsumed &&
                            IsMouseInRect(context, moreButtonX, moreButtonY,
                                          moreButtonSize, moreButtonSize);

        // Draw button background
        u8 bgR = isMoreHovered ? Colors::CONTROL_HOVER_R : Colors::CONTROL_R;
        u8 bgG = isMoreHovered ? Colors::CONTROL_HOVER_G : Colors::CONTROL_G;
        u8 bgB = isMoreHovered ? Colors::CONTROL_HOVER_B : Colors::CONTROL_B;

        canvas_draw_rect(context->cnvs, moreButtonX, moreButtonY,
                        moreButtonSize, moreButtonSize,
                        bgR, bgG, bgB);

        // Draw left border
        canvas_draw_rect(context->cnvs, moreButtonX, moreButtonY, 1, moreButtonSize,
                        Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);

        // Draw three dots centered in the square button
        u32 dotSize = 3;
        u32 dotSpacing = 4;
        u32 totalDotsWidth = dotSize * 3 + dotSpacing * 2;
        u32 dotX = moreButtonX + (moreButtonSize - totalDotsWidth) / 2;
        u32 dotY = moreButtonY + (moreButtonSize - dotSize) / 2;

        for (u32 i = 0; i < 3; i++) {
            canvas_draw_rect(context->cnvs, dotX + i * (dotSize + dotSpacing), dotY, dotSize, dotSize,
                            Colors::TEXT_R, Colors::TEXT_G, Colors::TEXT_B);
        }

        // Handle more button click (could open a dropdown menu in the future)
        if (isMoreHovered && context->mouseLeftPressed) {
            // TODO: Implement dropdown menu for tab selection
        }
    }

    // Handle scroll wheel to change active tab
    if (IsMouseInRect(context, context->tabBar.x, context->tabBar.y,
                      context->tabBar.w, context->tabBar.h)) {
        if (context->scrollDelta > 0) {
            // Scrolling up - decrease tab index (move left)
            if (context->tabBar.activeTab > 0) {
                context->tabBar.activeTab--;
            }
        } else if (context->scrollDelta < 0) {
            // Scrolling down - increase tab index (move right)
            if (context->tabBar.activeTab < context->tabBar.numTabs - 1) {
                context->tabBar.activeTab++;
            }
        }
    }

    // Reset tab bar state
    context->tabBar.inTabBar = false;

    return context->tabBar.activeTab;
}

// Menu bar implementation
void ImGuiBeginMenuBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, i32 activeItem) {
    // Initialize menu bar state
    context->menuBar.x = x;
    context->menuBar.y = y;
    context->menuBar.w = w;
    context->menuBar.h = h;
    context->menuBar.openMenuItem = activeItem;    // Start with provided active item
    context->menuBar.currentItemIndex = 0;
    context->menuBar.currentItemX = x;
    context->menuBar.inMenuBar = true;
    context->menuBar.itemCount = 0;
    context->menuBar.menuBarId = GenerateId(context);  // Generate ID for the menu bar

    // If mouse is in menu bar area, mark input as consumed
    if (IsMouseInRect(context, x, y, w, h)) {
        context->mouseInputConsumed = true;
    }

    // Draw menu bar background
    canvas_draw_rect(context->cnvs, x, y, w, h,
                    Colors::CONTROL_R, Colors::CONTROL_G, Colors::CONTROL_B);

    // Draw bottom border
    canvas_draw_rect(context->cnvs, x, y + h - 1, w, 1,
                    Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
}

void ImGuiMenuBarItem(ImGui* context, const char* itemName) {
    if (!context->menuBar.inMenuBar) return;

    u32 itemIndex = context->menuBar.currentItemIndex++;
    u32 id = GenerateId(context);

    // Calculate item dimensions
    u32 textWidth = itemName ? font_get_width_cstr(context->fnt, itemName) : 40;
    const u32 padding = 12;
    u32 itemWidth = textWidth + padding * 2;

    u32 itemX = context->menuBar.currentItemX;
    u32 itemY = context->menuBar.y;
    u32 itemH = context->menuBar.h;

    // Store item info for later use
    context->menuBar.itemCount++;

    // Check hover state
    bool isHovered = IsMouseInRect(context, itemX, itemY, itemWidth, itemH) &&
                    context->disabledDepth == 0;
    bool isActive = ((i32)itemIndex == context->menuBar.openMenuItem);

    // If hovered, mark input as consumed so lower elements don't process it
    if (isHovered) {
        context->mouseInputConsumed = true;
    }

    // Handle click - update the open menu item
    if (isHovered && context->mouseLeftPressed) {
        // Toggle open state
        if (context->menuBar.openMenuItem == (i32)itemIndex) {
            context->menuBar.openMenuItem = -1;  // Close if already open
        } else {
            context->menuBar.openMenuItem = itemIndex;  // Open this menu
            context->activeItem = context->menuBar.menuBarId;  // Menu bar takes active control
        }
    }

    // If a menu is already open and we hover over a different item, switch to it
    if (isHovered && context->menuBar.openMenuItem >= 0 &&
        context->menuBar.openMenuItem != (i32)itemIndex) {
        context->menuBar.openMenuItem = itemIndex;
    }

    // Draw item background if hovered or active
    if (isActive || isHovered) {
        u8 bgR = isActive ? Colors::PRIMARY_R : Colors::CONTROL_HOVER_R;
        u8 bgG = isActive ? Colors::PRIMARY_G : Colors::CONTROL_HOVER_G;
        u8 bgB = isActive ? Colors::PRIMARY_B : Colors::CONTROL_HOVER_B;

        canvas_draw_rect(context->cnvs, itemX, itemY, itemWidth, itemH,
                        bgR, bgG, bgB);
    }

    // Draw item text
    if (itemName) {
        u32 textHeight = font_get_line_height(context->fnt);
        u32 textY = GetCenteredTextY(itemY, itemH, textHeight);
        canvas_draw_text_cstr(context->cnvs, context->fnt, itemName,
                            itemX + padding, textY,
                            Colors::TEXT_R, Colors::TEXT_G, Colors::TEXT_B);
    }

    // Update position for next item
    context->menuBar.currentItemX += itemWidth;
}

i32 ImGuiEndMenuBar(ImGui* context) {
    if (!context->menuBar.inMenuBar) return -1;

    // Check if another control has taken focus
    if (context->activeItem != 0 && context->activeItem != context->menuBar.menuBarId) {
        context->menuBar.openMenuItem = -1;
    }

    // Check if user clicked outside the menu bar (on nothing)
    if (context->mouseLeftPressed && context->menuBar.openMenuItem >= 0) {
        // Check if the click was outside the menu bar area
        if (!IsMouseInRect(context, context->menuBar.x, context->menuBar.y,
                          context->menuBar.w, context->menuBar.h)) {
            context->menuBar.openMenuItem = -1;
            context->activeItem = 0;  // Clear active item since nothing was clicked
        }
    }

    context->menuBar.inMenuBar = false;
    return context->menuBar.openMenuItem;
}

// New separated input/rendering API for popup menus

// Mark the entire popup menu area as consuming input
void ImGuiConsumePopupMenuInput(ImGui* context, u32 menuX, u32 menuY, u32 itemCount) {
    // Fixed item dimensions
    const u32 itemHeight = 50;
    const u32 menuWidth = 200;

    // Calculate total menu height
    u32 totalHeight = itemCount * itemHeight;

    // If mouse is anywhere in the menu area, mark input as consumed
    if (IsMouseInRect(context, menuX, menuY, menuWidth, totalHeight)) {
        context->mouseInputConsumed = true;
    }
}

// Process input for a single menu item - returns true if clicked
bool ImGuiProcessMenuItem(ImGui* context, u32 menuX, u32 menuY, u32 itemIndex) {
    // Fixed item dimensions
    const u32 itemHeight = 50;
    const u32 menuWidth = 200;

    // Calculate item position
    u32 itemY = menuY + itemIndex * itemHeight;

    // Check if mouse is over this item
    bool isHovered = IsMouseInRect(context, menuX, itemY, menuWidth, itemHeight) &&
                    context->disabledDepth == 0;

    // Check for click
    if (isHovered && context->mouseLeftReleased) {
        return true;  // Item was clicked
    }

    return false;
}

// Begin rendering a popup menu
void ImGuiRenderBeginMenu(ImGui* context, u32 x, u32 y, u32 itemCount) {
    // Initialize render state for menu
    context->popupMenu.x = x;
    context->popupMenu.y = y;
    context->popupMenu.width = 200;
    context->popupMenu.currentY = y;
    context->popupMenu.itemHeight = 50;

    // Calculate actual menu height based on item count
    u32 totalHeight = itemCount * context->popupMenu.itemHeight;

    // Draw shadow
    canvas_draw_rect(context->cnvs, x + 2, y + 2,
                    context->popupMenu.width, totalHeight,
                    Colors::SHADOW_R, Colors::SHADOW_G, Colors::SHADOW_B);

    // Draw background
    canvas_draw_rect(context->cnvs, x, y,
                    context->popupMenu.width, totalHeight,
                    Colors::SURFACE_R, Colors::SURFACE_G, Colors::SURFACE_B);
}

// Render a single menu item
void ImGuiRenderMenuItem(ImGui* context, u32 menuX, u32 menuY, u32 itemIndex, const char* text) {
    // Fixed item dimensions (must match ImGuiProcessMenuItem)
    const u32 itemHeight = 50;
    const u32 menuWidth = 200;

    // Calculate item position
    u32 itemY = menuY + itemIndex * itemHeight;

    // Check if this item is hovered
    bool isHovered = IsMouseInRect(context, menuX, itemY, menuWidth, itemHeight) &&
                    context->disabledDepth == 0;

    // Draw hover background
    if (isHovered) {
        canvas_draw_rect(context->cnvs, menuX + 1, itemY, menuWidth - 2, itemHeight,
                        Colors::PRIMARY_R, Colors::PRIMARY_G, Colors::PRIMARY_B);
    }

    // Draw item text
    if (text) {
        const u32 padding = 12;
        u32 textHeight = font_get_line_height(context->fnt);
        u32 textY = GetCenteredTextY(itemY, itemHeight, textHeight);

        canvas_draw_text_cstr(context->cnvs, context->fnt, text,
                            menuX + padding, textY,
                            Colors::TEXT_R, Colors::TEXT_G, Colors::TEXT_B);
    }

    // Track the bottom of the last item for border drawing
    context->popupMenu.currentY = itemY + itemHeight;
}

// Finish rendering the popup menu
void ImGuiRenderEndMenu(ImGui* context) {
    // Calculate total height
    u32 totalHeight = context->popupMenu.currentY - context->popupMenu.y;

    // Draw borders only - background was already drawn in ImGuiRenderMenu
    // Top border
    canvas_draw_rect(context->cnvs, context->popupMenu.x, context->popupMenu.y,
                    context->popupMenu.width, 1,
                    Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    // Bottom border
    canvas_draw_rect(context->cnvs, context->popupMenu.x, context->popupMenu.y + totalHeight - 1,
                    context->popupMenu.width, 1,
                    Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    // Left border
    canvas_draw_rect(context->cnvs, context->popupMenu.x, context->popupMenu.y,
                    1, totalHeight,
                    Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    // Right border
    canvas_draw_rect(context->cnvs, context->popupMenu.x + context->popupMenu.width - 1,
                    context->popupMenu.y, 1, totalHeight,
                    Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
}