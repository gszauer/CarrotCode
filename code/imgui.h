#ifndef _H_IMGUI_CARROT_
#define _H_IMGUI_CARROT_ 

#include "types.h"
#include "strings.h"

struct ImGui;

// Lifecycle
ImGui* ImGuiInit(canvas* cnvs, font* fnt);
void ImGuiSetTargets(ImGui* context, canvas* cnvs, font* fnt);
canvas* ImGuiGetCanvas(ImGui* context);
font* ImGuiGetFont(ImGui* context);
void ImGuiBeginFrame(ImGui* context);
void ImGuiKeyboardInput(ImGui* context, u32 characterCodeUnicode, u32 virtualKeyCode, bool isKeyDown, bool altDown, bool ctrlDown, bool shiftDown);
void ImGuiMouseInput(ImGui* context, u32 windowRelativeXPos, u32 windowRelativeYPos, f32 windowNormalizedXPos, f32 windowNormalizedYPos, f32 scrollDirection, bool leftDown, bool middleDown, bool rightDown);
void ImGuiEndFrame(ImGui* context);
void ImGuiShutdown(ImGui* context);

// State management
void ImGuiPushDisabled(ImGui* context);
void ImGuiPopDisabled(ImGui* context);

// Control functions
bool ImGuiButton(ImGui* context, u32 x, u32 y, u32 w, u32 h, u32_string* text);
bool ImGuiCheckbox(ImGui* context, u32 x, u32 y, u32_string* text, bool* checked);
f32 ImGuiHorizontalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, f32 value, f32 minValue, f32 maxValue);
f32 ImGuiVerticalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, f32 value, f32 minValue, f32 maxValue);
bool ImGuiCollapsableHeader(ImGui* context, u32 x, u32 y, u32 w, u32 h, u32_string* text, bool* isOpen);

#endif