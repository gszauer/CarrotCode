#ifndef _H_APPLICATION_CARROT_
#define _H_APPLICATION_CARROT_ 

#include "renderer.h"
#include "strings.h"
#include "document.h"
#include "imgui.h"
#include "view.h"

#define MAX_DOCUMENT_VIEWS 10

struct UserData {
    float offset;
    canvas* cnvs;
    font* fnt;
    ImGui* imgui_context;

    // Document views
    document_view* views[MAX_DOCUMENT_VIEWS];
    u32 view_count;
    u32 active_view;
};

UserData* Initialize(u32 desiredWidth, u32 desiredHeight);
void Update(UserData* userData, float deltaTime);
canvas* Render(UserData* userData); // Returns canvas to blit
void Shutdown(void* userData);
void AddDocumentView(UserData* user, document* doc, const char* path);

#endif