#include "application.h"
#include "platform.h"
#include "strings.h"
#include "logo_256.xpm"  // Include XPM icon data
#include <map>
#include <string>
#include <cstring>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <sys/time.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fstream>
#include <vector>
#include <cctype>

#include "renderer.h"
#include "strings.h"
#include "document.h"
#include "imgui.h"

struct WindowData {
    Display* display;
    Window window;
    GC gc;
    XImage* backBuffer;
    unsigned int* pixels;
    int screen;
    int width;
    int height;
    bool closeWindow;
};

#define WINDOW_WIDTH 1600
#define WINDOW_HEIGHT 1200
#define WINDOW_TITLE "Carrot Code"

// External callback functions from application.cpp
extern void clipboard_copy_callback(void* userData);
extern void clipboard_paste_callback(u32_string* content, void* userData);
extern void file_open_callback(u32_string* filePath, void* fileData, u32 fileBytes, void* userData);
extern void save_file_callback(bool success, void* userData);
extern void save_as_callback(u32_string* filePath, void* userData);
extern void AddDocumentView(UserData* user, document* doc, const char* path);

// Global window data for platform_exit
static WindowData* g_windowData = nullptr;

long long GetTimeInMilliseconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int main(int argc, char** argv) {
    WindowData windowData = {};
    g_windowData = &windowData;  // Set global pointer for platform_exit

    // Open connection to X server
    windowData.display = XOpenDisplay(NULL);
    if (windowData.display == NULL) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }
    
    windowData.screen = DefaultScreen(windowData.display);
    windowData.width = WINDOW_WIDTH;
    windowData.height = WINDOW_HEIGHT;
    windowData.closeWindow = false;
    
    // Declare atoms at the beginning for use throughout
    Atom wmDeleteMessage;
    Atom XdndAware, XdndTypeList, XdndDrop, XdndEnter;
    Atom XdndPosition, XdndStatus, XdndLeave, XdndFinished;
    Atom XdndSelection, XdndActionCopy, textUriList;
    Atom XdndSelectionProperty;
    
    // Create window
    Window rootWindow = DefaultRootWindow(windowData.display);
    
    XSetWindowAttributes windowAttributes;
    windowAttributes.background_pixel = BlackPixel(windowData.display, windowData.screen);
    windowAttributes.border_pixel = BlackPixel(windowData.display, windowData.screen);
    windowAttributes.backing_store = Always;
    windowAttributes.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | PropertyChangeMask;
    
    windowData.window = XCreateWindow(
        windowData.display,
        rootWindow,
        0, 0,
        windowData.width, windowData.height,
        0,
        DefaultDepth(windowData.display, windowData.screen),
        InputOutput,
        DefaultVisual(windowData.display, windowData.screen),
        CWBackPixel | CWBorderPixel | CWBackingStore | CWEventMask,
        &windowAttributes
    );
    
    // Set window title
    XStoreName(windowData.display, windowData.window, WINDOW_TITLE);

    // Set window icon from logo_256.xpm - manual parsing
    {
        // Parse XPM data manually
        const char** xpm_data = b0ae89c132d64a2c8b12a6466c522e8016u6IrXAJrrPEpEz;

        // First line contains: width height num_colors chars_per_pixel
        int width, height, num_colors, chars_per_pixel;
        sscanf(xpm_data[0], "%d %d %d %d", &width, &height, &num_colors, &chars_per_pixel);

        // Parse color table
        std::map<std::string, unsigned long> color_map;
        for (int i = 1; i <= num_colors; i++) {
            const char* line = xpm_data[i];
            std::string key(line, chars_per_pixel);

            // Find the color definition (look for 'c #' or 'c None')
            const char* color_start = strstr(line, "c ");
            if (color_start) {
                color_start += 2; // Skip "c "

                unsigned long color;
                if (strncmp(color_start, "None", 4) == 0) {
                    color = 0x00000000; // Transparent
                } else if (color_start[0] == '#') {
                    // Parse hex color
                    unsigned int r, g, b;
                    sscanf(color_start + 1, "%02x%02x%02x", &r, &g, &b);
                    color = 0xFF000000 | (r << 16) | (g << 8) | b; // ARGB with full opacity
                } else {
                    color = 0xFF000000; // Default to opaque black
                }
                color_map[key] = color;
            }
        }

        // Allocate icon data for _NET_WM_ICON
        unsigned long* icon_data = (unsigned long*)malloc((2 + width * height) * sizeof(unsigned long));
        icon_data[0] = width;
        icon_data[1] = height;

        // Parse pixel data
        int pixel_index = 2;
        for (int y = 0; y < height; y++) {
            const char* line = xpm_data[1 + num_colors + y];
            for (int x = 0; x < width; x++) {
                std::string pixel_key(line + (x * chars_per_pixel), chars_per_pixel);
                auto it = color_map.find(pixel_key);
                if (it != color_map.end()) {
                    icon_data[pixel_index++] = it->second;
                } else {
                    icon_data[pixel_index++] = 0xFF000000; // Default to opaque black
                }
            }
        }

        // Set _NET_WM_ICON property
        Atom net_wm_icon = XInternAtom(windowData.display, "_NET_WM_ICON", False);
        Atom cardinal = XInternAtom(windowData.display, "CARDINAL", False);

        XChangeProperty(windowData.display, windowData.window, net_wm_icon, cardinal, 32,
                      PropModeReplace, (unsigned char*)icon_data,
                      2 + width * height);

        free(icon_data);
    }

    // Create graphics context
    windowData.gc = XCreateGC(windowData.display, windowData.window, 0, NULL);
    
    // Set up WM_DELETE_WINDOW protocol
    wmDeleteMessage = XInternAtom(windowData.display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(windowData.display, windowData.window, &wmDeleteMessage, 1);
    
    // Set up drag and drop
    XdndAware = XInternAtom(windowData.display, "XdndAware", False);
    Atom XdndVersion = 5;
    XChangeProperty(windowData.display, windowData.window, XdndAware, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)&XdndVersion, 1);
    
    XdndTypeList = XInternAtom(windowData.display, "XdndTypeList", False);
    XdndDrop = XInternAtom(windowData.display, "XdndDrop", False);
    XdndEnter = XInternAtom(windowData.display, "XdndEnter", False);
    XdndPosition = XInternAtom(windowData.display, "XdndPosition", False);
    XdndStatus = XInternAtom(windowData.display, "XdndStatus", False);
    XdndLeave = XInternAtom(windowData.display, "XdndLeave", False);
    XdndFinished = XInternAtom(windowData.display, "XdndFinished", False);
    XdndSelection = XInternAtom(windowData.display, "XdndSelection", False);
    XdndActionCopy = XInternAtom(windowData.display, "XdndActionCopy", False);
    textUriList = XInternAtom(windowData.display, "text/uri-list", False);
    XdndSelectionProperty = XInternAtom(windowData.display, "XdndSelectionProperty", False);
    
    // Allocate back buffer
    windowData.pixels = (unsigned int*)malloc(windowData.width * windowData.height * sizeof(unsigned int));
    memset(windowData.pixels, 0, windowData.width * windowData.height * sizeof(unsigned int));
    
    // Create XImage for back buffer
    Visual* visual = DefaultVisual(windowData.display, windowData.screen);
    int depth = DefaultDepth(windowData.display, windowData.screen);
    
    windowData.backBuffer = XCreateImage(
        windowData.display,
        visual,
        depth,
        ZPixmap,
        0,
        (char*)windowData.pixels,
        windowData.width,
        windowData.height,
        32,
        0
    );
    
    // Map window (make it visible)
    XMapWindow(windowData.display, windowData.window);
    XFlush(windowData.display);
    
    // Initialize user data
    // Initialize with window dimensions - zoom will be handled in rendering
    UserData* user = Initialize(windowData.width, windowData.height);
    
    // Main loop
    XEvent event;
    bool running = true;
    long long lastTime = GetTimeInMilliseconds();
    Window xdndSourceWindow = None;  // Track the drag source window
    
    while (running && !windowData.closeWindow) {
        // Process events
        while (XPending(windowData.display) > 0) {
            XNextEvent(windowData.display, &event);
            
            switch (event.type) {
                case Expose:
                    // Redraw on expose
                    if (event.xexpose.count == 0) {
                        XPutImage(windowData.display, windowData.window, windowData.gc,
                                  windowData.backBuffer, 0, 0, 0, 0,
                                  windowData.width, windowData.height);
                    }
                    break;
                    
                case ConfigureNotify:
                    // Handle window resize if needed
                    if (event.xconfigure.width != windowData.width || 
                        event.xconfigure.height != windowData.height) {
                        windowData.width = event.xconfigure.width;
                        windowData.height = event.xconfigure.height;
                        
                        // Get visual and depth for this display
                        Visual* resize_visual = DefaultVisual(windowData.display, windowData.screen);
                        int resize_depth = DefaultDepth(windowData.display, windowData.screen);
                        
                        // Reallocate back buffer
                        free(windowData.pixels);
                        windowData.pixels = (unsigned int*)malloc(windowData.width * windowData.height * sizeof(unsigned int));
                        memset(windowData.pixels, 0, windowData.width * windowData.height * sizeof(unsigned int));
                        
                        // Recreate XImage
                        windowData.backBuffer->data = NULL; // Prevent double free
                        XDestroyImage(windowData.backBuffer);
                        windowData.backBuffer = XCreateImage(
                            windowData.display,
                            resize_visual,
                            resize_depth,
                            ZPixmap,
                            0,
                            (char*)windowData.pixels,
                            windowData.width,
                            windowData.height,
                            32,
                            0
                        );
                        
                        // Recreate canvas with new size - adjust for zoom level
                        float scale = 1.0f;
                        if (user->zoom_level == 0) scale = 0.5f;       // 50%
                        else if (user->zoom_level == 1) scale = 1.0f;  // 100%
                        else if (user->zoom_level == 2) scale = 2.0f;  // 200%

                        canvas_destroy(user->cnvs);
                        user->cnvs = canvas_create((u32)(windowData.width / scale), (u32)(windowData.height / scale));
                        // Update ImGui canvas target
                        ImGuiSetTargets(user->imgui_context, user->cnvs, user->fnt);
                    }
                    break;
                    
                case ClientMessage:
                    // Check for window close
                    if ((Atom)event.xclient.data.l[0] == wmDeleteMessage) {
                        windowData.closeWindow = true;
                    }
                    // Handle drag and drop events
                    else if (event.xclient.message_type == XdndEnter) {
                        // DnD operation started
                        xdndSourceWindow = event.xclient.data.l[0];
                    }
                    else if (event.xclient.message_type == XdndPosition) {
                        // Send XdndStatus
                        Window source = event.xclient.data.l[0];
                        XEvent reply;
                        memset(&reply, 0, sizeof(reply));
                        reply.type = ClientMessage;
                        reply.xclient.window = source;
                        reply.xclient.message_type = XdndStatus;
                        reply.xclient.format = 32;
                        reply.xclient.data.l[0] = windowData.window;
                        reply.xclient.data.l[1] = 1; // Accept
                        reply.xclient.data.l[2] = 0; // Rectangle
                        reply.xclient.data.l[3] = 0;
                        reply.xclient.data.l[4] = XdndActionCopy;
                        XSendEvent(windowData.display, source, False, NoEventMask, &reply);
                        XFlush(windowData.display);
                    }
                    else if (event.xclient.message_type == XdndLeave) {
                        // Drag cancelled
                        XDeleteProperty(windowData.display, windowData.window, XdndSelectionProperty);
                        xdndSourceWindow = None;
                    }
                    else if (event.xclient.message_type == XdndDrop) {
                        // Request the selection
                        xdndSourceWindow = event.xclient.data.l[0];
                        Time timestamp = (Time)event.xclient.data.l[2];
                        
                        // Clear any existing property first
                        XDeleteProperty(windowData.display, windowData.window, XdndSelectionProperty);
                        XFlush(windowData.display);
                        
                        // Use the timestamp from the drop event if available
                        XConvertSelection(windowData.display, XdndSelection, textUriList,
                                        XdndSelectionProperty, windowData.window, 
                                        timestamp ? timestamp : CurrentTime);
                        XFlush(windowData.display);
                    }
                    break;
                    
                case SelectionNotify:
                    if (event.xselection.property == XdndSelectionProperty) {
                        Atom actual_type;
                        int actual_format;
                        unsigned long nitems, bytes_after;
                        unsigned char* data = nullptr;
                        
                        if (XGetWindowProperty(windowData.display, windowData.window,
                                             XdndSelectionProperty, 0, ~0L, True,  // Delete after reading
                                             AnyPropertyType, &actual_type, &actual_format,
                                             &nitems, &bytes_after, &data) == Success) {
                            
                            if (data && nitems > 0) {
                                // Parse URI list (file://path format)
                                std::string uri((char*)data, nitems);

                                // Process all files in the drop (they're separated by newlines)
                                size_t pos = 0;
                                while (pos < uri.length()) {
                                    // Find the next file:// URI
                                    size_t file_start = uri.find("file://", pos);
                                    if (file_start == std::string::npos) {
                                        break;  // No more files
                                    }

                                    // Extract this URI up to the next newline or end of string
                                    size_t uri_end = uri.find_first_of("\r\n", file_start);
                                    std::string file_uri;
                                    if (uri_end != std::string::npos) {
                                        file_uri = uri.substr(file_start, uri_end - file_start);
                                        pos = uri_end + 1;  // Move past this URI for next iteration
                                    } else {
                                        file_uri = uri.substr(file_start);
                                        pos = uri.length();  // This was the last URI
                                    }

                                    // Extract path from file:// URI
                                    if (file_uri.substr(0, 7) == "file://") {
                                        std::string filepath = file_uri.substr(7);

                                        // URL decode the path (handle %20 for spaces, etc)
                                        size_t decode_pos = 0;
                                        while ((decode_pos = filepath.find('%', decode_pos)) != std::string::npos) {
                                            if (decode_pos + 2 < filepath.length()) {
                                                std::string hex = filepath.substr(decode_pos + 1, 2);
                                                char ch = (char)std::stoi(hex, nullptr, 16);
                                                filepath.replace(decode_pos, 3, 1, ch);
                                            }
                                            decode_pos++;
                                        }

                                        // Read file and create document
                                        std::ifstream file(filepath, std::ios::binary);
                                        if (file.is_open()) {
                                            std::string content((std::istreambuf_iterator<char>(file)),
                                                              std::istreambuf_iterator<char>());
                                            file.close();

                                            // Convert to u32_string
                                            std::vector<u32> u32content;
                                            for (char c : content) {
                                                u32content.push_back((u32)(unsigned char)c);
                                            }
                                            u32content.push_back(0);

                                            u32_string* file_str = u32str_init(u32content.data());

                                            // Create document and add it as a view
                                            document* new_doc = doc_from_str32(file_str, 100);
                                            u32str_destroy(file_str);

                                            // Add document view
                                            AddDocumentView(user, new_doc, filepath.c_str());
                                        }
                                    }  // end if (file_uri.substr(0, 7) == "file://")
                                }  // end while loop processing all files
                                XFree(data);
                            }
                        }
                        
                        // Send XdndFinished to the source window
                        if (xdndSourceWindow != None) {
                            XEvent reply;
                            memset(&reply, 0, sizeof(reply));
                            reply.type = ClientMessage;
                            reply.xclient.window = xdndSourceWindow;
                            reply.xclient.message_type = XdndFinished;
                            reply.xclient.format = 32;
                            reply.xclient.data.l[0] = windowData.window;
                            reply.xclient.data.l[1] = 1; // Accepted
                            reply.xclient.data.l[2] = XdndActionCopy;
                            XSendEvent(windowData.display, xdndSourceWindow, False, NoEventMask, &reply);
                            XFlush(windowData.display);
                        }
                        
                        // Clear the property and source window
                        XDeleteProperty(windowData.display, windowData.window, XdndSelectionProperty);
                        xdndSourceWindow = None;
                    }
                    break;
                    
                case KeyPress:
                case KeyRelease:
                    {
                        bool isKeyDown = (event.type == KeyPress);

                        // Use XLookupString to get the properly shifted character
                        char buffer[32];
                        KeySym keysym;
                        int len = XLookupString(&event.xkey, buffer, sizeof(buffer), &keysym, nullptr);

                        // Exit on Escape key
                        if (isKeyDown && keysym == XK_Escape) {
                            windowData.closeWindow = true;
                        }

                        // Pass keyboard input to ImGui
                        u32 virtualKeyCode = (u32)keysym;
                        u32 characterCode = 0;

                        // Get the actual character with shift/caps lock applied
                        if (isKeyDown && len > 0) {
                            // XLookupString returns the properly shifted character
                            characterCode = (unsigned char)buffer[0];
                        }

                        bool altDown = (event.xkey.state & Mod1Mask) != 0;
                        bool ctrlDown = (event.xkey.state & ControlMask) != 0;
                        bool shiftDown = (event.xkey.state & ShiftMask) != 0;

                        // Check for clipboard shortcuts before passing to ImGui/document
                        bool handled = false;
                        if (isKeyDown && ctrlDown && !user->waiting_for_operation) {
                            // Handle Ctrl+N and Ctrl+O even when no document is open
                            if (keysym == XK_o || keysym == XK_O) {  // Ctrl+O (Open)
                                user->waiting_for_operation = true;
                                platform_open_file(file_open_callback, user);
                                handled = true;
                            }
                            else if (keysym == XK_n || keysym == XK_N) {  // Ctrl+N (New)
                                // Create a new empty document
                                document* doc = doc_create(100);
                                u32 empty_data[] = {0};
                                u32_string* empty_line = u32str_init(empty_data);
                                doc_append_line_str32(doc, empty_line);
                                u32str_destroy(empty_line);
                                AddDocumentView(user, doc, nullptr);
                                handled = true;
                            }
                            // Other shortcuts require an active document view
                            else if (!user->views.empty() && user->active_view < user->views.size()) {
                                document_view* view = user->views[user->active_view];
                                if (view) {
                                    if (keysym == XK_x || keysym == XK_X) {  // Ctrl+X (Cut)
                                        ApplicationCut(user);
                                        handled = true;
                                    }
                                    else if (keysym == XK_c || keysym == XK_C) {  // Ctrl+C (Copy)
                                        ApplicationCopy(user);
                                        handled = true;
                                    }
                                    else if (keysym == XK_v || keysym == XK_V) {  // Ctrl+V (Paste)
                                        ApplicationPaste(user);
                                        handled = true;
                                    }
                                    else if (keysym == XK_a || keysym == XK_A) {  // Ctrl+A (Select All)
                                        document_view_select_all(view);
                                        handled = true;
                                    }
                                    else if (keysym == XK_s || keysym == XK_S) {  // Ctrl+S (Save) or Ctrl+Shift+S (Save As)
                                        if (shiftDown) {
                                            // Ctrl+Shift+S - Save As
                                            // Trigger save as for the current document
                                            document* doc = document_view_get_document(view);
                                            if (doc) {
                                                // Convert document content to string
                                                u32_string* content = doc_to_str32(doc);
                                                if (content) {
                                                    // Get the UTF-32 content length
                                                    u32 content_len = u32str_length(content);
                                                    // Convert to UTF-8
                                                    u32 buffer_size = content_len * 4 + 1;
                                                    unsigned char* utf8_content = (unsigned char*)malloc(buffer_size);
                                                    u32 utf8_len = 0;
                                                    for (u32 i = 0; i < content_len; i++) {
                                                        u32 ch = u32str_get(content, i);
                                                        if (ch <= 0x7F) {
                                                            utf8_content[utf8_len++] = (unsigned char)ch;
                                                        } else if (ch <= 0x7FF) {
                                                            utf8_content[utf8_len++] = (unsigned char)(0xC0 | (ch >> 6));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                                                        } else if (ch <= 0xFFFF) {
                                                            utf8_content[utf8_len++] = (unsigned char)(0xE0 | (ch >> 12));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                                                        } else if (ch <= 0x10FFFF) {
                                                            utf8_content[utf8_len++] = (unsigned char)(0xF0 | (ch >> 18));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 12) & 0x3F));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                                                        }
                                                    }
                                                    platform_save_file_as(utf8_content, utf8_len, save_as_callback, user);
                                                    free(utf8_content);
                                                    u32str_destroy(content);
                                                }
                                            }
                                        } else {
                                            // Ctrl+S - Save
                                            document* doc = document_view_get_document(view);
                                            if (doc) {
                                                // Convert document content to string
                                                u32_string* content = doc_to_str32(doc);
                                                if (content) {
                                                    // Get the UTF-32 content length
                                                    u32 content_len = u32str_length(content);
                                                    // Convert to UTF-8
                                                    u32 buffer_size = content_len * 4 + 1;
                                                    unsigned char* utf8_content = (unsigned char*)malloc(buffer_size);
                                                    u32 utf8_len = 0;
                                                    for (u32 i = 0; i < content_len; i++) {
                                                        u32 ch = u32str_get(content, i);
                                                        if (ch <= 0x7F) {
                                                            utf8_content[utf8_len++] = (unsigned char)ch;
                                                        } else if (ch <= 0x7FF) {
                                                            utf8_content[utf8_len++] = (unsigned char)(0xC0 | (ch >> 6));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                                                        } else if (ch <= 0xFFFF) {
                                                            utf8_content[utf8_len++] = (unsigned char)(0xE0 | (ch >> 12));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                                                        } else if (ch <= 0x10FFFF) {
                                                            utf8_content[utf8_len++] = (unsigned char)(0xF0 | (ch >> 18));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 12) & 0x3F));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | ((ch >> 6) & 0x3F));
                                                            utf8_content[utf8_len++] = (unsigned char)(0x80 | (ch & 0x3F));
                                                        }
                                                    }
                                                    if (document_view_get_path(view)) {
                                                        // Has backing file - save directly
                                                        platform_write_file(document_view_get_path(view), utf8_content, utf8_len, save_file_callback, user);
                                                    } else {
                                                        // No backing file - save as
                                                        platform_save_file_as(utf8_content, utf8_len, save_as_callback, user);
                                                    }
                                                    free(utf8_content);
                                                    u32str_destroy(content);
                                                }
                                            }
                                        }
                                        handled = true;
                                    }
                                    else if (keysym == XK_d || keysym == XK_D) {  // Ctrl+D (Duplicate Line)
                                        document* doc = document_view_get_document(view);
                                        u32 line_row = document_view_get_cursor_row(view);
                                        if (doc && line_row < doc_line_count(doc)) {
                                            // Get the current line
                                            u32_string* line = doc_get_line(doc, line_row);
                                            if (line) {
                                                // Create a copy of the line
                                                u32 line_len = u32str_length(line);
                                                u32* line_data = (u32*)malloc((line_len + 1) * sizeof(u32));
                                                for (u32 i = 0; i < line_len; i++) {
                                                    line_data[i] = u32str_get(line, i);
                                                }
                                                line_data[line_len] = 0;
                                                u32_string* duplicate = u32str_init(line_data);
                                                free(line_data);

                                                // Insert the duplicate after the current line
                                                doc_insert_line_str32(doc, line_row + 1, duplicate);
                                                u32str_destroy(duplicate);

                                                // Move cursor to the duplicated line
                                                document_view_set_cursor_row(view, line_row + 1);
                                            }
                                        }
                                        handled = true;
                                    }
                                    else if (keysym == XK_l || keysym == XK_L) {  // Ctrl+L (Delete Line)
                                        document* doc = document_view_get_document(view);
                                        u32 line_row = document_view_get_cursor_row(view);
                                        if (doc && line_row < doc_line_count(doc)) {
                                            // Delete the current line
                                            doc_delete_line(doc, line_row);

                                            // Adjust cursor if needed
                                            u32 new_line_count = doc_line_count(doc);
                                            if (document_view_get_cursor_row(view) >= new_line_count && new_line_count > 0) {
                                                document_view_set_cursor_row(view, new_line_count - 1);
                                            }
                                            document_view_set_cursor_col(view, 0);
                                        }
                                        handled = true;
                                    }
                                    else if (keysym == XK_z || keysym == XK_Z) {  // Ctrl+Z (Undo)
                                        if (doc_can_undo(document_view_get_document(view))) {
                                            document_view_undo(view);
                                        }
                                        handled = true;
                                    }
                                    else if (keysym == XK_y || keysym == XK_Y) {  // Ctrl+Y (Redo)
                                        if (doc_can_redo(document_view_get_document(view))) {
                                            document_view_redo(view);
                                        }
                                        handled = true;
                                    }
                                }
                            }
                        }

                        // Only forward input if not handled
                        if (!handled) {
                            ImGuiKeyboardInput(user->imgui_context, characterCode, virtualKeyCode,
                                             isKeyDown, altDown, ctrlDown, shiftDown);

                            // Also forward keyboard input to the active document view
                            if (!user->views.empty() && user->active_view < user->views.size()) {
                                document_view* view = user->views[user->active_view];
                                if (view) {
                                    document_view_keyboard_input(view, characterCode, virtualKeyCode,
                                                                isKeyDown, altDown, ctrlDown, shiftDown);
                                }
                            }
                        }
                    }
                    break;

                case ButtonPress:
                case ButtonRelease:
                case MotionNotify:
                    {
                        // Get mouse position and adjust for zoom
                        float scale = 1.0f;
                        if (user->zoom_level == 0) scale = 0.5f;       // 50%
                        else if (user->zoom_level == 1) scale = 1.0f;  // 100%
                        else if (user->zoom_level == 2) scale = 2.0f;  // 200%

                        u32 mouseX = (u32)(event.xbutton.x / scale);
                        u32 mouseY = (u32)(event.xbutton.y / scale);
                        f32 normX = (f32)mouseX / windowData.width;
                        f32 normY = (f32)mouseY / windowData.height;

                        // Get button states
                        bool leftDown = false;
                        bool middleDown = false;
                        bool rightDown = false;
                        f32 scrollDir = 0.0f;

                        if (event.type == ButtonPress || event.type == ButtonRelease) {
                            bool isPressed = (event.type == ButtonPress);
                            switch (event.xbutton.button) {
                                case Button1: leftDown = isPressed; break;
                                case Button2: middleDown = isPressed; break;
                                case Button3: rightDown = isPressed; break;
                                case Button4: if (isPressed) scrollDir = 1.0f; break;  // Scroll up
                                case Button5: if (isPressed) scrollDir = -1.0f; break; // Scroll down
                            }
                        } else if (event.type == MotionNotify) {
                            // For motion events, check current button states
                            leftDown = (event.xmotion.state & Button1Mask) != 0;
                            middleDown = (event.xmotion.state & Button2Mask) != 0;
                            rightDown = (event.xmotion.state & Button3Mask) != 0;
                        }

                        ImGuiMouseInput(user->imgui_context, mouseX, mouseY, normX, normY,
                                      scrollDir, leftDown, middleDown, rightDown);

                        // Check if mouse is over tab bar area (hardcoded for now since we know the layout)
                        bool overTabBar = false;
                        if (!user->views.empty() && mouseX >= 360 && mouseY <= 50) {
                            overTabBar = true;
                            // Don't forward scroll input if over tab bar
                            if (scrollDir != 0) {
                                scrollDir = 0;
                            }
                        }

                        if (event.type == ButtonPress && event.xbutton.button == Button3) {
                            bool hasDocument = !user->views.empty();
                            bool inDocumentArea = mouseY >= 51;
                            if (hasDocument && inDocumentArea && !overTabBar && !ImGuiIsMouseConsumed(user->imgui_context)) {
                                user->show_context_menu = true;
                                user->context_menu_x = mouseX;
                                user->context_menu_y = mouseY;
                            } else {
                                user->show_context_menu = false;
                            }
                        }

                        // Forward mouse input to active document view if ImGui didn't consume it
                        if (!ImGuiIsMouseConsumed(user->imgui_context) && !overTabBar) {
                            if (!user->views.empty() && user->active_view < user->views.size()) {
                                document_view* view = user->views[user->active_view];
                                if (view) {
                                    if (event.type == ButtonPress || event.type == ButtonRelease) {
                                        document_view_mouse_input(view, mouseX, mouseY, scrollDir,
                                                                leftDown, middleDown, rightDown);
                                    } else if (event.type == MotionNotify && leftDown) {
                                        // Only send mouse move if left button is down (for selection)
                                        document_view_mouse_moved(view, mouseX, mouseY, leftDown);
                                    }
                                }
                            }
                        }
                    }
                    break;
            }
        }
        
        // Update and render
        long long currentTime = GetTimeInMilliseconds();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;
        
        Update(user, deltaTime);

        {
            // Clear the window buffer first
            memset(windowData.pixels, 0, windowData.width * windowData.height * sizeof(u32));

            // Draw application
            canvas* toDraw = Render(user);

            // Blit Safety check
            u32* doc_canvas_pixels = canvas_get_raw_pixels(toDraw);
            u32 doc_canvas_width = canvas_get_width(toDraw);
            u32 doc_canvas_height = canvas_get_height(toDraw);

            if (doc_canvas_pixels && doc_canvas_width > 0 && doc_canvas_height > 0) {
                // Get zoom scale factor
                float scale = 1.0f;
                if (user->zoom_level == 0) scale = 0.5f;       // 50%
                else if (user->zoom_level == 1) scale = 1.0f;  // 100%
                else if (user->zoom_level == 2) scale = 2.0f;  // 200%

                // Calculate scaled dimensions
                u32 scaled_width = (u32)(doc_canvas_width * scale);
                u32 scaled_height = (u32)(doc_canvas_height * scale);

                // Simple nearest-neighbor scaling
                if (scale == 1.0f) {
                    // No scaling needed - direct copy
                    u32 copy_width = (doc_canvas_width < (u32)windowData.width) ? doc_canvas_width : windowData.width;
                    u32 copy_height = (doc_canvas_height < (u32)windowData.height) ? doc_canvas_height : windowData.height;

                    for (u32 y = 0; y < copy_height; y++) {
#ifdef RAW_COPY
                        memcpy(windowData.pixels + y * windowData.width,
                            doc_canvas_pixels + y * doc_canvas_width,
                            copy_width * sizeof(u32));
#else
                        // Swizzle RGBA to BGRA for X11
                        for (u32 x = 0; x < copy_width; x++) {
                            u32 pixel = doc_canvas_pixels[y * doc_canvas_width + x];
                            // Extract RGBA components
                            u32 r = pixel & 0xFF;
                            u32 g = (pixel >> 8) & 0xFF;
                            u32 b = (pixel >> 16) & 0xFF;
                            u32 a = (pixel >> 24) & 0xFF;
                            // Repack as BGRA for X11
                            windowData.pixels[y * windowData.width + x] = (a << 24) | (r << 16) | (g << 8) | b;
                        }
#endif
                    }
                } else {
                    // Scaled copy with nearest-neighbor sampling
                    u32 copy_width = (scaled_width < (u32)windowData.width) ? scaled_width : windowData.width;
                    u32 copy_height = (scaled_height < (u32)windowData.height) ? scaled_height : windowData.height;

                    for (u32 y = 0; y < copy_height; y++) {
                        u32 src_y = (u32)(y / scale);
                        if (src_y >= doc_canvas_height) continue;

                        for (u32 x = 0; x < copy_width; x++) {
                            u32 src_x = (u32)(x / scale);
                            if (src_x >= doc_canvas_width) continue;

                            u32 pixel = doc_canvas_pixels[src_y * doc_canvas_width + src_x];
#ifdef RAW_COPY
                            windowData.pixels[y * windowData.width + x] = pixel;
#else
                            // Extract RGBA components
                            u32 r = pixel & 0xFF;
                            u32 g = (pixel >> 8) & 0xFF;
                            u32 b = (pixel >> 16) & 0xFF;
                            u32 a = (pixel >> 24) & 0xFF;
                            // Repack as BGRA for X11
                            windowData.pixels[y * windowData.width + x] = (a << 24) | (r << 16) | (g << 8) | b;
#endif
                        }
                    }
                }
            }
        }

        // Present back buffer
        XPutImage(windowData.display, windowData.window, windowData.gc,
                  windowData.backBuffer, 0, 0, 0, 0,
                  windowData.width, windowData.height);
        XFlush(windowData.display);
        
        // Small sleep to prevent CPU spinning
        usleep(1000); // 1ms
    }
    
    // Cleanup
    Shutdown(user);
    
    // Note: XDestroyImage also frees the pixel data we provided
    windowData.backBuffer->data = NULL; // Prevent double free
    XDestroyImage(windowData.backBuffer);
    free(windowData.pixels);
    
    XFreeGC(windowData.display, windowData.gc);
    XDestroyWindow(windowData.display, windowData.window);
    XCloseDisplay(windowData.display);
    
    return 0;
}

