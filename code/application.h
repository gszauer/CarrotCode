#ifndef _H_APPLICATION_CARROT_
#define _H_APPLICATION_CARROT_ 

#include "renderer.h"
#include "strings.h"
#include "document.h"
#include "imgui.h"
#include "view.h"
#include <vector>

struct UserData {
    float offset;
    canvas* cnvs;
    font* fnt;
    ImGui* imgui_context;

    // Document views - dynamic allocation
    std::vector<document_view*> views;
    u32 active_view;

    // Zoom level (0=50%, 1=100%, 2=200%)
    u32 zoom_level;

    // Async operation state
    bool waiting_for_operation;

    // Deferred operations
    bool has_deferred_line_delete;
    u32 deferred_delete_view;
    u32 deferred_delete_line;
};

UserData* Initialize(u32 desiredWidth, u32 desiredHeight);
void Update(UserData* userData, float deltaTime);
canvas* Render(UserData* userData); // Returns canvas to blit
void Shutdown(void* userData);
void AddDocumentView(UserData* user, document* doc, const char* path);

#endif