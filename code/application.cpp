#include "application.h"
#include "strings.h"
#include "platform.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <string>

// Clipboard callback functions
void clipboard_copy_callback(void* userData) {
    UserData* user = (UserData*)userData;
    user->waiting_for_operation = false;
}

void clipboard_paste_callback(u32_string* content, void* userData) {
    UserData* user = (UserData*)userData;
    user->waiting_for_operation = false;

    // If we have content and an active document view, insert it
    if (content && !user->views.empty() && user->active_view < user->views.size()) {
        document_view* view = user->views[user->active_view];
        if (view) {
            // Extract text data from u32_string
            u32 text_len = u32str_length(content);
            u32* text_data = (u32*)malloc(text_len * sizeof(u32));
            for (u32 i = 0; i < text_len; i++) {
                text_data[i] = u32str_get(content, i);
            }

            // Use document_view_paste to insert the text
            document_view_paste(view, text_data, text_len);

            free(text_data);
        }
    }
}

// Structure to hold data for the close confirmation callback
struct CloseConfirmData {
    UserData* user;
    u32 view_index;
    bool has_path;
};

// Structure to hold data for the save file as callback when closing
struct SaveAsCloseData {
    UserData* user;
    u32 view_index;
};

// Structure to hold data for the write file callback when closing
struct WriteFileCloseData {
    UserData* user;
    u32 view_index;
};

// Callback for write file operation when closing an existing document
void write_file_close_callback(bool success, void* userData) {
    WriteFileCloseData* data = (WriteFileCloseData*)userData;
    UserData* user = data->user;
    u32 view_index = data->view_index;

    // Close the document regardless of whether save succeeded
    if (view_index < user->views.size()) {
        document_view* view = user->views[view_index];
        document* doc = view->target;

        if (success && doc) {
            // Mark document as saved if write succeeded
            doc_set_modified(doc, false);
        }

        // Destroy the document and its view
        if (doc) {
            doc_destroy(doc);
        }
        document_view_destroy(view);

        // Remove from vector
        user->views.erase(user->views.begin() + view_index);

        // Adjust active tab after closing
        if (!user->views.empty()) {
            if (user->active_view >= user->views.size()) {
                // We closed the last tab, select the new last tab
                user->active_view = user->views.size() - 1;
            } else if (view_index < user->active_view) {
                // We closed a tab before the active one, adjust the index
                user->active_view--;
            }
            // Otherwise keep the same index (which now points to the next tab)
        } else {
            // No tabs left
            user->active_view = 0;
        }
    }

    delete data;
}

// Callback for save file as operation when closing a new document
void save_as_close_callback(u32_string* filePath, void* userData) {
    SaveAsCloseData* data = (SaveAsCloseData*)userData;
    UserData* user = data->user;
    u32 view_index = data->view_index;

    // Whether save succeeded or not, close the document
    // (If filePath is not null, the save succeeded, but we close either way)

    if (view_index < user->views.size()) {
        document_view* view = user->views[view_index];
        document* doc = view->target;

        // If save succeeded, update the document's path
        if (filePath) {
            // Update the view's path
            if (view->path) {
                u32str_destroy(view->path);
            }
            // Create a copy of the path for the view
            u32 path_len = u32str_length(filePath);
            u32* path_copy = (u32*)malloc((path_len + 1) * sizeof(u32));
            for (u32 i = 0; i <= path_len; i++) {
                path_copy[i] = u32str_get(filePath, i);
            }
            view->path = u32str_init(path_copy);
            free(path_copy);

            // Mark document as saved
            if (doc) {
                doc_set_modified(doc, false);
            }
        }

        // Now close the document
        if (doc) {
            doc_destroy(doc);
        }
        document_view_destroy(view);

        // Remove from vector
        user->views.erase(user->views.begin() + view_index);

        // Adjust active tab after closing
        if (!user->views.empty()) {
            if (user->active_view >= user->views.size()) {
                // We closed the last tab, select the new last tab
                user->active_view = user->views.size() - 1;
            } else if (view_index < user->active_view) {
                // We closed a tab before the active one, adjust the index
                user->active_view--;
            }
            // Otherwise keep the same index (which now points to the next tab)
        } else {
            // No tabs left
            user->active_view = 0;
        }
    }

    delete data;
}

