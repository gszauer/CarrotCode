#include "imgui.h"
#include "renderer.h"
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
};

static u32 GenerateId(ImGui* context) {
    return context->nextId++;
}

static bool IsMouseInRect(ImGui* context, u32 x, u32 y, u32 w, u32 h) {
    return context->mouseX >= x && context->mouseX < x + w &&
           context->mouseY >= y && context->mouseY < y + h;
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

    // Reset ID counter
    context->nextId = 1;

    // Reset scroll delta after frame
    context->scrollDelta = 0;
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
    if (isHovered) {
        context->hotItem = id;
        if (context->mouseLeftPressed) {
            context->activeItem = id;
        }
        if (context->mouseLeftReleased && isActive) {
            clicked = true;
        }
    }

    // Draw button background
    u8 bgR, bgG, bgB;
    if (isDisabled) {
        bgR = Colors::CONTROL_R;
        bgG = Colors::CONTROL_G;
        bgB = Colors::CONTROL_B;
    } else if (isActive && isHovered) {
        bgR = Colors::PRIMARY_ACTIVE_R;
        bgG = Colors::PRIMARY_ACTIVE_G;
        bgB = Colors::PRIMARY_ACTIVE_B;
    } else if (isHovered) {
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
        u32 textY = y + (h - textHeight) / 2;

        u8 textR = isDisabled ? Colors::TEXT_DISABLED_R : Colors::TEXT_R;
        u8 textG = isDisabled ? Colors::TEXT_DISABLED_G : Colors::TEXT_G;
        u8 textB = isDisabled ? Colors::TEXT_DISABLED_B : Colors::TEXT_B;

        canvas_draw_text(context->cnvs, context->fnt, text, textX, textY, textR, textG, textB);
    }

    return clicked;
}

bool ImGuiCheckbox(ImGui* context, u32 x, u32 y, u32_string* text, bool* checked) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;

    const u32 boxSize = 20;
    const u32 textPadding = 8;

    // Calculate total hit area
    u32 textWidth = text ? font_get_width(context->fnt, text, 0) : 0;
    u32 totalWidth = boxSize + textPadding + textWidth;
    u32 totalHeight = boxSize;

    bool isHovered = IsMouseInRect(context, x, y, totalWidth, totalHeight) && !isDisabled;
    bool isActive = context->activeItem == id;
    bool clicked = false;

    // Handle mouse interaction
    if (isHovered) {
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
    u8 bgR, bgG, bgB;
    if (isDisabled) {
        bgR = Colors::CONTROL_R;
        bgG = Colors::CONTROL_G;
        bgB = Colors::CONTROL_B;
    } else if (isActive && isHovered) {
        bgR = Colors::CONTROL_ACTIVE_R;
        bgG = Colors::CONTROL_ACTIVE_G;
        bgB = Colors::CONTROL_ACTIVE_B;
    } else if (isHovered) {
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
        u32 textY = y + (boxSize - font_get_line_height(context->fnt)) / 2;
        u8 textR = isDisabled ? Colors::TEXT_DISABLED_R : Colors::TEXT_R;
        u8 textG = isDisabled ? Colors::TEXT_DISABLED_G : Colors::TEXT_G;
        u8 textB = isDisabled ? Colors::TEXT_DISABLED_B : Colors::TEXT_B;

        canvas_draw_text(context->cnvs, context->fnt, text, x + boxSize + textPadding, textY, textR, textG, textB);
    }

    return clicked;
}

f32 ImGuiHorizontalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, f32 value, f32 minValue, f32 maxValue) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;
    bool isHovered = IsMouseInRect(context, x, y, w, h) && !isDisabled;
    bool isActive = context->activeItem == id;

    // Handle mouse interaction
    if (isHovered) {
        context->hotItem = id;
        if (context->mouseLeftPressed) {
            context->activeItem = id;
        }
    }

    // Update value if active
    if (isActive && context->mouseLeftDown) {
        f32 t = (f32)(context->mouseX - x) / (f32)w;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        value = minValue + t * (maxValue - minValue);
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
    } else if (isHovered) {
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

f32 ImGuiVerticalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, f32 value, f32 minValue, f32 maxValue) {
    u32 id = GenerateId(context);
    bool isDisabled = context->disabledDepth > 0;
    bool isHovered = IsMouseInRect(context, x, y, w, h) && !isDisabled;
    bool isActive = context->activeItem == id;

    // Handle mouse interaction
    if (isHovered) {
        context->hotItem = id;
        if (context->mouseLeftPressed) {
            context->activeItem = id;
        }
    }

    // Update value if active
    if (isActive && context->mouseLeftDown) {
        f32 t = (f32)(context->mouseY - y) / (f32)h;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        value = minValue + t * (maxValue - minValue);
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
    } else if (isHovered) {
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
    if (isHovered) {
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
    u8 bgR, bgG, bgB;
    if (isDisabled) {
        bgR = Colors::SURFACE_R;
        bgG = Colors::SURFACE_G;
        bgB = Colors::SURFACE_B;
    } else if (isActive && isHovered) {
        bgR = Colors::CONTROL_ACTIVE_R;
        bgG = Colors::CONTROL_ACTIVE_G;
        bgB = Colors::CONTROL_ACTIVE_B;
    } else if (isHovered) {
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
        u32 textY = y + (h - font_get_line_height(context->fnt)) / 2;

        u8 textR = isDisabled ? Colors::TEXT_DISABLED_R : Colors::TEXT_R;
        u8 textG = isDisabled ? Colors::TEXT_DISABLED_G : Colors::TEXT_G;
        u8 textB = isDisabled ? Colors::TEXT_DISABLED_B : Colors::TEXT_B;

        canvas_draw_text(context->cnvs, context->fnt, text, textX, textY, textR, textG, textB);
    }

    return clicked;
}