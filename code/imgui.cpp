#include "imgui.h"
#include "renderer.h"
#include "strings.h"
#include <stdlib.h>
#include <string.h>

// Adobe Spectrum Dark color theme
namespace Colors {
    const u8 BACKGROUND_R = 30, BACKGROUND_G = 30, BACKGROUND_B = 30;
    const u8 SURFACE_R = 50, SURFACE_G = 50, SURFACE_B = 50;
    const u8 CONTROL_R = 62, CONTROL_G = 62, CONTROL_B = 62;
    const u8 CONTROL_HOVER_R = 75, CONTROL_HOVER_G = 75, CONTROL_HOVER_B = 75;
    const u8 CONTROL_ACTIVE_R = 45, CONTROL_ACTIVE_G = 45, CONTROL_ACTIVE_B = 45;
    const u8 PRIMARY_R = 36, PRIMARY_G = 131, PRIMARY_B = 226;
    const u8 PRIMARY_HOVER_R = 50, PRIMARY_HOVER_G = 145, PRIMARY_HOVER_B = 240;
    const u8 PRIMARY_ACTIVE_R = 28, PRIMARY_ACTIVE_G = 105, PRIMARY_ACTIVE_B = 180;
    const u8 TEXT_R = 255, TEXT_G = 255, TEXT_B = 255;
    const u8 TEXT_DISABLED_R = 128, TEXT_DISABLED_G = 128, TEXT_DISABLED_B = 128;
    const u8 BORDER_R = 70, BORDER_G = 70, BORDER_B = 70;
    const u8 CHECK_R = 36, CHECK_G = 131, CHECK_B = 226;
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

    // Previous frame mouse state
    bool prevMouseLeftDown;

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

    // Tab bar state
    struct TabBarState {
        u32 x, y, w, h;
        u32 numTabs;
        u32 activeTab;
        u32 currentTabIndex;
        bool inTabBar;
        u32 scrollOffset;
    } tabBar;
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

    // Clear the canvas
    canvas_clear(context->cnvs, Colors::BACKGROUND_R, Colors::BACKGROUND_G, Colors::BACKGROUND_B);

    // Reset ID counter for the new frame
    context->nextId = 1;

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

bool ImGuiButton(ImGui* context, u32 x, u32 y, u32 w, u32 h, u32_string* text) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;
    bool isHovered = IsMouseInRect(context, x, y, w, h) && !isDisabled;
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
        bgR = Colors::PRIMARY_ACTIVE_R;
        bgG = Colors::PRIMARY_ACTIVE_G;
        bgB = Colors::PRIMARY_ACTIVE_B;
    } else if (isHovered && canShowHover) {
        bgR = Colors::PRIMARY_HOVER_R;
        bgG = Colors::PRIMARY_HOVER_G;
        bgB = Colors::PRIMARY_HOVER_B;
    } else {
        bgR = Colors::PRIMARY_R;
        bgG = Colors::PRIMARY_G;
        bgB = Colors::PRIMARY_B;
    }

    canvas_draw_rect(context->cnvs, x, y, w, h, bgR, bgG, bgB);

    // Draw border
    canvas_draw_rect(context->cnvs, x, y, w, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y + h - 1, w, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x, y, 1, h, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, x + w - 1, y, 1, h, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);

    // Draw text centered
    if (text) {
        u32 textWidth = font_get_width(context->fnt, text, 0);
        u32 textHeight = font_get_line_height(context->fnt);
        u32 textX = x + (w - textWidth) / 2;
        u32 textY = GetCenteredTextY(y, h, textHeight);

        u8 textR = isDisabled ? Colors::TEXT_DISABLED_R : Colors::TEXT_R;
        u8 textG = isDisabled ? Colors::TEXT_DISABLED_G : Colors::TEXT_G;
        u8 textB = isDisabled ? Colors::TEXT_DISABLED_B : Colors::TEXT_B;

        canvas_draw_text(context->cnvs, context->fnt, text, textX, textY, textR, textG, textB);
    }

    return clicked;
}

bool ImGuiCheckbox(ImGui* context, u32 x, u32 y, u32 w, u32 h, u32_string* text, bool* checked) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;

    // Use the smaller of width or height for the checkbox box size
    u32 boxSize = (w < h) ? w : h;
    const u32 textPadding = 8;

    // Calculate total hit area (includes text if present)
    u32 textWidth = text ? font_get_width(context->fnt, text, 0) : 0;
    u32 totalWidth = text ? (boxSize + textPadding + textWidth) : boxSize;
    u32 totalHeight = boxSize;

    bool isHovered = IsMouseInRect(context, x, y, totalWidth, totalHeight) && !isDisabled;
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

        canvas_draw_text(context->cnvs, context->fnt, text, x + boxSize + textPadding, textY, textR, textG, textB);
    }

    return clicked;
}

