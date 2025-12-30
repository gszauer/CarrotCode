#ifndef _H_APPLICATION_CARROT_
#define _H_APPLICATION_CARROT_ 

#include "renderer.h"
#include "strings.h"
#include "document.h"
#include "imgui.h"
#include "view.h"
#include "platform.h"
#include <vector>

struct UserData {
    float offset;
    canvas* cnvs;
    font* fnt;
    ImGui* imgui_context;

    // Document views - dynamic allocation
    std::vector<document_view*> views;
    u32 active_view;

    // Menu state
    i32 menu_index;

    // Zoom level (0=50%, 1=100%, 2=200%)
    u32 zoom_level;

    // Async operation state
    bool waiting_for_operation;
    bool disabled_state_pushed;  // Track if we've pushed disabled state
    u32_string* pending_clipboard_text;

    // Context menu state
    bool show_context_menu;
    u32 context_menu_x;
    u32 context_menu_y;

    bool tile_debug_enabled;
    bool should_quit;
};

enum class ApplicationMouseEventType {
    Press,
    Release,
    Move
};

enum class ApplicationMouseButton {
    NoneButton,
    Left,
    Middle,
    Right
};

struct ApplicationMouseEvent {
    ApplicationMouseEventType type;
    ApplicationMouseButton button;
    u32 x;
    u32 y;
    f32 normX;
    f32 normY;
    f32 scrollDelta;
    bool leftDown;
    bool middleDown;
    bool rightDown;
    bool shiftDown;
};

UserData* Initialize(u32 desiredWidth, u32 desiredHeight);
void Update(UserData* userData, float deltaTime);
canvas* Render(UserData* userData); // Returns canvas to blit
void Shutdown(void* userData);
void AddDocumentView(UserData* user, document* doc, const char* path);

void ApplicationCut(UserData* user);
void ApplicationCopy(UserData* user);
void ApplicationPaste(UserData* user);

void ApplicationHandleKeyboard(UserData* user, u32 characterCode, PlatformKey key,
                               u32 nativeKey, bool isDown, bool altDown, bool ctrlDown, bool shiftDown);
void ApplicationHandleMouse(UserData* user, const ApplicationMouseEvent& evt);

#endif
