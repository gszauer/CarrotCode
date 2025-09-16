
#include "application.h"
#include <cstdio>

UserData* Initialize(u32 desiredWidth, u32 desiredHeight) {
    UserData* user = new UserData();
    user->offset = 0.0f;
    user->cnvs = canvas_create(desiredWidth, desiredHeight);
    user->fnt = font_create(nullptr, 0, 32); // Using bitmap font
    user->doc = nullptr;
    user->doc_canvas = nullptr;
    user->has_document = false;
    user->imgui_context = ImGuiInit(user->cnvs, user->fnt);
    // Initialize demo control states
    user->checkbox_state = false;
    user->h_scrollbar_value = 0.5f;
    user->v_scrollbar_value = 0.3f;
    user->header_open = false;
    // Initialize tab bar state
    user->active_tab = 0;
    for (int i = 0; i < 5; i++) {
        user->tab_states[i] = true;  // All tabs start open
    }
    return user;
}

void Update(UserData* userData, float deltaTime) {
}

canvas* Render(UserData* user) {
    // Begin ImGui frame
    ImGuiBeginFrame(user->imgui_context);
    canvas* result = 0;

    if (user->has_document && user->doc_canvas) {
        // Display the document canvas
        result = user->doc_canvas;
    } else {
        // Clear canvas to dark gray
        canvas_clear(user->cnvs, 40, 40, 50);

        u32 canvasWidth = canvas_get_width(user->cnvs);
        u32 canvasHeight = canvas_get_height(user->cnvs);

        // Draw a rectangle behind the text
        u32 rect_width = 400;
        u32 rect_height = 60;
        u32 rect_x = (canvasWidth - rect_width) / 2;
        u32 rect_y = (canvasHeight - rect_height) / 2;
        canvas_draw_rect(user->cnvs, rect_x, rect_y, rect_width, rect_height, 60, 60, 80);

        // Draw "Drop file here to preview" text
        const char* drop_text = "Drop file here to preview";

        // Center the text
        u32 text_width = font_get_width_cstr(user->fnt, drop_text);
        u32 text_x = (canvasWidth - text_width) / 2;
        u32 text_y = rect_y + (rect_height - font_get_line_height(user->fnt)) / 2;

        canvas_draw_text_cstr(user->cnvs, user->fnt, drop_text, text_x, text_y, 200, 200, 220);

        // Add ImGui quit button underneath
        const char* quit_text = "Quit";

        static i32 menuIndex = -1;
        i32 clickedItem = -1;  // Track which menu item was clicked this frame

        // Process popup menu input FIRST (before any other controls)
        u32 menuY = 50;
        u32 menuX = 0;
        if (menuIndex == 0) {
            menuX = 0;
            // First, mark the entire menu area as consuming input
            ImGuiConsumePopupMenuInput(user->imgui_context, 0, menuY, 5);

            // Check each item for clicks
            if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 0)) clickedItem = 0;
            if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 1)) clickedItem = 1;
            if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 2)) clickedItem = 2;
            if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 3)) clickedItem = 3;
            if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 4)) clickedItem = 4;

            // Close menu if any item was clicked
            if (clickedItem >= 0) {
                menuIndex = -1;
                // Handle the clicked item here if needed
                // For example: if (clickedItem == 4) { /* Exit */ }
            }
        }
        else if (menuIndex == 1) {
            menuX = 90;
            // First, mark the entire menu area as consuming input
            ImGuiConsumePopupMenuInput(user->imgui_context, menuX, menuY, 6);

            // Check each item for clicks
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 0)) clickedItem = 0;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 1)) clickedItem = 1;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 2)) clickedItem = 2;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 3)) clickedItem = 3;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 4)) clickedItem = 4;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 5)) clickedItem = 5;

            // Close menu if any item was clicked
            if (clickedItem >= 0) {
                menuIndex = -1;
                // Handle the clicked item here if needed
                // For example: if (clickedItem == 4) { /* Exit */ }
            }
        }
        else if (menuIndex == 2) {
            menuX = 175;
            // First, mark the entire menu area as consuming input
            ImGuiConsumePopupMenuInput(user->imgui_context, menuX, menuY, 7);

            // Check each item for clicks
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 0)) clickedItem = 0;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 1)) clickedItem = 1;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 2)) clickedItem = 2;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 3)) clickedItem = 3;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 4)) clickedItem = 4;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 5)) clickedItem = 5;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 6)) clickedItem = 6;

            // Close menu if any item was clicked
            if (clickedItem >= 0) {
                menuIndex = -1;
                // Handle the clicked item here if needed
                // For example: if (clickedItem == 4) { /* Exit */ }
            }
        }
        else if (menuIndex == 3) {
            menuX = 265;
            // First, mark the entire menu area as consuming input
            ImGuiConsumePopupMenuInput(user->imgui_context, menuX, menuY, 3);

            // Check each item for clicks
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 0)) clickedItem = 0;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 1)) clickedItem = 1;
            if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 2)) clickedItem = 2;

            // Close menu if any item was clicked
            if (clickedItem >= 0) {
                menuIndex = -1;
                // Handle the clicked item here if needed
                // For example: if (clickedItem == 4) { /* Exit */ }
            }
        }

        // Now render menu bar and other controls
        ImGuiBeginMenuBar(user->imgui_context, 0, 0, 360, 50, menuIndex);
        ImGuiMenuBarItem(user->imgui_context, "FILE");
        ImGuiMenuBarItem(user->imgui_context, "EDIT");
        ImGuiMenuBarItem(user->imgui_context, "VIEW");
        ImGuiMenuBarItem(user->imgui_context, "HELP");
        menuIndex = ImGuiEndMenuBar(user->imgui_context);

       
        
        u32 button_height = 40;
        u32 button_x = 50;
        u32 button_y = 50;
        u32 current_y = 0;

        if (canvasWidth > 360) { // only draw bar is possible
            u32 num_open_tabs = 0;
            for (int i = 0; i < 5; i++) {
                if (user->tab_states[i]) num_open_tabs++;
            }
            ImGuiBeginTabBar(user->imgui_context, 360, current_y, canvasWidth - 360, 50, num_open_tabs, user->active_tab);

            u32 tab_index = 0;
            for (int i = 0; i < 5; i++) {
                if (user->tab_states[i]) {
                    // Create tab text
                    char tab_text[20];
                    snprintf(tab_text, sizeof(tab_text), "Tabotha hodor %d", i + 1);

                    bool is_open = ImGuiTab(user->imgui_context, tab_text);

                    if (!is_open) {
                        user->tab_states[i] = false;
                        // If we closed the active tab, select another one
                        if (tab_index == user->active_tab) {
                            // Find the next open tab
                            for (int j = 0; j < 5; j++) {
                                if (user->tab_states[j]) {
                                    user->active_tab = 0; // Will be recalculated
                                    break;
                                }
                            }
                        }
                    }
                    tab_index++;
                }
            }

            user->active_tab = ImGuiEndTabBar(user->imgui_context);
        }
        current_y += 80;

        // Collapsable header
        const char* header_text = "Tool Showcase";
        ImGuiCollapsableHeader(user->imgui_context, button_x - 50, current_y, canvasWidth, 50, header_text, &user->header_open);

        if (user->header_open) {
            current_y += 60;

            // Show content based on active tab
            // Find which actual tab number is active
            int actual_tab_num = 0;
            u32 current_tab_index = 0;
            for (int i = 0; i < 5; i++) {
                if (user->tab_states[i]) {
                    if (current_tab_index == user->active_tab) {
                        actual_tab_num = i + 1;
                        break;
                    }
                    current_tab_index++;
                }
            }

            char content_text[100];
            snprintf(content_text, sizeof(content_text), "Content for Tab %d", actual_tab_num);
            canvas_draw_text_cstr(user->cnvs, user->fnt, content_text, button_x, current_y, 200, 200, 220);
            current_y += 40;

            // Checkbox
            const char* checkbox_text = "Enable Demo Mode";
            ImGuiCheckbox(user->imgui_context, button_x, current_y, 40, 40, checkbox_text, &user->checkbox_state);
            current_y += 60;

            // Add another button inside the collapsable section
            const char* inner_button_text = "Nested Button";
            if (ImGuiButton(user->imgui_context, button_x, current_y, 250, 40, inner_button_text)) {
                // Just for demo - toggle the checkbox when this button is clicked
                user->checkbox_state = !user->checkbox_state;
            }


            current_y += 60;
            // Horizontal scrollbar
            const char* h_scroll_label = "Horizontal: ";
            canvas_draw_text_cstr(user->cnvs, user->fnt, h_scroll_label, button_x, current_y + 5, 180, 180, 200);
            user->h_scrollbar_value = ImGuiHorizontalScrollBar(user->imgui_context,
                                                            button_x + 200, current_y, 200, 30,
                                                            user->h_scrollbar_value, 0.0f, 1.0f);

            // Vertical scrollbar (positioned to the right)
            user->v_scrollbar_value = ImGuiVerticalScrollBar(user->imgui_context,
                                                        button_x + 400, current_y + 30 - 150 - 30, 30, 150,
                                                        user->v_scrollbar_value, 0.0f, 1.0f);
            current_y += 40;
        }

        // Copy canvas pixels to window buffer
        //u32* canvas_pixels = canvas_get_raw_pixels(user->cnvs);
        //memcpy(windowData.pixels, canvas_pixels, windowData.width * windowData.height * sizeof(u32));
        result = user->cnvs;

        // Render popup menu LAST (on top of everything)
        if (menuIndex == 0) {
            menuX = 0;
            ImGuiRenderBeginMenu(user->imgui_context, 0, menuY, 5);
            ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 0, "New");
            ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 1, "Open");
            ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 2, "Save");
            ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 3, "Close");
            ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 4, "Exit");
            ImGuiRenderEndMenu(user->imgui_context);
        }
        else if (menuIndex == 1) {
            menuX = 90;
            ImGuiRenderBeginMenu(user->imgui_context, menuX, menuY, 6); 
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 0, "Undo");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 1, "Redo");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 2, "Cut");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 3, "Copy");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 4, "Paste");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 5, "Export");
            ImGuiRenderEndMenu(user->imgui_context);
        }
        else if (menuIndex == 2) {
            menuX = 175;
            ImGuiRenderBeginMenu(user->imgui_context, menuX, menuY, 7); 
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 0, "Zoom: 50%");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 1, "Zoom: 75%");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 2, "> Zoom: 100%");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 3, "Zoom: 125%");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 4, "Zoom: 150%");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 5, "Syntax: On");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 6, "> Syntax: Off");
            ImGuiRenderEndMenu(user->imgui_context);
        }
        else if (menuIndex == 3) {
            menuX = 265;
            ImGuiRenderBeginMenu(user->imgui_context, menuX, menuY, 3); 
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 0, "About");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 1, "Tutorial");
            ImGuiRenderMenuItem(user->imgui_context, menuX, menuY, 2, "Github");
            ImGuiRenderEndMenu(user->imgui_context);
        }
    }

    // End ImGui frame
    ImGuiEndFrame(user->imgui_context);

    return result;
}

void Shutdown(void* userData) {
    UserData* user = (UserData*)userData;
    ImGuiShutdown(user->imgui_context);
    canvas_destroy(user->cnvs);
    if (user->doc_canvas) {
        canvas_destroy(user->doc_canvas);
    }
    if (user->doc) {
        doc_destroy(user->doc);
    }
    font_destroy(user->fnt);
    delete user;
}