f32 ImGuiHorizontalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, f32 value, f32 minValue, f32 maxValue, bool* valueChanged) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;
    bool isHovered = IsMouseInRect(context, x, y, w, h) && !isDisabled;
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
        thumbR = Colors::PRIMARY_ACTIVE_R;
        thumbG = Colors::PRIMARY_ACTIVE_G;
        thumbB = Colors::PRIMARY_ACTIVE_B;
    } else if (isHovered && (context->activeItem == 0 || context->activeItem == id)) {
        thumbR = Colors::PRIMARY_HOVER_R;
        thumbG = Colors::PRIMARY_HOVER_G;
        thumbB = Colors::PRIMARY_HOVER_B;
    } else {
        thumbR = Colors::PRIMARY_R;
        thumbG = Colors::PRIMARY_G;
        thumbB = Colors::PRIMARY_B;
    }

    canvas_draw_rect(context->cnvs, thumbX, y, thumbWidth, h, thumbR, thumbG, thumbB);

    return value;
}

f32 ImGuiVerticalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, f32 value, f32 minValue, f32 maxValue, bool* valueChanged) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;
    bool isHovered = IsMouseInRect(context, x, y, w, h) && !isDisabled;
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
        thumbR = Colors::PRIMARY_ACTIVE_R;
        thumbG = Colors::PRIMARY_ACTIVE_G;
        thumbB = Colors::PRIMARY_ACTIVE_B;
    } else if (isHovered && (context->activeItem == 0 || context->activeItem == id)) {
        thumbR = Colors::PRIMARY_HOVER_R;
        thumbG = Colors::PRIMARY_HOVER_G;
        thumbB = Colors::PRIMARY_HOVER_B;
    } else {
        thumbR = Colors::PRIMARY_R;
        thumbG = Colors::PRIMARY_G;
        thumbB = Colors::PRIMARY_B;
    }

    canvas_draw_rect(context->cnvs, x, thumbY, w, thumbHeight, thumbR, thumbG, thumbB);

    return value;
}

bool ImGuiCollapsableHeader(ImGui* context, u32 x, u32 y, u32 w, u32 h, u32_string* text, bool* isOpen) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;
    bool isHovered = IsMouseInRect(context, x, y, w, h) && !isDisabled;
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

        canvas_draw_text(context->cnvs, context->fnt, text, textX, textY, textR, textG, textB);
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

    // Calculate total width needed for all tabs and determine scroll offset
    const u32 tabWidth = 150; // Fixed width per tab for simplicity
    const u32 totalTabsWidth = numTabs * tabWidth;
    const u32 moreButtonWidth = 30;

    // Calculate scroll offset to ensure active tab is visible
    if (totalTabsWidth > w) {
        u32 activeTabX = activeTab * tabWidth;
        u32 activeTabEndX = activeTabX + tabWidth;
        u32 visibleWidth = w - moreButtonWidth;

        // If active tab is beyond the visible area, adjust offset
        if (activeTabX < context->tabBar.scrollOffset) {
            context->tabBar.scrollOffset = activeTabX;
        } else if (activeTabEndX > context->tabBar.scrollOffset + visibleWidth) {
            context->tabBar.scrollOffset = activeTabEndX - visibleWidth;
        }

        // Ensure we don't scroll past the end
        u32 maxScroll = totalTabsWidth - visibleWidth;
        if (context->tabBar.scrollOffset > maxScroll) {
            context->tabBar.scrollOffset = maxScroll;
        }
    } else {
        context->tabBar.scrollOffset = 0;
    }

    // Draw tab bar background
    canvas_draw_rect(context->cnvs, x, y, w, h, Colors::SURFACE_R, Colors::SURFACE_G, Colors::SURFACE_B);

    // Draw "more" button if tabs overflow
    if (totalTabsWidth > w) {
        u32 moreButtonX = x + w - moreButtonWidth;
        u32 moreButtonY = y;
        u32 moreButtonH = h;

        // Draw button background
        canvas_draw_rect(context->cnvs, moreButtonX, moreButtonY, moreButtonWidth, moreButtonH,
                        Colors::CONTROL_R, Colors::CONTROL_G, Colors::CONTROL_B);

        // Draw left border
        canvas_draw_rect(context->cnvs, moreButtonX, moreButtonY, 1, moreButtonH,
                        Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);

        // Draw three dots
        u32 dotSize = 3;
        u32 dotSpacing = 4;
        u32 totalDotsWidth = dotSize * 3 + dotSpacing * 2;
        u32 dotX = moreButtonX + (moreButtonWidth - totalDotsWidth) / 2;
        u32 dotY = moreButtonY + (moreButtonH - dotSize) / 2;

        for (u32 i = 0; i < 3; i++) {
            canvas_draw_rect(context->cnvs, dotX + i * (dotSize + dotSpacing), dotY, dotSize, dotSize,
                            Colors::TEXT_R, Colors::TEXT_G, Colors::TEXT_B);
        }

        // Set clip rectangle for tabs (excluding more button)
        canvas_set_clip(context->cnvs, x, y, w - moreButtonWidth, h);
    } else {
        // Set clip rectangle for full tab bar
        canvas_set_clip(context->cnvs, x, y, w, h);
    }
}