// Platform function to get window size
void platform_get_window_size(u32* width, u32* height) {
    if (g_windowData && width && height) {
        *width = g_windowData->width;
        *height = g_windowData->height;
    }
}

// Platform clipboard implementation for Linux/X11
// These are simplified synchronous implementations for now
// A full async implementation would require handling X11 events in the main loop

void platform_clipboard_copy_text(u32_string* content, platform_clipboard_copy_text_callback callback, void* userData) {
    // For now, we'll implement a simplified version that works synchronously
    // A proper async implementation would need to integrate with the X11 event loop

    Display* display = XOpenDisplay(NULL);
    if (!display) {
        if (callback) callback(userData);
        return;
    }

    Window window = DefaultRootWindow(display);
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
    Atom targets = XInternAtom(display, "TARGETS", False);

    if (content && u32str_length(content) > 0) {
        // Convert u32_string to UTF-8
        u32 len = u32str_length(content);
        // Allocate buffer for UTF-8 (worst case is 4 bytes per character)
        char* utf8_text = (char*)malloc(len * 4 + 1);
        u32 utf8_len = 0;

        for (u32 i = 0; i < len; i++) {
            u32 ch = u32str_get(content, i);
            if (ch < 0x80) {
                utf8_text[utf8_len++] = (char)ch;
            } else if (ch < 0x800) {
                utf8_text[utf8_len++] = (char)(0xC0 | (ch >> 6));
                utf8_text[utf8_len++] = (char)(0x80 | (ch & 0x3F));
            } else if (ch < 0x10000) {
                utf8_text[utf8_len++] = (char)(0xE0 | (ch >> 12));
                utf8_text[utf8_len++] = (char)(0x80 | ((ch >> 6) & 0x3F));
                utf8_text[utf8_len++] = (char)(0x80 | (ch & 0x3F));
            } else {
                utf8_text[utf8_len++] = (char)(0xF0 | (ch >> 18));
                utf8_text[utf8_len++] = (char)(0x80 | ((ch >> 12) & 0x3F));
                utf8_text[utf8_len++] = (char)(0x80 | ((ch >> 6) & 0x3F));
                utf8_text[utf8_len++] = (char)(0x80 | (ch & 0x3F));
            }
        }
        utf8_text[utf8_len] = '\0';

        // Store in a property (simplified - proper implementation would handle selection requests)
        XStoreBytes(display, utf8_text, utf8_len);

        // Also try to set the clipboard selection
        XSetSelectionOwner(display, clipboard, window, CurrentTime);
        XSetSelectionOwner(display, XA_PRIMARY, window, CurrentTime);

        free(utf8_text);
    }

    XCloseDisplay(display);

    if (callback) callback(userData);
}

