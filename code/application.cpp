#include "application.h"
#include "strings.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

UserData* Initialize(u32 desiredWidth, u32 desiredHeight) {
    UserData* user = new UserData();
    user->offset = 0.0f;
    user->cnvs = canvas_create(desiredWidth, desiredHeight);
    user->fnt = font_create(nullptr, 0, 32);
    user->imgui_context = ImGuiInit(user->cnvs, user->fnt);

    // Initialize document views
    user->view_count = 0;
    user->active_view = 0;
    for (int i = 0; i < MAX_DOCUMENT_VIEWS; i++) {
        user->views[i] = nullptr;
    }

    return user;
}

void Update(UserData* userData, float deltaTime) {
    // Update active document view
    if (userData->view_count > 0 && userData->active_view < userData->view_count) {
        document_view* view = userData->views[userData->active_view];
        if (view) {
            document_view_update(view, deltaTime);
        }
    }
}

void AddDocumentView(UserData* user, document* doc, const char* path) {
    if (user->view_count >= MAX_DOCUMENT_VIEWS) return;

    // Check if a document with this path is already open
    if (path) {
        for (u32 i = 0; i < user->view_count; i++) {
            if (user->views[i] && user->views[i]->path) {
                // Convert the stored path to char* for comparison
                u32 stored_path_len = u32str_length(user->views[i]->path);
                char* stored_path = (char*)malloc(stored_path_len + 1);
                for (u32 j = 0; j < stored_path_len; j++) {
                    stored_path[j] = (char)u32str_get(user->views[i]->path, j);
                }
                stored_path[stored_path_len] = '\0';

                // Compare paths
                if (strcmp(stored_path, path) == 0) {
                    // Document already open, just switch to it
                    // printf("File already open in tab %u, switching to it: %s (was on tab %u)\n", i, path, user->active_view);
                    free(stored_path);
                    user->active_view = i;


                    // Clean up the document we were going to add since we don't need it
                    if (doc) {
                        doc_destroy(doc);
                    }
                    return;
                }
                free(stored_path);
            }
        }
    }

    // Convert path to u32_string if provided
    u32_string* path_str = nullptr;
    if (path) {
        u32* path_u32 = (u32*)malloc((strlen(path) + 1) * sizeof(u32));
        for (size_t i = 0; i <= strlen(path); i++) {
            path_u32[i] = (u32)path[i];
        }
        path_str = u32str_init(path_u32);
        free(path_u32);
    }

    // Create new document view
    document_view* view = document_view_create(doc, user->fnt, path_str);
    if (path_str) {
        u32str_destroy(path_str);
    }

    // Set display area (will be updated in render)
    view->displayAreaX = 0;
    view->displayAreaY = 100; // Below menu and tabs
    view->displayAreaW = canvas_get_width(user->cnvs);
    view->displayAreaH = canvas_get_height(user->cnvs) - 100;

    user->views[user->view_count] = view;
    user->active_view = user->view_count;
    user->view_count++;

}

