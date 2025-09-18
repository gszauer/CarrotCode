#ifndef _H_IMGUI_CARROT_
#define _H_IMGUI_CARROT_ 

#include "types.h"
#include "strings.h"

struct ImGui;

// Lifecycle
ImGui* ImGuiInit(canvas* cnvs, font* fnt);
void ImGuiBeginFrame(ImGui* context);
void ImGuiKeyboardInput(ImGui* context, u32 characterCodeUnicode, u32 virtualKeyCode, bool isKeyDown, bool altDown, bool ctrlDown, bool shiftDown);
void ImGuiMouseInput(ImGui* context, u32 windowRelativeXPos, u32 windowRelativeYPos, f32 windowNormalizedXPos, f32 windowNormalizedYPos, f32 scrollDirection, bool leftDown, bool middleDown, bool rightDown);
void ImGuiEndFrame(ImGui* context);
void ImGuiShutdown(ImGui* context);

// Manage targets for imgui system
void ImGuiSetTargets(ImGui* context, canvas* cnvs, font* fnt);
canvas* ImGuiGetCanvas(ImGui* context);
font* ImGuiGetFont(ImGui* context);

// State management
void ImGuiPushDisabled(ImGui* context);
void ImGuiPopDisabled(ImGui* context);
bool ImGuiIsMouseConsumed(ImGui* context);

// Control functions
bool ImGuiButton(ImGui* context, u32 x, u32 y, u32 w, u32 h, const char* text);
bool ImGuiCheckbox(ImGui* context, u32 x, u32 y, u32 w, u32 h, const char* text, bool* checked);
f32 ImGuiHorizontalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, f32 value, f32 minValue, f32 maxValue, bool* valueChanged = nullptr);
f32 ImGuiVerticalScrollBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, f32 value, f32 minValue, f32 maxValue, bool* valueChanged = nullptr);
bool ImGuiCollapsableHeader(ImGui* context, u32 x, u32 y, u32 w, u32 h, const char* text, bool* isOpen);

// Tab bar functions
void ImGuiBeginTabBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, u32 numTabs, u32 activeTab);
bool ImGuiTab(ImGui* context, const char* text); // Returns true if the tab is open, false if it's closed
u32 ImGuiEndTabBar(ImGui* context); // Returns the index of the active tab
bool ImGuiTabBarMoreButtonClicked(ImGui* context); // Returns true if the "..." button was clicked


// This is a file menu, IE: FILE, EDIT, CUT, etc.
void ImGuiBeginMenuBar(ImGui* context, u32 x, u32 y, u32 w, u32 h, i32 activeItem);
void ImGuiMenuBarItem(ImGui* context, const char* itemName);
i32 ImGuiEndMenuBar(ImGui* context); // Returns the index of the currently open menu (or -1)

// This is a popup menu. It would appear if you click on File, or if you right click
// Separated input/rendering API for popup menus
void ImGuiConsumePopupMenuInput(ImGui* context, u32 menuX, u32 menuY, u32 itemCount, u32 menuWidth);
bool ImGuiProcessMenuItem(ImGui* context, u32 menuX, u32 menuY, u32 itemIndex);
void ImGuiRenderBeginMenu(ImGui* context, u32 x, u32 y, u32 itemCount);
void ImGuiRenderMenuItem(ImGui* context, u32 menuX, u32 menuY, u32 itemIndex, const char* text);
void ImGuiRenderEndMenu(ImGui* context);



#endif