// Callback for the save changes dialog when closing a document
void close_confirm_callback(bool yes_clicked, void* userData) {
    CloseConfirmData* data = (CloseConfirmData*)userData;
    UserData* user = data->user;
    u32 view_index = data->view_index;

    if (yes_clicked) {
        // User clicked Yes - they want to save
        if (data->has_path) {
            // Document has a backing file - save to existing path
            if (view_index < user->views.size()) {
                document_view* view = user->views[view_index];
                document* doc = view->target;

                if (doc && view->path) {
                    // Convert document content to string (includes newlines between lines)
                    u32_string* content = doc_to_str32(doc);

                    if (content) {
                        // Get the UTF-32 content length
                        u32 content_len = u32str_length(content);

                        // Convert to UTF-8 for compatibility with text editors
                        // Allocate worst-case buffer (4 bytes per character)
                        u32 buffer_size = content_len * 4 + 1;
                        unsigned char* utf8_content = (unsigned char*)malloc(buffer_size);
                        u32 utf8_len = 0;

                        // Convert UTF-32 to UTF-8, preserving all Unicode characters
                        for (u32 i = 0; i < content_len; i++) {
                            u32 ch = u32str_get(content, i);

                            if (ch <= 0x7F) {
                                // 1-byte sequence (ASCII, including newlines)
                                utf8_content[utf8_len++] = (unsigned char)ch;
                            } else if (ch <= 0x7FF) {
                                // 2-byte sequence
                                utf8_content[utf8_len++] = (unsigned char)(0xC0 | (ch >> 6));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                            } else if (ch <= 0xFFFF) {
                                // 3-byte sequence
                                utf8_content[utf8_len++] = (unsigned char)(0xE0 | (ch >> 12));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                            } else if (ch <= 0x10FFFF) {
                                // 4-byte sequence (for emojis and other extended Unicode)
                                utf8_content[utf8_len++] = (unsigned char)(0xF0 | (ch >> 18));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 12) & 0x3F));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                            }
                            // Skip invalid Unicode values above 0x10FFFF
                        }

                        // Create callback data for write file
                        WriteFileCloseData* writeData = new WriteFileCloseData();
                        writeData->user = user;
                        writeData->view_index = view_index;

                        // Call platform write file
                        platform_write_file(view->path, utf8_content, utf8_len, write_file_close_callback, writeData);

                        // Clean up
                        free(utf8_content);
                        u32str_destroy(content);
                    }
                }
            }
        } else {
            // No backing file - use Save As dialog
            if (view_index < user->views.size()) {
                document_view* view = user->views[view_index];
                document* doc = view->target;

                if (doc) {
                    // Convert document content to string (includes newlines between lines)
                    u32_string* content = doc_to_str32(doc);

                    if (content) {
                        // Get the UTF-32 content length
                        u32 content_len = u32str_length(content);

                        // Convert to UTF-8 for compatibility with text editors
                        // Allocate worst-case buffer (4 bytes per character)
                        u32 buffer_size = content_len * 4 + 1;
                        unsigned char* utf8_content = (unsigned char*)malloc(buffer_size);
                        u32 utf8_len = 0;

                        // Convert UTF-32 to UTF-8, preserving all Unicode characters
                        for (u32 i = 0; i < content_len; i++) {
                            u32 ch = u32str_get(content, i);

                            if (ch <= 0x7F) {
                                // 1-byte sequence (ASCII, including newlines)
                                utf8_content[utf8_len++] = (unsigned char)ch;
                            } else if (ch <= 0x7FF) {
                                // 2-byte sequence
                                utf8_content[utf8_len++] = (unsigned char)(0xC0 | (ch >> 6));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                            } else if (ch <= 0xFFFF) {
                                // 3-byte sequence
                                utf8_content[utf8_len++] = (unsigned char)(0xE0 | (ch >> 12));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                            } else if (ch <= 0x10FFFF) {
                                // 4-byte sequence (for emojis and other extended Unicode)
                                utf8_content[utf8_len++] = (unsigned char)(0xF0 | (ch >> 18));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 12) & 0x3F));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                                utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                            }
                            // Skip invalid Unicode values above 0x10FFFF
                        }

                        // Create callback data for save as
                        SaveAsCloseData* saveData = new SaveAsCloseData();
                        saveData->user = user;
                        saveData->view_index = view_index;

                        // Call platform save file as with UTF-8 data
                        platform_save_file_as(utf8_content, utf8_len, save_as_close_callback, saveData);

                        // Clean up
                        free(utf8_content);
                        u32str_destroy(content);
                    }
                }
            }
        }
    } else {
        // User clicked No - close without saving
        if (view_index < user->views.size()) {
            document_view* view = user->views[view_index];
            document* doc = view->target;

            // Destroy the document and its view
            if (doc) {
                doc_destroy(doc);
            }
            document_view_destroy(view);

            // Remove from vector
            user->views.erase(user->views.begin() + view_index);

            // Adjust active tab after closing
            if (!user->views.empty()) {
                if (user->active_view >= user->views.size()) {
                    // We closed the last tab, select the new last tab
                    user->active_view = user->views.size() - 1;
                } else if (view_index < user->active_view) {
                    // We closed a tab before the active one, adjust the index
                    user->active_view--;
                }
                // Otherwise keep the same index (which now points to the next tab)
            } else {
                // No tabs left
                user->active_view = 0;
            }
        }
    }

    delete data;
}

// Callback for save file operation
void save_file_callback(bool success, void* userData) {
    UserData* user = (UserData*)userData;

    if (success) {
        // Mark document as saved if we have an active view
        if (!user->views.empty() && user->active_view < user->views.size()) {
            document_view* view = user->views[user->active_view];
            if (view && view->target) {
                doc_set_modified(view->target, false);
            }
        }
    } else {
        fprintf(stderr, "Failed to save file\n");
    }
}

// Callback for save as operation
void save_as_callback(u32_string* filePath, void* userData) {
    UserData* user = (UserData*)userData;

    if (filePath) {
        // Update the document's path
        if (!user->views.empty() && user->active_view < user->views.size()) {
            document_view* view = user->views[user->active_view];
            if (view) {
                // Update the view's path
                if (view->path) {
                    u32str_destroy(view->path);
                }
                // Create a copy of the path for the view
                u32 path_len = u32str_length(filePath);
                u32* path_copy = (u32*)malloc((path_len + 1) * sizeof(u32));
                for (u32 i = 0; i <= path_len; i++) {
                    path_copy[i] = u32str_get(filePath, i);
                }
                view->path = u32str_init(path_copy);
                free(path_copy);

                // Mark document as saved
                if (view->target) {
                    doc_set_modified(view->target, false);
                }
            }
        }
    }
}