void platform_clipboard_paste_text(platform_clipboard_paste_text_callback callback, void* userData) {
    Display* display = XOpenDisplay(NULL);
    if (!display) {
        if (callback) callback(nullptr, userData);
        return;
    }

    // Try to get clipboard contents
    int nbytes = 0;
    char* data = XFetchBytes(display, &nbytes);

    u32_string* result = nullptr;

    if (data && nbytes > 0) {
        // Convert UTF-8 to u32_string
        // Count characters first
        u32 char_count = 0;
        for (int i = 0; i < nbytes; ) {
            unsigned char c = data[i];
            if (c < 0x80) {
                char_count++;
                i++;
            } else if ((c & 0xE0) == 0xC0) {
                char_count++;
                i += 2;
            } else if ((c & 0xF0) == 0xE0) {
                char_count++;
                i += 3;
            } else if ((c & 0xF8) == 0xF0) {
                char_count++;
                i += 4;
            } else {
                i++; // Skip invalid byte
            }
        }

        // Allocate u32 buffer
        u32* buffer = (u32*)malloc((char_count + 1) * sizeof(u32));
        u32 idx = 0;

        // Convert UTF-8 to u32
        for (int i = 0; i < nbytes && idx < char_count; ) {
            unsigned char c = data[i];
            if (c < 0x80) {
                buffer[idx++] = c;
                i++;
            } else if ((c & 0xE0) == 0xC0 && i + 1 < nbytes) {
                buffer[idx++] = ((c & 0x1F) << 6) | (data[i + 1] & 0x3F);
                i += 2;
            } else if ((c & 0xF0) == 0xE0 && i + 2 < nbytes) {
                buffer[idx++] = ((c & 0x0F) << 12) | ((data[i + 1] & 0x3F) << 6) | (data[i + 2] & 0x3F);
                i += 3;
            } else if ((c & 0xF8) == 0xF0 && i + 3 < nbytes) {
                buffer[idx++] = ((c & 0x07) << 18) | ((data[i + 1] & 0x3F) << 12) |
                               ((data[i + 2] & 0x3F) << 6) | (data[i + 3] & 0x3F);
                i += 4;
            } else {
                i++; // Skip invalid byte
            }
        }
        buffer[idx] = 0;

        result = u32str_init(buffer);
        free(buffer);
        XFree(data);
    }

    XCloseDisplay(display);

    if (callback) callback(result, userData);

    // Clean up the u32_string if it was created
    if (result) {
        u32str_destroy(result);
    }
}