canvas* Render(UserData* user) {
    ImGuiBeginFrame(user->imgui_context);

    canvas_clear(user->cnvs, 40, 40, 50);

    u32 canvasWidth = canvas_get_width(user->cnvs);
    u32 canvasHeight = canvas_get_height(user->cnvs);

    static i32 menuIndex = -1;
    i32 clickedItem = -1;
    i32 clickedTabItem = -1;

    // Process tab menu if open
    u32 tabMenuW = 400;
    u32 tabMenuX = canvasWidth - tabMenuW;
    if (menuIndex == 4 && user->view_count > 0) {
        u32 tabMenuY = 50;

        // printf("Tab menu is open at %u,%u with %u items\n", tabMenuX, tabMenuY, user->view_count);
        ImGuiConsumePopupMenuInput(user->imgui_context, tabMenuX, tabMenuY, user->view_count, tabMenuW);

        for (u32 i = 0; i < user->view_count; i++) {
            bool clicked = ImGuiProcessMenuItem(user->imgui_context, tabMenuX, tabMenuY, i);
            if (clicked) {
                // printf("Tab menu: selected tab %u (was %u)\n", i, user->active_view);
                user->active_view = i;
                menuIndex = -1;
                break;
            }
        }
    }

    // Process main menu
    u32 menuY = 50;
    u32 menuX = 0;
    if (menuIndex == 0) {
        menuX = 0;
        ImGuiConsumePopupMenuInput(user->imgui_context, 0, menuY, 5, 220);

        if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 0)) clickedItem = 0;
        if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 1)) clickedItem = 1;
        if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 2)) clickedItem = 2;
        if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 3)) {
            // Close current tab
            if (user->view_count > 0 && user->active_view < user->view_count) {
                // Destroy the document and its view
                if (user->views[user->active_view]->target) {
                    doc_destroy(user->views[user->active_view]->target);
                }
                document_view_destroy(user->views[user->active_view]);

                // Shift remaining views
                for (u32 i = user->active_view; i < user->view_count - 1; i++) {
                    user->views[i] = user->views[i + 1];
                }
                user->view_count--;

                // Adjust active tab after closing
                if (user->view_count > 0) {
                    if (user->active_view >= user->view_count) {
                        // We closed the last tab, select the new last tab
                        user->active_view = user->view_count - 1;
                    }
                    // Otherwise keep the same index (which now points to the next tab)
                } else {
                    // No tabs left
                    user->active_view = 0;
                }
            }
            menuIndex = -1;
        }
        if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 4)) clickedItem = 4;

        if (clickedItem >= 0) {
            menuIndex = -1;
        }
    }
    else if (menuIndex == 1) {
        menuX = 90;
        ImGuiConsumePopupMenuInput(user->imgui_context, menuX, menuY, 6, 220);

        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 0)) clickedItem = 0;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 1)) clickedItem = 1;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 2)) clickedItem = 2;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 3)) clickedItem = 3;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 4)) clickedItem = 4;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 5)) clickedItem = 5;

        if (clickedItem >= 0) {
            menuIndex = -1;
        }
    }
    else if (menuIndex == 2) {
        menuX = 175;
        ImGuiConsumePopupMenuInput(user->imgui_context, menuX, menuY, 7, 220);

        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 0)) clickedItem = 0;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 1)) clickedItem = 1;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 2)) clickedItem = 2;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 3)) clickedItem = 3;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 4)) clickedItem = 4;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 5)) clickedItem = 5;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 6)) clickedItem = 6;

        if (clickedItem >= 0) {
            menuIndex = -1;
        }
    }
    else if (menuIndex == 3) {
        menuX = 265;
        ImGuiConsumePopupMenuInput(user->imgui_context, menuX, menuY, 3, 220);

        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 0)) clickedItem = 0;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 1)) clickedItem = 1;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 2)) clickedItem = 2;

        if (clickedItem >= 0) {
            menuIndex = -1;
        }
    }

    // Render menu bar
    ImGuiBeginMenuBar(user->imgui_context, 0, 0, 360, 50, menuIndex);
    ImGuiMenuBarItem(user->imgui_context, "FILE");
    ImGuiMenuBarItem(user->imgui_context, "EDIT");
    ImGuiMenuBarItem(user->imgui_context, "VIEW");
    ImGuiMenuBarItem(user->imgui_context, "HELP");
    menuIndex = ImGuiEndMenuBar(user->imgui_context);

    // Render tab bar if we have documents
    if (user->view_count > 0 && canvasWidth > 360) {
        ImGuiBeginTabBar(user->imgui_context, 360, 0, canvasWidth - 360, 50, user->view_count, user->active_view);

        for (u32 i = 0; i < user->view_count; i++) {
            char tab_text[256];
            document_view* view = user->views[i];

            bool is_open = true;

            if (view && view->path && u32str_length(view->path) > 0) {
                // Convert path to char* for display
                u32 path_len = u32str_length(view->path);
                for (u32 j = 0; j < path_len && j < 255; j++) {
                    tab_text[j] = (char)u32str_get(view->path, j);
                }
                tab_text[path_len < 255 ? path_len : 255] = '\0';

                // Get just filename
                char* filename = strrchr(tab_text, '/');
                if (filename) {
                    filename++;
                } else {
                    filename = tab_text;
                }

                is_open = ImGuiTab(user->imgui_context, filename);
            } else {
                snprintf(tab_text, sizeof(tab_text), "Untitled %d", i + 1);
                is_open = ImGuiTab(user->imgui_context, tab_text);
            }

            // Handle tab close
            if (!is_open) {
                // Destroy the document and its view
                if (user->views[i]->target) {
                    doc_destroy(user->views[i]->target);
                }
                document_view_destroy(user->views[i]);

                // Shift remaining views
                for (u32 j = i; j < user->view_count - 1; j++) {
                    user->views[j] = user->views[j + 1];
                }
                user->view_count--;

                // Adjust active tab
                if (user->view_count > 0) {
                    if (i == user->active_view) {
                        // We're closing the active tab
                        if (i < user->view_count) {
                            // Select the tab that's now at this position (was next)
                            user->active_view = i;
                        } else {
                            // We closed the last tab, select the new last tab
                            user->active_view = user->view_count - 1;
                        }
                    } else if (i < user->active_view) {
                        // We closed a tab before the active one, shift active index
                        user->active_view--;
                    }
                    // If we closed a tab after the active one, no adjustment needed
                } else {
                    // No tabs left
                    user->active_view = 0;
                }

                // Need to break here because we modified the array we're iterating over
                // The tab bar will be redrawn next frame with the correct tabs
                break;
            }
        }

        u32 selected_tab = ImGuiEndTabBar(user->imgui_context);

        // Only update active_view if ImGuiEndTabBar returns a valid tab index
        // It might return -1 or an invalid index after closing tabs
        if (selected_tab < user->view_count) {
            user->active_view = selected_tab;
        } else if (user->view_count > 0 && user->active_view >= user->view_count) {
            // Ensure we always have a valid active tab if tabs exist
            user->active_view = user->view_count - 1;
        }

        if (ImGuiTabBarMoreButtonClicked(user->imgui_context)) {
            if (menuIndex == 4) {
                menuIndex = -1;
            } else {
                menuIndex = 4;
            }
        }
    }

    // Render document view or drop zone
    if (user->view_count > 0 && user->active_view < user->view_count) {
        document_view* view = user->views[user->active_view];
        if (view) {
            // Update display area
            view->displayAreaX = 0;
            view->displayAreaY = 60;
            view->displayAreaW = canvasWidth;
            view->displayAreaH = canvasHeight - 60;

            // Render the document view
            document_view_render(view, user->imgui_context, user->cnvs, user->fnt, true);
        }
    } else {
        // Show drop zone
        u32 rect_width = 400;
        u32 rect_height = 60;
        u32 rect_x = (canvasWidth - rect_width) / 2;
        u32 rect_y = (canvasHeight - rect_height) / 2;
        canvas_draw_rect(user->cnvs, rect_x, rect_y, rect_width, rect_height, 60, 60, 80);

        const char* drop_text = "Drop file here to open";
        u32 text_width = font_get_width_cstr(user->fnt, drop_text);
        u32 text_x = (canvasWidth - text_width) / 2;
        u32 text_y = rect_y + (rect_height - font_get_line_height(user->fnt)) / 2;

        canvas_draw_text_cstr(user->cnvs, user->fnt, drop_text, text_x, text_y, 200, 200, 220);
    }

    // Render popup menus on top
    if (menuIndex == 0) {
        ImGuiRenderBeginMenu(user->imgui_context, 0, menuY, 5);
        ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 0, "New");
        ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 1, "Open");
        ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 2, "Save");
        ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 3, "Close");
        ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 4, "Exit");
        ImGuiRenderEndMenu(user->imgui_context);
    }
    else if (menuIndex == 1) {
        ImGuiRenderBeginMenu(user->imgui_context, 90, menuY, 6);
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 0, "Undo");
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 1, "Redo");
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 2, "Cut");
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 3, "Copy");
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 4, "Paste");
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 5, "Export");
        ImGuiRenderEndMenu(user->imgui_context);
    }
    else if (menuIndex == 2) {
        ImGuiRenderBeginMenu(user->imgui_context, 175, menuY, 7);
        ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 0, "Zoom: 50%");
        ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 1, "Zoom: 75%");
        ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 2, "> Zoom: 100%");
        ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 3, "Zoom: 125%");
        ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 4, "Zoom: 150%");
        ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 5, "Syntax: On");
        ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 6, "> Syntax: Off");
        ImGuiRenderEndMenu(user->imgui_context);
    }
    else if (menuIndex == 3) {
        ImGuiRenderBeginMenu(user->imgui_context, 265, menuY, 3);
        ImGuiRenderMenuItem(user->imgui_context, 265, menuY, 0, "About");
        ImGuiRenderMenuItem(user->imgui_context, 265, menuY, 1, "Tutorial");
        ImGuiRenderMenuItem(user->imgui_context, 265, menuY, 2, "Github");
        ImGuiRenderEndMenu(user->imgui_context);
    }
    else if (menuIndex == 4 && user->view_count > 0) {
        u32 tabMenuY = 50;

        ImGuiRenderBeginMenu(user->imgui_context, tabMenuX, tabMenuY, user->view_count);

        for (u32 i = 0; i < user->view_count; i++) {
            char tab_text[256];
            document_view* view = user->views[i];

            if (view && view->path && u32str_length(view->path) > 0) {
                u32 path_len = u32str_length(view->path);
                for (u32 j = 0; j < path_len && j < 255; j++) {
                    tab_text[j] = (char)u32str_get(view->path, j);
                }
                tab_text[path_len < 255 ? path_len : 255] = '\0';

                char* filename = strrchr(tab_text, '/');
                if (filename) {
                    filename++;
                } else {
                    filename = tab_text;
                }
                ImGuiRenderMenuItem(user->imgui_context, tabMenuX, tabMenuY, i, filename);
            } else {
                snprintf(tab_text, sizeof(tab_text), "Untitled %d", i + 1);
                ImGuiRenderMenuItem(user->imgui_context, tabMenuX, tabMenuY, i, tab_text);
            }
        }

        ImGuiRenderEndMenu(user->imgui_context);
    }

    ImGuiEndFrame(user->imgui_context);

    // Handle keyboard input for active document view
    if (user->view_count > 0 && user->active_view < user->view_count) {
        document_view* view = user->views[user->active_view];
        if (view) {
            // Forward keyboard input to the document view
            // This would be connected to actual keyboard events from the platform layer
        }
    }

    return user->cnvs;
}

void Shutdown(void* userData) {
    UserData* user = (UserData*)userData;

    // Clean up document views and their documents
    for (u32 i = 0; i < user->view_count; i++) {
        if (user->views[i]) {
            if (user->views[i]->target) {
                doc_destroy(user->views[i]->target);
            }
            document_view_destroy(user->views[i]);
        }
    }

    ImGuiShutdown(user->imgui_context);
    canvas_destroy(user->cnvs);
    font_destroy(user->fnt);
    delete user;
}