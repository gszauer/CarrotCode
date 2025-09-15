#ifndef _H_APPLICATION_CARROT_
#define _H_APPLICATION_CARROT_ 

#include "renderer.h"
#include "strings.h"
#include "document.h"
#include "imgui.h"

struct UserData {
    float offset;
    canvas* cnvs;
    font* fnt;
    document* doc;
    canvas* doc_canvas;
    bool has_document;
    ImGui* imgui_context;
    // Demo control states
    bool checkbox_state;
    f32 h_scrollbar_value;
    f32 v_scrollbar_value;
    bool header_open;
    // Tab bar demo state
    u32 active_tab;
    bool tab_states[5];  // Track which tabs are open
};

UserData* Initialize(u32 desiredWidth, u32 desiredHeight);
void Update(UserData* userData, float deltaTime);
canvas* Render(UserData* userData); // Returns canvas to blit
void Shutdown(void* userData);

#endif