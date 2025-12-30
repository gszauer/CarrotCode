#ifndef _H_IMGUI_CARROT_
#define _H_IMGUI_CARROT_ 

#include "types.h"
#include "strings.h"

// Immediate-mode UI layer used by the editor; callers drive it by feeding input
// events each frame and then rendering controls in the order they should appear.
struct ImGui;

// Lifecycle ---------------------------------------------------------------
ImGui* ImGuiInit(canvas* cnvs, font* fnt);
void ImGuiBeginFrame(ImGui* context);
void ImGuiKeyboardInput(ImGui* context, u32 characterCodeUnicode, u32 virtualKeyCode,
                        bool isKeyDown, bool altDown, bool ctrlDown, bool shiftDown);
void ImGuiMouseInput(ImGui* context, u32 windowRelativeXPos, u32 windowRelativeYPos,
                     f32 windowNormalizedXPos, f32 windowNormalizedYPos, f32 scrollDirection,
                     bool leftDown, bool middleDown, bool rightDown);
void ImGuiEndFrame(ImGui* context);
void ImGuiShutdown(ImGui* context);

// Targets -----------------------------------------------------------------
void ImGuiSetTargets(ImGui* context, canvas* cnvs, font* fnt);
canvas* ImGuiGetCanvas(ImGui* context);
font* ImGuiGetFont(ImGui* context);

// State management -------------------------------------------------------
void ImGuiPushDisabled(ImGui* context);
void ImGuiPopDisabled(ImGui* context);
bool ImGuiIsMouseConsumed(ImGui* context);
bool ImGuiIsDisabled(ImGui* context);
void ImGuiGetMousePosition(ImGui* context, u32* outX, u32* outY);
bool ImGuiMouseLeftPressed(ImGui* context);
bool ImGuiMouseLeftReleased(ImGui* context);
bool ImGuiMouseRightPressed(ImGui* context);
bool ImGuiMouseRightReleased(ImGui* context);

// Basic controls ---------------------------------------------------------
bool ImGuiButton(ImGui* context, u32 x, u32 y, u32 w, u32 h, const char* text);
bool ImGuiCheckbox(ImGui* context, u32 x, u32 y, u32 w, u32 h, const char* text, bool* checked);
f32 ImGuiHorizontalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h,
                             f32 value, f32 minValue, f32 maxValue, bool* valueChanged = nullptr);
f32 ImGuiVerticalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h,
                           f32 value, f32 minValue, f32 maxValue, bool* valueChanged = nullptr);
f32 ImGuiHorizontalScrollBarEx(ImGui* context, u32 x, u32 y, u32 w, u32 h,
                               f32 scrollPos, f32 viewSize, f32 contentSize, bool* valueChanged = nullptr);
f32 ImGuiVerticalScrollBarEx(ImGui* context, u32 x, u32 y, u32 w, u32 h,
                             f32 scrollPos, f32 viewSize, f32 contentSize, bool* valueChanged = nullptr);

// Tab bar ----------------------------------------------------------------
void ImGuiBeginTabBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, u32 numTabs, u32 activeTab);
bool ImGuiTab(ImGui* context, const char* text, bool saved);
u32 ImGuiEndTabBar(ImGui* context);
bool ImGuiTabBarMoreButtonClicked(ImGui* context);

// Menu bar ---------------------------------------------------------------
void ImGuiBeginMenuBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, i32 activeItem);
void ImGuiMenuBarItem(ImGui* context, const char* itemName);
i32 ImGuiEndMenuBar(ImGui* context);

// Popup menus ------------------------------------------------------------
void ImGuiConsumePopupMenuInput(ImGui* context, u32 menuX, u32 menuY, u32 itemCount, u32 menuWidth);
bool ImGuiProcessMenuItem(ImGui* context, u32 menuX, u32 menuY, u32 itemIndex);
void ImGuiRenderBeginMenu(ImGui* context, u32 x, u32 y, u32 itemCount);
void ImGuiRenderMenuItem(ImGui* context, u32 menuX, u32 menuY, u32 itemIndex, const char* text);
void ImGuiRenderEndMenu(ImGui* context);



#endif