// File open callback function
void file_open_callback(u32_string* filePath, void* fileData, u32 fileBytes, void* userData) {
    UserData* user = (UserData*)userData;
    user->waiting_for_operation = false;

    // If we have a file, create a document and add it
    if (filePath && fileData && fileBytes > 0) {
        // Convert file path to char* for AddDocumentView
        u32 path_len = u32str_length(filePath);
        char* path = (char*)malloc(path_len + 1);
        for (u32 i = 0; i < path_len; i++) {
            path[i] = (char)u32str_get(filePath, i);
        }
        path[path_len] = '\0';

        // Convert file data to u32_string
        std::vector<u32> u32content;
        char* file_chars = (char*)fileData;
        for (u32 i = 0; i < fileBytes; i++) {
            u32content.push_back((unsigned char)file_chars[i]);
        }
        u32content.push_back(0); // null terminator

        u32_string* file_str = u32str_init(u32content.data());

        // Create document from the string
        document* doc = doc_from_str32(file_str, 100);
        u32str_destroy(file_str);

        if (doc) {
            // Add the document view
            AddDocumentView(user, doc, path);
        }

        free(path);
    }
}

UserData* Initialize(u32 desiredWidth, u32 desiredHeight) {
    UserData* user = new UserData();
    user->offset = 0.0f;

    // Initialize zoom level to 100% first
    user->zoom_level = 1;

    // Adjust canvas size based on initial zoom level
    // At 50% zoom, we need 2x canvas size to fill the screen
    // At 200% zoom, we need 0.5x canvas size
    float scale = 1.0f;
    if (user->zoom_level == 0) scale = 0.5f;       // 50% - need larger canvas
    else if (user->zoom_level == 1) scale = 1.0f;  // 100% - normal
    else if (user->zoom_level == 2) scale = 2.0f;  // 200% - need smaller canvas

    u32 canvasWidth = (u32)(desiredWidth / scale);
    u32 canvasHeight = (u32)(desiredHeight / scale);

    user->cnvs = canvas_create(canvasWidth, canvasHeight);
    user->fnt = font_create(nullptr, 0, 32);
    user->imgui_context = ImGuiInit(user->cnvs, user->fnt);

    // Initialize document views (vector is already empty)
    user->active_view = 0;

    // Initialize async operation state
    user->waiting_for_operation = false;

    // Initialize deferred operations
    user->has_deferred_line_delete = false;
    user->deferred_delete_view = 0;
    user->deferred_delete_line = 0;

    return user;
}

// Global menu state - needs to be accessible from AddDocumentView
static i32 menuIndex = -1;

void Update(UserData* userData, float deltaTime) {
    // Update active document view
    if (!userData->views.empty() && userData->active_view < userData->views.size()) {
        document_view* view = userData->views[userData->active_view];
        if (view) {
            document_view_update(view, deltaTime);
        }
    }
}

