 
#include "application.h"

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
    user->header_open = true;
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
        u32 drop_text[] = {'D', 'r', 'o', 'p', ' ', 'f', 'i', 'l', 'e', ' ',
                          'h', 'e', 'r', 'e', ' ', 't', 'o', ' ',
                          'p', 'r', 'e', 'v', 'i', 'e', 'w', 0};
        u32_string* drop_str = u32str_init(drop_text);

        // Center the text
        u32 text_width = font_get_width(user->fnt, drop_str, 0);
        u32 text_x = (canvasWidth - text_width) / 2;
        u32 text_y = rect_y + (rect_height - font_get_line_height(user->fnt)) / 2;

        canvas_draw_text(user->cnvs, user->fnt, drop_str, text_x, text_y, 200, 200, 220);
        u32str_destroy(drop_str);

        // Add ImGui quit button underneath
        u32 quit_text[] = {'Q', 'u', 'i', 't', 0};
        u32_string* quit_str = u32str_init(quit_text);

        u32 button_width = 100;
        u32 button_height = 40;
        u32 button_x = (canvasWidth - button_width) / 2;
        u32 button_y = rect_y + rect_height + 20; // 20 pixels below the text rectangle

        if (ImGuiButton(user->imgui_context, button_x, button_y, button_width, button_height, quit_str)) {
            //windowData.closeWindow = true;
        }

        u32str_destroy(quit_str);

        // Showcase all other controls below the quit button
        u32 current_y = button_y + button_height + 20;

        // Collapsable header
        u32 header_text[] = {'A', 'd', 'v', 'a', 'n', 'c', 'e', 'd', ' ', 'O', 'p', 't', 'i', 'o', 'n', 's', 0};
        u32_string* header_str = u32str_init(header_text);
        ImGuiCollapsableHeader(user->imgui_context, button_x - 50, current_y,
                             300, 35, header_str, &user->header_open);
        u32str_destroy(header_str);

        if (user->header_open) {
            current_y += 40;

            // Tab bar demo
            u32 num_open_tabs = 0;
            for (int i = 0; i < 5; i++) {
                if (user->tab_states[i]) num_open_tabs++;
            }

            ImGuiBeginTabBar(user->imgui_context, button_x - 50, current_y, 400, 30, num_open_tabs, user->active_tab);

            u32 tab_index = 0;
            for (int i = 0; i < 5; i++) {
                if (user->tab_states[i]) {
                    // Create tab text
                    u32 tab_text[20];
                    int len = 0;
                    tab_text[len++] = 'T';
                    tab_text[len++] = 'a';
                    tab_text[len++] = 'b';
                    tab_text[len++] = ' ';
                    tab_text[len++] = '0' + i + 1;
                    tab_text[len++] = 0;

                    u32_string* tab_str = u32str_init(tab_text);
                    bool is_open = ImGuiTab(user->imgui_context, tab_str);
                    u32str_destroy(tab_str);

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
            current_y += 80;

            // Show content based on active tab
            u32 content_text[100];
            int len = 0;
            content_text[len++] = 'C';
            content_text[len++] = 'o';
            content_text[len++] = 'n';
            content_text[len++] = 't';
            content_text[len++] = 'e';
            content_text[len++] = 'n';
            content_text[len++] = 't';
            content_text[len++] = ' ';
            content_text[len++] = 'f';
            content_text[len++] = 'o';
            content_text[len++] = 'r';
            content_text[len++] = ' ';

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

            content_text[len++] = 'T';
            content_text[len++] = 'a';
            content_text[len++] = 'b';
            content_text[len++] = ' ';
            content_text[len++] = '0' + actual_tab_num;
            content_text[len++] = 0;

            u32_string* content_str = u32str_init(content_text);
            canvas_draw_text(user->cnvs, user->fnt, content_str, button_x, current_y, 200, 200, 220);
            u32str_destroy(content_str);
            current_y += 40;

            // Checkbox
            u32 checkbox_text[] = {'E', 'n', 'a', 'b', 'l', 'e', ' ', 'D', 'e', 'm', 'o', ' ', 'M', 'o', 'd', 'e', 0};
            u32_string* checkbox_str = u32str_init(checkbox_text);
            ImGuiCheckbox(user->imgui_context, button_x, current_y, 40, 40, checkbox_str, &user->checkbox_state);
            u32str_destroy(checkbox_str);
            current_y += 60;

            // Add another button inside the collapsable section
            u32 inner_button_text[] = {'N', 'e', 's', 't', 'e', 'd', ' ', 'B', 'u', 't', 't', 'o', 'n', 0};
            u32_string* inner_button_str = u32str_init(inner_button_text);
            if (ImGuiButton(user->imgui_context, button_x, current_y, 250, 40, inner_button_str)) {
                // Just for demo - toggle the checkbox when this button is clicked
                user->checkbox_state = !user->checkbox_state;
            }
            u32str_destroy(inner_button_str);


            current_y += 60;
            // Horizontal scrollbar
            u32 h_scroll_label[] = {'H', 'o', 'r', 'i', 'z', 'o', 'n', 't', 'a', 'l', ':', ' ', 0};
            u32_string* h_label_str = u32str_init(h_scroll_label);
            canvas_draw_text(user->cnvs, user->fnt, h_label_str, button_x, current_y + 5, 180, 180, 200);
            u32str_destroy(h_label_str);
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