void platform_exit() {
    if (g_windowData) {
        g_windowData->closeWindow = true;
    }
}

void platform_launch_browser(const char* url) {
    if (!url) return;

    // Use xdg-open on Linux, which is the standard way to open URLs
    // It will use the user's default browser
    char command[2048];
    snprintf(command, sizeof(command), "xdg-open '%s' 2>/dev/null &", url);

    // Use system() to execute the command
    // The & at the end runs it in background so we don't block
    system(command);
}

void platform_open_file(platform_open_file_callback callback, void* userData) {
    if (!callback) return;

    // Try kdialog first (KDE systems)
    FILE* fp = popen("kdialog --getopenfilename . 2>/dev/null", "r");

    if (!fp) {
        // Try zenity as fallback (GNOME/GTK systems)
        fp = popen("zenity --file-selection 2>/dev/null", "r");
    }

    if (!fp) {
        // No dialog tool available, call callback with null
        callback(nullptr, nullptr, 0, userData);
        return;
    }

    // Read the selected file path
    char filepath[4096];
    if (fgets(filepath, sizeof(filepath), fp) != nullptr) {
        // Remove trailing newline
        size_t len = strlen(filepath);
        if (len > 0 && filepath[len-1] == '\n') {
            filepath[len-1] = '\0';
            len--;
        }

        // Check if user cancelled (zenity returns empty, kdialog returns nothing)
        if (len > 0) {
            // Read the file
            FILE* file = fopen(filepath, "rb");
            if (file) {
                // Get file size
                fseek(file, 0, SEEK_END);
                long filesize = ftell(file);
                fseek(file, 0, SEEK_SET);

                if (filesize > 0) {
                    // Allocate buffer and read file
                    void* filedata = malloc(filesize);
                    if (filedata) {
                        size_t bytes_read = fread(filedata, 1, filesize, file);
                        fclose(file);

                        // Convert filepath to u32_string
                        u32* path_u32 = (u32*)malloc((len + 1) * sizeof(u32));
                        for (size_t i = 0; i <= len; i++) {
                            path_u32[i] = (u32)filepath[i];
                        }
                        u32_string* path_str = u32str_init(path_u32);
                        free(path_u32);

                        // Call callback with file data
                        callback(path_str, filedata, bytes_read, userData);

                        // Clean up
                        u32str_destroy(path_str);
                        free(filedata);
                    } else {
                        fclose(file);
                        callback(nullptr, nullptr, 0, userData);
                    }
                } else {
                    fclose(file);
                    // Empty file
                    callback(nullptr, nullptr, 0, userData);
                }
            } else {
                // Could not open file
                callback(nullptr, nullptr, 0, userData);
            }
        } else {
            // User cancelled
            callback(nullptr, nullptr, 0, userData);
        }
    } else {
        // No file selected
        callback(nullptr, nullptr, 0, userData);
    }

    pclose(fp);
}