void AddDocumentView(UserData* user, document* doc, const char* path) {
    // Check if a document with this path is already open
    if (path) {
        for (size_t i = 0; i < user->views.size(); i++) {
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
                    // printf("File already open in tab %zu, switching to it: %s (was on tab %u)\n", i, path, user->active_view);
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
    view->displayAreaY = 50; // Below menu and tabs (no gap)
    view->displayAreaW = canvas_get_width(user->cnvs);
    view->displayAreaH = canvas_get_height(user->cnvs) - 50;

    user->views.push_back(view);
    user->active_view = user->views.size() - 1;

    // Close any open menu when a new document is opened
    menuIndex = -1;
}

canvas* Render(UserData* user) {
    ImGuiBeginFrame(user->imgui_context);

    // Push disabled state if waiting for async operation
    if (user->waiting_for_operation) {
        ImGuiPushDisabled(user->imgui_context);
    }

    canvas_clear(user->cnvs, 0x1E, 0x1E, 0x1E);  // SPECTRUM_DARKEST_GRAY_50 - Application background

    u32 canvasWidth = canvas_get_width(user->cnvs);
    u32 canvasHeight = canvas_get_height(user->cnvs);

    // menuIndex is now global static
    i32 clickedItem = -1;
    i32 clickedTabItem = -1;

    // Process tab menu if open
    u32 tabMenuW = 400;
    u32 tabMenuX = canvasWidth - tabMenuW;
    if (menuIndex == 4 && !user->views.empty()) {
        u32 tabMenuY = 50;

        // printf("Tab menu is open at %u,%u with %zu items\n", tabMenuX, tabMenuY, user->views.size());
        ImGuiConsumePopupMenuInput(user->imgui_context, tabMenuX, tabMenuY, user->views.size(), tabMenuW);

        for (size_t i = 0; i < user->views.size(); i++) {
            bool clicked = ImGuiProcessMenuItem(user->imgui_context, tabMenuX, tabMenuY, i);
            if (clicked) {
                // printf("Tab menu: selected tab %zu (was %u)\n", i, user->active_view);
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

        // Disable Save and Close if no document is open
        bool hasDocument = !user->views.empty();
        if (!hasDocument) {
            ImGuiPushDisabled(user->imgui_context);
        }

        if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 2)) clickedItem = 2;
        if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 3)) {
            // Close current tab
            if (!user->views.empty() && user->active_view < user->views.size()) {
                document_view* view = user->views[user->active_view];
                document* doc = view->target;

                // Check if document has unsaved changes
                if (doc && doc_is_modified(doc)) {
                    // Build the dialog message
                    std::string message;
                    bool has_path = (view->path != nullptr);

                    if (has_path) {
                        // Document has a file path - extract just the filename
                        u32 path_len = u32str_length(view->path);
                        std::string full_path;
                        for (u32 i = 0; i < path_len; i++) {
                            full_path += (char)u32str_get(view->path, i);
                        }

                        // Extract filename from path
                        size_t last_slash = full_path.find_last_of("/\\");
                        std::string filename = (last_slash != std::string::npos)
                            ? full_path.substr(last_slash + 1)
                            : full_path;

                        message = "Save changes before closing?\n" + filename;
                    } else {
                        // Document doesn't have a file path
                        message = "Save file before closing?\nUntitled";
                    }

                    // Create callback data
                    CloseConfirmData* data = new CloseConfirmData();
                    data->user = user;
                    data->view_index = user->active_view;
                    data->has_path = has_path;

                    // Show the modal dialog
                    platform_modal_yesno(message.c_str(), close_confirm_callback, data);
                } else {
                    // No unsaved changes, close normally
                    if (doc) {
                        doc_destroy(doc);
                    }
                    document_view_destroy(view);

                    // Remove from vector
                    user->views.erase(user->views.begin() + user->active_view);

                    // Adjust active tab after closing
                    if (!user->views.empty()) {
                        if (user->active_view >= user->views.size()) {
                            // We closed the last tab, select the new last tab
                            user->active_view = user->views.size() - 1;
                        }
                        // Otherwise keep the same index (which now points to the next tab)
                    } else {
                        // No tabs left
                        user->active_view = 0;
                    }
                }
            }
            menuIndex = -1;
        }

        if (!hasDocument) {
            ImGuiPopDisabled(user->imgui_context);
        }

        if (ImGuiProcessMenuItem(user->imgui_context, 0, menuY, 4)) clickedItem = 4;

        if (clickedItem >= 0) {
            menuIndex = -1;

            // Handle File menu items
            if (clickedItem == 0) {  // New
                // Create a new empty document with 100 undo levels
                document* doc = doc_create(100);
                // Add an initial empty line
                u32 empty_data[] = {0};
                u32_string* empty_line = u32str_init(empty_data);
                doc_append_line_str32(doc, empty_line);
                u32str_destroy(empty_line);
                // Add the document view with no path (nullptr)
                AddDocumentView(user, doc, nullptr);
            }
            else if (clickedItem == 1) {  // Open
                // Set waiting state and call platform file open
                user->waiting_for_operation = true;
                platform_open_file(file_open_callback, user);
            }
            else if (clickedItem == 2) {  // Save
                // Save the current document
                if (!user->views.empty() && user->active_view < user->views.size()) {
                    document_view* view = user->views[user->active_view];
                    document* doc = view->target;

                    if (doc) {
                        // Convert document content to string (includes newlines between lines)
                        u32_string* content = doc_to_str32(doc);

                        if (content) {
                            // Get the UTF-32 content length
                            u32 content_len = u32str_length(content);

                            // Convert to UTF-8 for compatibility with text editors
                            // Allocate worst-case buffer (4 bytes per character)
                            u32 buffer_size = content_len * 4 + 1;
                            unsigned char* utf8_content = (unsigned char*)malloc(buffer_size);
                            u32 utf8_len = 0;

                            // Convert UTF-32 to UTF-8, preserving all Unicode characters
                            for (u32 i = 0; i < content_len; i++) {
                                u32 ch = u32str_get(content, i);

                                if (ch <= 0x7F) {
                                    // 1-byte sequence (ASCII, including newlines)
                                    utf8_content[utf8_len++] = (unsigned char)ch;
                                } else if (ch <= 0x7FF) {
                                    // 2-byte sequence
                                    utf8_content[utf8_len++] = (unsigned char)(0xC0 | (ch >> 6));
                                    utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                                } else if (ch <= 0xFFFF) {
                                    // 3-byte sequence
                                    utf8_content[utf8_len++] = (unsigned char)(0xE0 | (ch >> 12));
                                    utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                                    utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                                } else if (ch <= 0x10FFFF) {
                                    // 4-byte sequence (for emojis and other extended Unicode)
                                    utf8_content[utf8_len++] = (unsigned char)(0xF0 | (ch >> 18));
                                    utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 12) & 0x3F));
                                    utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                                    utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                                }
                                // Skip invalid Unicode values above 0x10FFFF
                            }

                            if (view->path) {
                                // Document has a backing file - save directly
                                platform_write_file(view->path, utf8_content, utf8_len, save_file_callback, user);
                            } else {
                                // No backing file - show save as dialog
                                platform_save_file_as(utf8_content, utf8_len, save_as_callback, user);
                            }

                            // Clean up
                            free(utf8_content);
                            u32str_destroy(content);
                        }
                    }
                }
            }
            else if (clickedItem == 4) {  // Exit
                platform_exit();
            }
        }
    }
    else if (menuIndex == 1) {
        menuX = 90;
        ImGuiConsumePopupMenuInput(user->imgui_context, menuX, menuY, 6, 220);

        // Check if we can undo/redo
        bool hasDocument = !user->views.empty();
        bool canUndo = false;
        bool canRedo = false;

        if (hasDocument && user->active_view < user->views.size()) {
            document_view* view = user->views[user->active_view];
            if (view && view->target) {
                canUndo = doc_can_undo(view->target);
                canRedo = doc_can_redo(view->target);
            }
        }

        // Disable undo if not possible
        if (!canUndo) {
            ImGuiPushDisabled(user->imgui_context);
        }
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 0)) clickedItem = 0;
        if (!canUndo) {
            ImGuiPopDisabled(user->imgui_context);
        }

        // Disable redo if not possible
        if (!canRedo) {
            ImGuiPushDisabled(user->imgui_context);
        }
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 1)) clickedItem = 1;
        if (!canRedo) {
            ImGuiPopDisabled(user->imgui_context);
        }

        // Disable other edit menu items if no document is open
        if (!hasDocument) {
            ImGuiPushDisabled(user->imgui_context);
        }
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 2)) clickedItem = 2;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 3)) clickedItem = 3;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 4)) clickedItem = 4;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 5)) clickedItem = 5;

        if (!hasDocument) {
            ImGuiPopDisabled(user->imgui_context);
        }

        if (clickedItem >= 0) {
            menuIndex = -1;

            // Handle edit menu operations
            if (!user->views.empty() && user->active_view < user->views.size()) {
                document_view* view = user->views[user->active_view];
                if (view) {
                    if (clickedItem == 0) {  // Undo
                        if (doc_can_undo(view->target)) {
                            document_view_undo(view);
                        }
                    }
                    else if (clickedItem == 1) {  // Redo
                        if (doc_can_redo(view->target)) {
                            document_view_redo(view);
                        }
                    }
                    else if (clickedItem == 2) {  // Cut
                        // Get selected text or whole line
                        u32_string* text_to_cut = document_view_get_selection(view);
                        bool has_selection = (text_to_cut && u32str_length(text_to_cut) > 0);

                        if (!has_selection && text_to_cut) {
                            u32str_destroy(text_to_cut);
                            text_to_cut = nullptr;
                        }

                        // If no selection, get the current line
                        if (!has_selection) {
                            document* doc = view->target;
                            u32 line_row = view->cursor.row;
                            // Make sure the line exists
                            if (line_row < doc_line_count(doc)) {
                                u32_string* line = doc_get_line(doc, line_row);
                                if (line) {
                                    // Create a copy with newline appended
                                    u32 line_len = u32str_length(line);
                                    u32* line_data = (u32*)malloc((line_len + 2) * sizeof(u32));
                                    for (u32 i = 0; i < line_len; i++) {
                                        line_data[i] = u32str_get(line, i);
                                    }
                                    line_data[line_len] = '\n';
                                    line_data[line_len + 1] = 0;
                                    text_to_cut = u32str_init(line_data);
                                    free(line_data);
                                    // DO NOT destroy line - it's owned by the document!
                                }
                            }
                        }

                        if (text_to_cut) {
                            // Copy to clipboard
                            user->waiting_for_operation = true;
                            platform_clipboard_copy_text(text_to_cut, clipboard_copy_callback, user);

                            if (has_selection) {
                                // Delete the selected text
                                document_view_delete_selection(view);
                            } else {
                                // Defer the whole line deletion until after rendering
                                u32 current_line = view->cursor.row;

                                printf("[CUT] Deferring deletion of line %u\n", current_line);

                                // Mark for deferred deletion
                                user->has_deferred_line_delete = true;
                                user->deferred_delete_view = user->active_view;
                                user->deferred_delete_line = current_line;
                            }

                            u32str_destroy(text_to_cut);
                        }
                    }
                    else if (clickedItem == 3) {  // Copy
                        // Get selected text or whole line
                        u32_string* text_to_copy = document_view_get_selection(view);
                        bool has_selection = (text_to_copy && u32str_length(text_to_copy) > 0);

                        if (!has_selection && text_to_copy) {
                            u32str_destroy(text_to_copy);
                            text_to_copy = nullptr;
                        }

                        // If no selection, get the current line
                        if (!has_selection) {
                            document* doc = view->target;
                            u32 line_row = view->cursor.row;
                            // Make sure the line exists
                            if (line_row < doc_line_count(doc)) {
                                u32_string* line = doc_get_line(doc, line_row);
                                if (line) {
                                    // Create a copy with newline appended
                                    u32 line_len = u32str_length(line);
                                    u32* line_data = (u32*)malloc((line_len + 2) * sizeof(u32));
                                    for (u32 i = 0; i < line_len; i++) {
                                        line_data[i] = u32str_get(line, i);
                                    }
                                    line_data[line_len] = '\n';
                                    line_data[line_len + 1] = 0;
                                    text_to_copy = u32str_init(line_data);
                                    free(line_data);
                                    // DO NOT destroy line - it's owned by the document!
                                }
                            }
                        }

                        if (text_to_copy) {
                            // Copy to clipboard
                            user->waiting_for_operation = true;
                            platform_clipboard_copy_text(text_to_copy, clipboard_copy_callback, user);
                            u32str_destroy(text_to_copy);
                        }
                    }
                    else if (clickedItem == 4) {  // Paste
                        user->waiting_for_operation = true;
                        platform_clipboard_paste_text(clipboard_paste_callback, user);
                    }
                }
            }
        }
    }
    else if (menuIndex == 2) {
        menuX = 175;
        ImGuiConsumePopupMenuInput(user->imgui_context, menuX, menuY, 5, 220);

        // Process zoom menu items (0-2)
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 0)) clickedItem = 0;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 1)) clickedItem = 1;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 2)) clickedItem = 2;

        // Disable syntax menu items if no document is open
        bool hasDocument = !user->views.empty();
        if (!hasDocument) {
            ImGuiPushDisabled(user->imgui_context);
        }

        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 3)) clickedItem = 3;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 4)) clickedItem = 4;

        if (!hasDocument) {
            ImGuiPopDisabled(user->imgui_context);
        }

        if (clickedItem >= 0) {
            menuIndex = -1;

            // Handle zoom level changes
            if (clickedItem >= 0 && clickedItem <= 2) {
                user->zoom_level = clickedItem;  // 0=50%, 1=100%, 2=200%

                // Get current window size and recreate canvas with proper dimensions
                u32 windowWidth, windowHeight;
                platform_get_window_size(&windowWidth, &windowHeight);

                // Calculate canvas size based on zoom level
                float scale = 1.0f;
                if (user->zoom_level == 0) scale = 0.5f;       // 50% - need larger canvas
                else if (user->zoom_level == 1) scale = 1.0f;  // 100% - normal
                else if (user->zoom_level == 2) scale = 2.0f;  // 200% - need smaller canvas

                u32 canvasWidth = (u32)(windowWidth / scale);
                u32 canvasHeight = (u32)(windowHeight / scale);

                // Recreate canvas with new size
                canvas_destroy(user->cnvs);
                user->cnvs = canvas_create(canvasWidth, canvasHeight);

                // Update ImGui canvas target
                ImGuiSetTargets(user->imgui_context, user->cnvs, user->fnt);

                // Update view display areas
                for (size_t i = 0; i < user->views.size(); i++) {
                    if (user->views[i]) {
                        user->views[i]->displayAreaW = canvasWidth;
                        user->views[i]->displayAreaH = canvasHeight - 50;  // Account for menu bar
                    }
                }
            }
            // Handle syntax highlighting toggle
            else if ((clickedItem == 3 || clickedItem == 4) && !user->views.empty() && user->active_view < user->views.size()) {
                document_view* view = user->views[user->active_view];
                if (view) {
                    bool syntaxEnabled = (clickedItem == 3);  // 3 = "Syntax: On", 4 = "Syntax: Off"
                    document_view_set_highlight_syntax(view, syntaxEnabled);
                }
            }
        }
    }
    else if (menuIndex == 3) {
        menuX = 265;
        ImGuiConsumePopupMenuInput(user->imgui_context, menuX, menuY, 2, 220);

        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 0)) clickedItem = 0;
        if (ImGuiProcessMenuItem(user->imgui_context, menuX, menuY, 1)) clickedItem = 1;

        if (clickedItem >= 0) {
            menuIndex = -1;

            // Handle Help menu items
            if (clickedItem == 1) {  // Github
                platform_launch_browser("https://github.com/gszauer/CarrotCode");
            }
        }
    }

    // Render menu bar
    ImGuiBeginMenuBar(user->imgui_context, 0, 0, 360, 50, menuIndex);
    ImGuiMenuBarItem(user->imgui_context, "FILE");
    ImGuiMenuBarItem(user->imgui_context, "EDIT");
    ImGuiMenuBarItem(user->imgui_context, "VIEW");
    ImGuiMenuBarItem(user->imgui_context, "HELP");
    menuIndex = ImGuiEndMenuBar(user->imgui_context);

    // Always render tab bar area as part of the header (even with no tabs)
    if (canvasWidth > 360) {
        // Draw tab bar background to match menu bar
        canvas_draw_rect(user->cnvs, 360, 0, canvasWidth - 360, 50, 0x27, 0x27, 0x27);  // SPECTRUM_DARKEST_GRAY_75

        if (!user->views.empty()) {
            ImGuiBeginTabBar(user->imgui_context, 360, 0, canvasWidth - 360, 50, user->views.size(), user->active_view);

            i32 tab_to_close = -1;  // Track which tab to close after the loop

            for (size_t i = 0; i < user->views.size(); i++) {
            char tab_text[256];
            document_view* view = user->views[i];

            bool is_open = true;
            bool is_saved = false;

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

                // Document is saved if it has a backing file AND is not modified
                if (view->target) {
                    is_saved = !doc_is_modified(view->target);
                }

                is_open = ImGuiTab(user->imgui_context, filename, is_saved);
            } else {
                // No backing file - always unsaved
                strcpy(tab_text, "Untitled");
                is_saved = false;
                is_open = ImGuiTab(user->imgui_context, tab_text, is_saved);
            }

            // Mark tab for closing (don't close during iteration)
            if (!is_open && tab_to_close == -1) {
                tab_to_close = i;
            }
        }

            u32 selected_tab = ImGuiEndTabBar(user->imgui_context);

            // Handle tab closing after the tab bar is complete
            if (tab_to_close >= 0 && (size_t)tab_to_close < user->views.size()) {
                // Destroy the document and its view
                if (user->views[tab_to_close]->target) {
                    doc_destroy(user->views[tab_to_close]->target);
                }
                document_view_destroy(user->views[tab_to_close]);

                // Remove from vector
                user->views.erase(user->views.begin() + tab_to_close);

                // Adjust active tab to keep the same document selected
                if (!user->views.empty()) {
                    if ((size_t)tab_to_close == user->active_view) {
                        // We're closing the active tab
                        if ((size_t)tab_to_close < user->views.size()) {
                            // Select the tab that's now at this position (was next)
                            user->active_view = tab_to_close;
                        } else {
                            // We closed the last tab, select the new last tab
                            user->active_view = user->views.size() - 1;
                        }
                    } else if ((size_t)tab_to_close < user->active_view) {
                        // We closed a tab before the active one, shift active index to keep same doc
                        user->active_view--;
                    }
                    // If we closed a tab after the active one, no adjustment needed
                } else {
                    // No tabs left
                    user->active_view = 0;
                }
            } else {
                // Only update active_view if we didn't close a tab and ImGuiEndTabBar returns a valid index
                if (selected_tab < user->views.size()) {
                    user->active_view = selected_tab;
                } else if (!user->views.empty() && user->active_view >= user->views.size()) {
                    // Ensure we always have a valid active tab if tabs exist
                    user->active_view = user->views.size() - 1;
                }
            }

            if (ImGuiTabBarMoreButtonClicked(user->imgui_context)) {
                if (menuIndex == 4) {
                    menuIndex = -1;
                } else {
                    menuIndex = 4;
                }
            }
        }  // End of if (!user->views.empty())
    }  // End of if (canvasWidth > 360)

    // Render document view or drop zone
    if (!user->views.empty() && user->active_view < user->views.size()) {
        document_view* view = user->views[user->active_view];
        if (view) {
            // Update display area
            view->displayAreaX = 0;
            view->displayAreaY = 50;  // Directly below menu/tab bar (50px), no gap
            view->displayAreaW = canvasWidth;
            view->displayAreaH = canvasHeight - 50;

            // Render the document view
            document_view_render(view, user->imgui_context, user->cnvs, user->fnt, true);
        }
    } else {
        // Show drop zone with Spectrum colors
        u32 rect_width = 400;
        u32 rect_height = 60;
        u32 rect_x = (canvasWidth - rect_width) / 2;
        u32 rect_y = (canvasHeight - rect_height) / 2;
        // Use SPECTRUM_DARKEST_GRAY_200 for the rectangle background
        canvas_draw_rect(user->cnvs, rect_x, rect_y, rect_width, rect_height, 0x39, 0x39, 0x39);

        const char* drop_text = "Drop file here to open";
        u32 text_width = font_get_width_cstr(user->fnt, drop_text);
        u32 text_x = (canvasWidth - text_width) / 2;
        u32 text_y = rect_y + (rect_height - font_get_line_height(user->fnt)) / 2;

        // Use SPECTRUM_DARKEST_GRAY_500 for the text
        canvas_draw_text_cstr(user->cnvs, user->fnt, drop_text, text_x, text_y, 0x75, 0x75, 0x75);
    }

    // Render popup menus on top
    if (menuIndex == 0) {
        ImGuiRenderBeginMenu(user->imgui_context, 0, menuY, 5);
        ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 0, "New");
        ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 1, "Open");

        // Disable Save and Close if no document is open
        bool hasDocument = !user->views.empty();
        if (!hasDocument) {
            ImGuiPushDisabled(user->imgui_context);
        }

        ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 2, "Save");
        ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 3, "Close");

        if (!hasDocument) {
            ImGuiPopDisabled(user->imgui_context);
        }

        ImGuiRenderMenuItem(user->imgui_context, 0, menuY, 4, "Exit");
        ImGuiRenderEndMenu(user->imgui_context);
    }
    else if (menuIndex == 1) {
        ImGuiRenderBeginMenu(user->imgui_context, 90, menuY, 6);

        // Check if we can undo/redo
        bool hasDocument = !user->views.empty();
        bool canUndo = false;
        bool canRedo = false;

        if (hasDocument && user->active_view < user->views.size()) {
            document_view* view = user->views[user->active_view];
            if (view && view->target) {
                canUndo = doc_can_undo(view->target);
                canRedo = doc_can_redo(view->target);
            }
        }

        // Disable undo if not possible
        if (!canUndo) {
            ImGuiPushDisabled(user->imgui_context);
        }
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 0, "Undo");
        if (!canUndo) {
            ImGuiPopDisabled(user->imgui_context);
        }

        // Disable redo if not possible
        if (!canRedo) {
            ImGuiPushDisabled(user->imgui_context);
        }
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 1, "Redo");
        if (!canRedo) {
            ImGuiPopDisabled(user->imgui_context);
        }

        // Disable other edit menu items if no document is open
        if (!hasDocument) {
            ImGuiPushDisabled(user->imgui_context);
        }
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 2, "Cut");
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 3, "Copy");
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 4, "Paste");
        ImGuiRenderMenuItem(user->imgui_context, 90, menuY, 5, "Export");

        if (!hasDocument) {
            ImGuiPopDisabled(user->imgui_context);
        }

        ImGuiRenderEndMenu(user->imgui_context);
    }
    else if (menuIndex == 2) {
        ImGuiRenderBeginMenu(user->imgui_context, 175, menuY, 5);

        // Dynamic zoom menu items with indicator for active zoom level
        const char* zoom_50 = (user->zoom_level == 0) ? "> Zoom: 50%" : "Zoom: 50%";
        const char* zoom_100 = (user->zoom_level == 1) ? "> Zoom: 100%" : "Zoom: 100%";
        const char* zoom_200 = (user->zoom_level == 2) ? "> Zoom: 200%" : "Zoom: 200%";

        ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 0, zoom_50);
        ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 1, zoom_100);
        ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 2, zoom_200);

        // Show syntax highlighting state based on current document
        bool hasDocument = !user->views.empty();
        bool syntaxEnabled = false;

        if (hasDocument && user->active_view < user->views.size()) {
            document_view* view = user->views[user->active_view];
            if (view) {
                syntaxEnabled = document_view_get_highlight_syntax(view);
            }
        }

        // Disable syntax options if no document is open
        if (!hasDocument) {
            ImGuiPushDisabled(user->imgui_context);
        }

        // Show with > indicator based on current state
        if (hasDocument && syntaxEnabled) {
            ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 3, "> Syntax: On");
            ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 4, "Syntax: Off");
        } else if (hasDocument && !syntaxEnabled) {
            ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 3, "Syntax: On");
            ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 4, "> Syntax: Off");
        } else {
            // No document open - show without > indicator
            ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 3, "Syntax: On");
            ImGuiRenderMenuItem(user->imgui_context, 175, menuY, 4, "Syntax: Off");
        }

        if (!hasDocument) {
            ImGuiPopDisabled(user->imgui_context);
        }
        ImGuiRenderEndMenu(user->imgui_context);
    }
    else if (menuIndex == 3) {
        ImGuiRenderBeginMenu(user->imgui_context, 265, menuY, 2);
        ImGuiRenderMenuItem(user->imgui_context, 265, menuY, 0, "About");
        ImGuiRenderMenuItem(user->imgui_context, 265, menuY, 1, "Github");
        ImGuiRenderEndMenu(user->imgui_context);
    }
    else if (menuIndex == 4 && !user->views.empty()) {
        u32 tabMenuY = 50;

        ImGuiRenderBeginMenu(user->imgui_context, tabMenuX, tabMenuY, user->views.size());

        for (size_t i = 0; i < user->views.size(); i++) {
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
                strcpy(tab_text, "Unsaved");
                ImGuiRenderMenuItem(user->imgui_context, tabMenuX, tabMenuY, i, tab_text);
            }
        }

        ImGuiRenderEndMenu(user->imgui_context);
    }

    // Pop disabled state if we pushed it
    if (user->waiting_for_operation) {
        ImGuiPopDisabled(user->imgui_context);
    }

    ImGuiEndFrame(user->imgui_context);

    // Handle keyboard input for active document view
    if (!user->views.empty() && user->active_view < user->views.size()) {
        document_view* view = user->views[user->active_view];
        if (view) {
            // Forward keyboard input to the document view
            // This would be connected to actual keyboard events from the platform layer
        }
    }

    // Process deferred line deletion at end of frame (after rendering)
    if (user->has_deferred_line_delete) {
        printf("[DEFERRED] Processing deferred line deletion at end of frame\n");
        if (user->deferred_delete_view < user->views.size()) {
            document_view* view = user->views[user->deferred_delete_view];
            if (view) {
                document* doc = view->target;
                u32 line_to_delete = user->deferred_delete_line;

                printf("[DEFERRED] Deleting line %u from view %u\n", line_to_delete, user->deferred_delete_view);
                printf("[DEFERRED] Document has %u lines before delete\n", doc_line_count(doc));

                // Delete the line
                doc_delete_line(doc, line_to_delete);

                printf("[DEFERRED] Document has %u lines after delete\n", doc_line_count(doc));

                // Adjust cursor position if needed
                if (line_to_delete >= doc_line_count(doc) && line_to_delete > 0) {
                    // If we deleted the last line, move cursor to previous line
                    printf("[DEFERRED] Moving cursor to previous line %u\n", line_to_delete - 1);
                    document_view_set_cursor(view, line_to_delete - 1, 0);
                } else {
                    // Otherwise stay on the same line number (which is now the next line)
                    printf("[DEFERRED] Keeping cursor on line %u\n", line_to_delete);
                    document_view_set_cursor(view, line_to_delete, 0);
                }
                printf("[DEFERRED] Cursor adjustment complete\n");
            }
        }
        user->has_deferred_line_delete = false;
    }

    return user->cnvs;
}

void Shutdown(void* userData) {
    UserData* user = (UserData*)userData;

    // Clean up document views and their documents
    for (document_view* view : user->views) {
        if (view) {
            if (view->target) {
                doc_destroy(view->target);
            }
            document_view_destroy(view);
        }
    }

    ImGuiShutdown(user->imgui_context);
    canvas_destroy(user->cnvs);
    font_destroy(user->fnt);
    delete user;
}