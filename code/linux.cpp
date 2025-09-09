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

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define WINDOW_TITLE "Code Viewer - Drop File to Preview"

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

struct UserData {
    float offset;
    int frameCount;
    canvas* cnvs;
    font* fnt;
    document* doc;
    canvas* doc_canvas;
    bool has_document;
};

void* Initialize(const WindowData& windowData);
void Update(void* userData, float deltaTime);
void Render(void* userData, WindowData& windowData);
void Shutdown(void* userData);

long long GetTimeInMilliseconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int main(int argc, char** argv) {
    WindowData windowData = {};
    
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
    windowAttributes.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask | PropertyChangeMask;
    
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
    void* userData = Initialize(windowData);
    
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
                        
                        // Recreate canvas with new size
                        UserData* user = (UserData*)userData;
                        canvas_destroy(user->cnvs);
                        user->cnvs = canvas_create(windowData.width, windowData.height);
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
                        printf("DnD Enter received from window %lu\n", xdndSourceWindow);
                    }
                    else if (event.xclient.message_type == XdndPosition) {
                        // Send XdndStatus
                        Window source = event.xclient.data.l[0];
                        printf("DnD Position received from window %lu\n", source);
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
                        printf("DnD Leave received\n");
                        XDeleteProperty(windowData.display, windowData.window, XdndSelectionProperty);
                        xdndSourceWindow = None;
                    }
                    else if (event.xclient.message_type == XdndDrop) {
                        // Request the selection
                        xdndSourceWindow = event.xclient.data.l[0];
                        Time timestamp = (Time)event.xclient.data.l[2];
                        printf("DnD Drop received from window %lu, timestamp %lu\n", xdndSourceWindow, timestamp);
                        
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
                    printf("SelectionNotify received\n");
                    printf("Selection property: %ld, Expected: %ld\n", 
                           event.xselection.property, XdndSelectionProperty);
                    if (event.xselection.property == XdndSelectionProperty) {
                        Atom actual_type;
                        int actual_format;
                        unsigned long nitems, bytes_after;
                        unsigned char* data = nullptr;
                        
                        if (XGetWindowProperty(windowData.display, windowData.window,
                                             XdndSelectionProperty, 0, ~0L, True,  // Delete after reading
                                             AnyPropertyType, &actual_type, &actual_format,
                                             &nitems, &bytes_after, &data) == Success) {
                            
                            printf("Got property data, nitems=%lu, format=%d\n", nitems, actual_format);
                            if (data && nitems > 0) {
                                // Parse URI list (file://path format)
                                std::string uri((char*)data, nitems);
                                printf("Full URI data: '%s'\n", uri.c_str());
                                // URI list may contain multiple files, take the first one
                                size_t file_start = uri.find("file://");
                                if (file_start != std::string::npos) {
                                    std::string file_uri = uri.substr(file_start);
                                    
                                    // Find the end of this URI (newline or end of string)
                                    size_t uri_end = file_uri.find_first_of("\r\n");
                                    if (uri_end != std::string::npos) {
                                        file_uri = file_uri.substr(0, uri_end);
                                    }
                                    
                                    // Extract path from file:// URI
                                    if (file_uri.substr(0, 7) == "file://") {
                                        std::string filepath = file_uri.substr(7);
                                        
                                        // URL decode the path (handle %20 for spaces, etc)
                                        size_t pos = 0;
                                        while ((pos = filepath.find('%', pos)) != std::string::npos) {
                                            if (pos + 2 < filepath.length()) {
                                                std::string hex = filepath.substr(pos + 1, 2);
                                                char ch = (char)std::stoi(hex, nullptr, 16);
                                                filepath.replace(pos, 3, 1, ch);
                                            }
                                            pos++;
                                        }
                                        
                                        printf("Decoded file path: %s\n", filepath.c_str());
                                        
                                        // Read file and create document
                                        std::ifstream file(filepath, std::ios::binary);
                                        if (file.is_open()) {
                                        printf("File opened successfully\n");
                                        std::string content((std::istreambuf_iterator<char>(file)),
                                                          std::istreambuf_iterator<char>());
                                        file.close();
                                        printf("File size: %zu bytes\n", content.size());
                                        
                                        // Convert to u32_string
                                        std::vector<u32> u32content;
                                        for (char c : content) {
                                            u32content.push_back((u32)(unsigned char)c);
                                        }
                                        u32content.push_back(0);
                                        
                                        u32_string* file_str = u32str_init(u32content.data());
                                        
                                        // Create document
                                        UserData* user = (UserData*)userData;
                                        if (user->doc) {
                                            doc_destroy(user->doc);
                                        }
                                        user->doc = doc_from_str32(file_str, 100);
                                        u32str_destroy(file_str);
                                        printf("Document created with %u lines\n", doc_line_count(user->doc));
                                        
                                        // Check file extension to determine if syntax highlighting should be applied
                                        bool should_highlight = false;
                                        size_t dot_pos = filepath.rfind('.');
                                        if (dot_pos != std::string::npos) {
                                            std::string extension = filepath.substr(dot_pos);
                                            // Convert to lowercase for case-insensitive comparison
                                            for (char& c : extension) {
                                                c = std::tolower(c);
                                            }
                                            // Check if it's a supported extension
                                            if (extension == ".c" || extension == ".h" || 
                                                extension == ".cpp" || extension == ".inl" || 
                                                extension == ".js" || extension == ".ts") {
                                                should_highlight = true;
                                            }
                                        }
                                        printf("Syntax highlighting: %s\n", should_highlight ? "enabled" : "disabled");
                                        
                                        // Create debug canvas
                                        if (user->doc_canvas) {
                                            canvas_destroy(user->doc_canvas);
                                        }
                                        user->doc_canvas = canvas_debug_doc(user->doc, user->fnt, should_highlight);
                                        user->has_document = (user->doc_canvas != nullptr);
                                        printf("Canvas created: %s\n", user->has_document ? "yes" : "no");
                                        
                                        // Resize window if canvas was created
                                        if (user->doc_canvas) {
                                            u32 canvas_width = canvas_get_width(user->doc_canvas);
                                            u32 canvas_height = canvas_get_height(user->doc_canvas);
                                            printf("Canvas dimensions: %ux%u\n", canvas_width, canvas_height);
                                            
                                            // Limit canvas size to reasonable maximum
                                            const u32 MAX_WIDTH = 1920;
                                            const u32 MAX_HEIGHT = 1080 * 5;
                                            if (canvas_width > MAX_WIDTH) canvas_width = MAX_WIDTH;
                                            if (canvas_height > MAX_HEIGHT) canvas_height = MAX_HEIGHT;
                                            
                                            XResizeWindow(windowData.display, windowData.window,
                                                        canvas_width, canvas_height);
                                            XFlush(windowData.display);
                                        }
                                        } else {
                                            printf("Failed to open file: %s\n", filepath.c_str());
                                        }
                                    }  // end if (file_uri.substr(0, 7) == "file://")
                                }  // end if (file_start != std::string::npos)
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
                            printf("Sent XdndFinished to window %lu\n", xdndSourceWindow);
                        }
                        
                        // Clear the property and source window
                        XDeleteProperty(windowData.display, windowData.window, XdndSelectionProperty);
                        xdndSourceWindow = None;
                    }
                    break;
                    
                case KeyPress:
                    // Exit on Escape key
                    if (XLookupKeysym(&event.xkey, 0) == XK_Escape) {
                        windowData.closeWindow = true;
                    }
                    break;
            }
        }
        
        // Update and render
        long long currentTime = GetTimeInMilliseconds();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;
        
        Update(userData, deltaTime);
        Render(userData, windowData);
        
        // Present back buffer
        XPutImage(windowData.display, windowData.window, windowData.gc,
                  windowData.backBuffer, 0, 0, 0, 0,
                  windowData.width, windowData.height);
        XFlush(windowData.display);
        
        // Small sleep to prevent CPU spinning
        usleep(1000); // 1ms
    }
    
    // Cleanup
    Shutdown(userData);
    
    // Note: XDestroyImage also frees the pixel data we provided
    windowData.backBuffer->data = NULL; // Prevent double free
    XDestroyImage(windowData.backBuffer);
    free(windowData.pixels);
    
    XFreeGC(windowData.display, windowData.gc);
    XDestroyWindow(windowData.display, windowData.window);
    XCloseDisplay(windowData.display);
    
    return 0;
}