void platform_modal_yesno(const char* message, platform_modal_yesno_callback callback, void* userData) {
    if (!callback) return;

    bool result = false;

    // Escape single quotes in the message for shell safety
    std::string safe_message;
    if (message) {
        for (const char* p = message; *p; p++) {
            if (*p == '\'') {
                safe_message += "'\\''";
            } else {
                safe_message += *p;
            }
        }
    }

    // Try kdialog first (KDE systems)
    char command[4096];
    snprintf(command, sizeof(command),
             "kdialog --yesno '%s' 2>/dev/null; echo $?",
             safe_message.c_str());

    FILE* fp = popen(command, "r");

    if (!fp) {
        // Try zenity as fallback (GNOME/GTK systems)
        snprintf(command, sizeof(command),
                 "zenity --question --text='%s' 2>/dev/null; echo $?",
                 safe_message.c_str());
        fp = popen(command, "r");
    }

    if (fp) {
        // Read the exit code
        char buffer[16];
        if (fgets(buffer, sizeof(buffer), fp) != nullptr) {
            int exit_code = atoi(buffer);
            // Exit code 0 means Yes/OK was clicked
            // Exit code 1 means No/Cancel was clicked
            result = (exit_code == 0);
        }
        pclose(fp);
    } else {
        // No dialog tool available, default to false
        result = false;
    }

    // Call the callback with the result and userData
    callback(result, userData);
}