bool ImGuiTab(ImGui* context, u32_string* text) {
    if (!context->tabBar.inTabBar) return true;

    u32 tabIndex = context->tabBar.currentTabIndex++;
    bool isActiveTab = (tabIndex == context->tabBar.activeTab);

    // Calculate tab dimensions
    u32 textWidth = text ? font_get_width(context->fnt, text, 0) : 40;
    const u32 padding = 10;
    const u32 closeButtonSize = 16;
    const u32 closeButtonPadding = 5;

    // Clamp tab width between min and max
    u32 tabWidth = textWidth + padding * 2 + closeButtonSize + closeButtonPadding;
    if (tabWidth < 50) tabWidth = 50;
    if (tabWidth > 300) tabWidth = 300;

    // Calculate tab position with scroll offset
    u32 tabX = context->tabBar.x + tabIndex * 150 - context->tabBar.scrollOffset;
    u32 tabY = context->tabBar.y;
    u32 tabH = context->tabBar.h;

    // Don't render tabs that are completely outside the visible area
    u32 visibleEndX = context->tabBar.x + context->tabBar.w;
    if (context->tabBar.numTabs * 150 > context->tabBar.w) {
        visibleEndX -= 30; // Account for "more" button
    }

    if (tabX >= visibleEndX || tabX + tabWidth <= context->tabBar.x) {
        return true;
    }

    // Generate unique IDs for tab and close button
    u32 tabId = GenerateId(context);
    u32 closeId = GenerateId(context);

    bool isTabHovered = IsMouseInRect(context, tabX, tabY, tabWidth - closeButtonSize - closeButtonPadding, tabH) &&
                        context->disabledDepth == 0;
    bool isCloseHovered = IsMouseInRect(context, tabX + tabWidth - closeButtonSize - closeButtonPadding,
                                        tabY + (tabH - closeButtonSize) / 2,
                                        closeButtonSize, closeButtonSize) &&
                         context->disabledDepth == 0;

    // Handle tab click
    if (isTabHovered && context->mouseLeftPressed) {
        context->tabBar.activeTab = tabIndex;
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

    canvas_draw_rect(context->cnvs, tabX, tabY, tabWidth, tabH, bgR, bgG, bgB);

    // Draw tab border
    canvas_draw_rect(context->cnvs, tabX, tabY, tabWidth, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, tabX, tabY, 1, tabH, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    canvas_draw_rect(context->cnvs, tabX + tabWidth - 1, tabY, 1, tabH, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);

    // Draw active tab indicator (bottom border removal)
    if (isActiveTab) {
        canvas_draw_rect(context->cnvs, tabX + 1, tabY + tabH - 1, tabWidth - 2, 1, bgR, bgG, bgB);
    } else {
        canvas_draw_rect(context->cnvs, tabX, tabY + tabH - 1, tabWidth, 1, Colors::BORDER_R, Colors::BORDER_G, Colors::BORDER_B);
    }

    // Draw tab text
    if (text) {
        u32 textHeight = font_get_line_height(context->fnt);
        u32 textY = GetCenteredTextY(tabY, tabH, textHeight);
        canvas_draw_text(context->cnvs, context->fnt, text, tabX + padding, textY,
                        Colors::TEXT_R, Colors::TEXT_G, Colors::TEXT_B);
    }

    // Draw close button
    u32 closeX = tabX + tabWidth - closeButtonSize - closeButtonPadding;
    u32 closeY = tabY + (tabH - closeButtonSize) / 2;

    // Close button background
    if (isCloseHovered) {
        canvas_draw_rect(context->cnvs, closeX, closeY, closeButtonSize, closeButtonSize,
                        Colors::CONTROL_HOVER_R, Colors::CONTROL_HOVER_G, Colors::CONTROL_HOVER_B);
    }

    // Draw X for close button
    const u32 xPadding = 4;
    u8 xR = isCloseHovered ? Colors::TEXT_R : Colors::TEXT_DISABLED_R;
    u8 xG = isCloseHovered ? Colors::TEXT_G : Colors::TEXT_DISABLED_G;
    u8 xB = isCloseHovered ? Colors::TEXT_B : Colors::TEXT_DISABLED_B;

    // Draw X as two diagonal lines
    for (u32 i = 0; i < 2; i++) {
        canvas_draw_rect(context->cnvs, closeX + xPadding + i, closeY + xPadding + i,
                        closeButtonSize - xPadding * 2 - i * 2, 1, xR, xG, xB);
        canvas_draw_rect(context->cnvs, closeX + xPadding + i, closeY + closeButtonSize - xPadding - 1 - i,
                        closeButtonSize - xPadding * 2 - i * 2, 1, xR, xG, xB);
    }

    // Handle close button click
    if (isCloseHovered && context->mouseLeftPressed) {
        return false; // Tab closed
    }

    return true; // Tab still open
}

u32 ImGuiEndTabBar(ImGui* context) {
    if (!context->tabBar.inTabBar) return 0;

    // Reset clip rectangle
    canvas_set_clip(context->cnvs, 0, 0, 0, 0);

    // Reset tab bar state
    context->tabBar.inTabBar = false;

    return context->tabBar.activeTab;
}