void* Initialize(const WindowData& windowData) {
    UserData* user = new UserData();
    user->offset = 0.0f;
    user->frameCount = 0;
    user->cnvs = canvas_create(windowData.width, windowData.height);
    user->fnt = font_create(nullptr, 0, 32); // Using bitmap font
    user->doc = nullptr;
    user->doc_canvas = nullptr;
    user->has_document = false;
    return user;
}

void Update(void* userData, float deltaTime) {
    UserData* user = (UserData*)userData;
    // No animation needed for document viewer
    user->frameCount++;
}

void Render(void* userData, WindowData& windowData) {
    UserData* user = (UserData*)userData;
    
    if (user->has_document && user->doc_canvas) {
        // Display the document canvas
        u32* doc_canvas_pixels = canvas_get_raw_pixels(user->doc_canvas);
        u32 doc_canvas_width = canvas_get_width(user->doc_canvas);
        u32 doc_canvas_height = canvas_get_height(user->doc_canvas);
        
        // Clear the window buffer first
        memset(windowData.pixels, 0, windowData.width * windowData.height * sizeof(u32));
        
        // Safety check
        if (doc_canvas_pixels && doc_canvas_width > 0 && doc_canvas_height > 0) {
            // Copy what fits into the window buffer
            u32 copy_width = (doc_canvas_width < (u32)windowData.width) ? doc_canvas_width : windowData.width;
            u32 copy_height = (doc_canvas_height < (u32)windowData.height) ? doc_canvas_height : windowData.height;
            
            for (u32 y = 0; y < copy_height; y++) {
                memcpy(windowData.pixels + y * windowData.width,
                       doc_canvas_pixels + y * doc_canvas_width,
                       copy_width * sizeof(u32));
            }
        }
    } else {
        // Clear canvas to dark gray
        canvas_clear(user->cnvs, 40, 40, 50);
        
        // Draw a rectangle behind the text
        u32 rect_width = 400;
        u32 rect_height = 60;
        u32 rect_x = (windowData.width - rect_width) / 2;
        u32 rect_y = (windowData.height - rect_height) / 2;
        canvas_draw_rect(user->cnvs, rect_x, rect_y, rect_width, rect_height, 60, 60, 80);
        
        // Draw "Drop file here to preview" text
        u32 drop_text[] = {'D', 'r', 'o', 'p', ' ', 'f', 'i', 'l', 'e', ' ',
                          'h', 'e', 'r', 'e', ' ', 't', 'o', ' ',
                          'p', 'r', 'e', 'v', 'i', 'e', 'w', 0};
        u32_string* drop_str = u32str_init(drop_text);
        
        // Center the text
        u32 text_width = font_get_width(user->fnt, drop_str, 0);
        u32 text_x = (windowData.width - text_width) / 2;
        u32 text_y = rect_y + (rect_height - font_get_line_height(user->fnt)) / 2;
        
        canvas_draw_text(user->cnvs, user->fnt, drop_str, text_x, text_y, 200, 200, 220);
        u32str_destroy(drop_str);
        
        // Copy canvas pixels to window buffer
        u32* canvas_pixels = canvas_get_raw_pixels(user->cnvs);
        memcpy(windowData.pixels, canvas_pixels, windowData.width * windowData.height * sizeof(u32));
    }
}

void Shutdown(void* userData) {
    UserData* user = (UserData*)userData;
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