void platform_save_file_as(void* fileData, u32 fileSizeBytes, platform_save_file_as_callback callback, void* userData) {
    if (!callback) return;

    // Try kdialog first (KDE systems)
    FILE* fp = popen("kdialog --getsavefilename . 2>/dev/null", "r");

    if (!fp) {
        // Try zenity as fallback (GNOME/GTK systems)
        fp = popen("zenity --file-selection --save --confirm-overwrite 2>/dev/null", "r");
    }

    if (!fp) {
        // No dialog tool available, call callback with null
        callback(nullptr, userData);
        return;
    }

    // Read the selected file path
    char filepath[4096];
    if (fgets(filepath, sizeof(filepath), fp) != nullptr) {
        // Remove trailing newline
        size_t len = strlen(filepath);
        if (len > 0 && filepath[len-1] == '\n') {
            filepath[len-1] = '\0';
            len--;
        }

        // Check if user cancelled (empty path)
        if (len > 0) {
            // Write the file
            FILE* file = fopen(filepath, "wb");
            if (file) {
                size_t bytes_written = 0;
                if (fileData && fileSizeBytes > 0) {
                    bytes_written = fwrite(fileData, 1, fileSizeBytes, file);
                }
                fclose(file);

                if (bytes_written == fileSizeBytes || fileSizeBytes == 0) {
                    // Success - convert filepath to u32_string
                    u32* path_u32 = (u32*)malloc((len + 1) * sizeof(u32));
                    for (size_t i = 0; i <= len; i++) {
                        path_u32[i] = (u32)(unsigned char)filepath[i];
                    }
                    u32_string* path_str = u32str_init(path_u32);
                    free(path_u32);

                    // Call callback with file path
                    callback(path_str, userData);

                    // Clean up
                    u32str_destroy(path_str);
                } else {
                    // Write failed
                    fprintf(stderr, "Failed to write all data to file: %s\n", filepath);
                    callback(nullptr, userData);
                }
            } else {
                // Could not open file for writing
                fprintf(stderr, "Failed to open file for writing: %s\n", filepath);
                callback(nullptr, userData);
            }
        } else {
            // User cancelled
            callback(nullptr, userData);
        }
    } else {
        // No file selected or error reading
        callback(nullptr, userData);
    }

    pclose(fp);
}

void platform_write_file(u32_string* filePath, void* fileData, u32 fileSizeBytes, platform_write_file_callback callback, void* userData) {
    if (!callback) return;

    bool success = false;

    if (filePath) {
        // Convert u32_string path to char*
        u32 path_len = u32str_length(filePath);
        char* filepath = (char*)malloc(path_len + 1);
        for (u32 i = 0; i < path_len; i++) {
            filepath[i] = (char)u32str_get(filePath, i);
        }
        filepath[path_len] = '\0';

        // Write the file
        FILE* file = fopen(filepath, "wb");
        if (file) {
            size_t bytes_written = 0;
            if (fileData && fileSizeBytes > 0) {
                bytes_written = fwrite(fileData, 1, fileSizeBytes, file);
            }
            fclose(file);

            if (bytes_written == fileSizeBytes || fileSizeBytes == 0) {
                success = true;
            } else {
                fprintf(stderr, "Failed to write all data to file: %s (wrote %zu of %u bytes)\n",
                        filepath, bytes_written, fileSizeBytes);
            }
        } else {
            fprintf(stderr, "Failed to open file for writing: %s\n", filepath);
        }

        free(filepath);
    }

    // Call callback with result
    callback(success, userData);
}
