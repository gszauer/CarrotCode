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

// Global window data for platform_exit
static WindowData* g_windowData = nullptr;

// Clipboard state shared with the X11 event loop
static u8_string* g_clipboardText = nullptr;

struct ClipboardRequestState {
    bool active = false;
    bool triedStringFallback = false;
    platform_clipboard_paste_text_callback callback = nullptr;
    void* userData = nullptr;
    Atom selection = None;
    Atom target = None;
    Atom property = None;
};

static ClipboardRequestState g_clipboardRequest;

u64 platform_get_milliseconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (u64)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static PlatformKey TranslateKeySym(KeySym keysym) {
    switch (keysym) {
        case XK_BackSpace:
#ifdef XK_KP_BackSpace
        case XK_KP_BackSpace:
#endif
            return PlatformKey::Backspace;
        case XK_Tab:
        case XK_ISO_Left_Tab:
            return PlatformKey::Tab;
        case XK_Return:
        case XK_KP_Enter:
            return PlatformKey::Return;
        case XK_Delete:
        case XK_KP_Delete:
            return PlatformKey::Delete;
        case XK_Left:
        case XK_KP_Left:
            return PlatformKey::Left;
        case XK_Right:
        case XK_KP_Right:
            return PlatformKey::Right;
        case XK_Up:
        case XK_KP_Up:
            return PlatformKey::Up;
        case XK_Down:
        case XK_KP_Down:
            return PlatformKey::Down;
        case XK_Home:
        case XK_KP_Home:
            return PlatformKey::Home;
        case XK_End:
        case XK_KP_End:
            return PlatformKey::End;
        case XK_Escape:
            return PlatformKey::Escape;
        default:
            break;
    }

    if ((keysym >= XK_a && keysym <= XK_z) || (keysym >= XK_A && keysym <= XK_Z)) {
        int index = 0;
        if (keysym >= XK_a && keysym <= XK_z) {
            index = static_cast<int>(keysym - XK_a);
        } else {
            index = static_cast<int>(keysym - XK_A);
        }
        return static_cast<PlatformKey>(static_cast<u32>(PlatformKey::KeyA) + index);
    }

    return PlatformKey::Unknown;
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
    long long lastTime = (long long)platform_get_milliseconds();
    Window xdndSourceWindow = None;  // Track the drag source window
    bool leftButtonDown = false;
    bool middleButtonDown = false;
    bool rightButtonDown = false;
    
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

                case SelectionRequest: {
                    XSelectionRequestEvent* req = &event.xselectionrequest;
                    XSelectionEvent respond = {};
                    respond.type = SelectionNotify;
                    respond.display = req->display;
                    respond.requestor = req->requestor;
                    respond.selection = req->selection;
                    respond.target = req->target;
                    respond.time = req->time;
                    respond.property = None;

                    Atom clipboardAtom = XInternAtom(windowData.display, "CLIPBOARD", False);
                    Atom targetsAtom = XInternAtom(windowData.display, "TARGETS", False);
                    Atom utf8Atom = XInternAtom(windowData.display, "UTF8_STRING", False);
                    Atom textAtom = XInternAtom(windowData.display, "TEXT", False);
                    Atom textPlainAtom = XInternAtom(windowData.display, "text/plain", False);
                    Atom textPlainUtf8Atom = XInternAtom(windowData.display, "text/plain;charset=utf-8", False);

                    Atom propertyAtom = (req->property == None) ? req->target : req->property;
                    bool handled = false;
                    bool ownsSelection = (req->selection == clipboardAtom || req->selection == XA_PRIMARY) &&
                        (XGetSelectionOwner(windowData.display, req->selection) == windowData.window);

                    if (ownsSelection) {
                        u8* utf8Bytes = g_clipboardText ? u8str_getBuffer(g_clipboardText) : nullptr;
                        u32 utf8Length = g_clipboardText ? u8str_size_bytes(g_clipboardText) : 0;
                        if (!utf8Bytes) {
                            utf8Length = 0;
                        }
                        if (req->target == targetsAtom) {
                            Atom supported[] = { utf8Atom, textPlainUtf8Atom, textAtom, textPlainAtom, XA_STRING };
                            XChangeProperty(windowData.display, req->requestor, propertyAtom, XA_ATOM, 32,
                                            PropModeReplace,
                                            reinterpret_cast<const unsigned char*>(supported),
                                            static_cast<int>(sizeof(supported) / sizeof(Atom)));
                            handled = true;
                        } else if (req->target == utf8Atom || req->target == textPlainUtf8Atom) {
                            XChangeProperty(windowData.display, req->requestor, propertyAtom, utf8Atom, 8,
                                            PropModeReplace,
                                            reinterpret_cast<const unsigned char*>(utf8Bytes),
                                            static_cast<int>(utf8Length));
                            handled = true;
                        } else if (req->target == textAtom || req->target == textPlainAtom || req->target == XA_STRING) {
                            std::string ascii;
                            if (utf8Bytes && utf8Length > 0) {
                                ascii.reserve(utf8Length);
                                for (u32 i = 0; i < utf8Length; i++) {
                                    unsigned char ch = utf8Bytes[i];
                                    ascii.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
                                }
                            }
                            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(ascii.c_str());
                            unsigned long length = ascii.size();
                            XChangeProperty(windowData.display, req->requestor, propertyAtom, XA_STRING, 8,
                                            PropModeReplace, bytes, static_cast<int>(length));
                            handled = true;
                        }
                    }

                    if (handled) {
                        respond.property = propertyAtom;
                    }

                    XSendEvent(windowData.display, req->requestor, False, 0,
                               reinterpret_cast<XEvent*>(&respond));
                    XFlush(windowData.display);
                    break;
                }

                case SelectionNotify:
                    if (event.xselection.property == XdndSelectionProperty) {
                        Atom actual_type;
                        int actual_format;
                        unsigned long nitems, bytes_after;
                        unsigned char* data = nullptr;

                        if (XGetWindowProperty(windowData.display, windowData.window,
                                             XdndSelectionProperty, 0, ~0L, True,
                                             AnyPropertyType, &actual_type, &actual_format,
                                             &nitems, &bytes_after, &data) == Success) {
                            if (data && nitems > 0) {
                                // Parse URI list (file://path format)
                                std::string uri((char*)data, nitems);

                                // Process all files in the drop (they're separated by newlines)
                                size_t pos = 0;
                                while (pos < uri.length()) {
                                    size_t file_start = uri.find("file://", pos);
                                    if (file_start == std::string::npos) {
                                        break;
                                    }

                                    // Extract this URI up to the next newline or end of string
                                    size_t uri_end = uri.find_first_of("\r\n", file_start);
                                    std::string file_uri;
                                    if (uri_end != std::string::npos) {
                                        file_uri = uri.substr(file_start, uri_end - file_start);
                                        pos = uri_end + 1;
                                    } else {
                                        file_uri = uri.substr(file_start);
                                        pos = uri.length();
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

                                            std::vector<u32> u32content;
                                            for (char c : content) {
                                                u32content.push_back((u32)(unsigned char)c);
                                            }
                                            u32content.push_back(0);

                                            u32_string* file_str = u32str_init(u32content.data());
                                            document* new_doc = doc_from_str32(file_str, 100);
                                            u32str_destroy(file_str);

                                            // Add document view
                                            AddDocumentView(user, new_doc, filepath.c_str());
                                        }
                                    }
                                }
                                XFree(data);
                            }
                        }

                        if (xdndSourceWindow != None) {
                            XEvent reply;
                            memset(&reply, 0, sizeof(reply));
                            reply.type = ClientMessage;
                            reply.xclient.window = xdndSourceWindow;
                            reply.xclient.message_type = XdndFinished;
                            reply.xclient.format = 32;
                            reply.xclient.data.l[0] = windowData.window;
                            reply.xclient.data.l[1] = 1;
                            reply.xclient.data.l[2] = XdndActionCopy;
                            XSendEvent(windowData.display, xdndSourceWindow, False, NoEventMask, &reply);
                            XFlush(windowData.display);
                        }

                        XDeleteProperty(windowData.display, windowData.window, XdndSelectionProperty);
                        xdndSourceWindow = None;
                    }
                    else if (g_clipboardRequest.active &&
                             event.xselection.selection == g_clipboardRequest.selection) {
                        if (event.xselection.property == None) {
                            if (!g_clipboardRequest.triedStringFallback && g_clipboardRequest.target != XA_STRING) {
                                g_clipboardRequest.triedStringFallback = true;
                                g_clipboardRequest.target = XA_STRING;
                                XConvertSelection(windowData.display, g_clipboardRequest.selection, XA_STRING,
                                                  g_clipboardRequest.property, windowData.window,
                                                  event.xselection.time ? event.xselection.time : CurrentTime);
                                XFlush(windowData.display);
                            } else {
                                if (g_clipboardRequest.callback) {
                                    g_clipboardRequest.callback(nullptr, g_clipboardRequest.userData);
                                }
                                g_clipboardRequest = ClipboardRequestState();
                            }
                            break;
                        }

                        Atom actual_type;
                        int actual_format;
                        unsigned long nitems, bytes_after;
                        unsigned char* data = nullptr;

                        u32_string* result = nullptr;
                        if (XGetWindowProperty(windowData.display, windowData.window,
                                               g_clipboardRequest.property, 0, ~0L, True,
                                               AnyPropertyType, &actual_type, &actual_format,
                                               &nitems, &bytes_after, &data) == Success) {
                            if (data && nitems > 0) {
                                u8* buffer = (u8*)malloc(nitems + 1);
                                if (buffer) {
                                    memcpy(buffer, data, nitems);
                                    buffer[nitems] = 0;
                                    u8_string* temp = u8str_init(buffer);
                                    if (temp) {
                                        result = u8str_to_u32str(temp);
                                        u8str_destroy(temp);
                                    }
                                    free(buffer);
                                }
                            }
                            if (!result) {
                                result = u32str_create();
                            }
                        }

                        if (data) {
                            XFree(data);
                        }

                        if (g_clipboardRequest.callback) {
                            g_clipboardRequest.callback(result, g_clipboardRequest.userData);
                        }
                        if (result) {
                            u32str_destroy(result);
                        }

                        g_clipboardRequest = ClipboardRequestState();
                    }
                    break;
                    
                case KeyPress:
                case KeyRelease:
                    {
                        bool isKeyDown = (event.type == KeyPress);

                        char buffer[32];
                        KeySym keysym;
                        int len = XLookupString(&event.xkey, buffer, sizeof(buffer), &keysym, nullptr);

                        PlatformKey platformKey = TranslateKeySym(keysym);
                        u32 characterCode = 0;
                        if (isKeyDown && len > 0) {
                            characterCode = static_cast<unsigned char>(buffer[0]);
                        }

                        bool altDown = (event.xkey.state & Mod1Mask) != 0;
                        bool ctrlDown = (event.xkey.state & ControlMask) != 0;
                        bool shiftDown = (event.xkey.state & ShiftMask) != 0;

                        ApplicationHandleKeyboard(user, characterCode, platformKey,
                                                  static_cast<u32>(keysym), isKeyDown,
                                                  altDown, ctrlDown, shiftDown);

                        if (user->should_quit) {
                            windowData.closeWindow = true;
                            user->should_quit = false;
                        }
                    }
                    break;

                case ButtonPress:
                case ButtonRelease:
                case MotionNotify:
                    {
                        float scale = 1.0f;
                        if (user->zoom_level == 0) scale = 0.5f;
                        else if (user->zoom_level == 1) scale = 1.0f;
                        else if (user->zoom_level == 2) scale = 2.0f;

                        int rawX = (event.type == MotionNotify) ? event.xmotion.x : event.xbutton.x;
                        int rawY = (event.type == MotionNotify) ? event.xmotion.y : event.xbutton.y;

                        u32 mouseX = static_cast<u32>(rawX / scale);
                        u32 mouseY = static_cast<u32>(rawY / scale);
                        f32 normX = static_cast<f32>(mouseX) / windowData.width;
                        f32 normY = static_cast<f32>(mouseY) / windowData.height;

                        ApplicationMouseEvent evt{};
                        evt.x = mouseX;
                        evt.y = mouseY;
                        evt.normX = normX;
                        evt.normY = normY;
                        evt.scrollDelta = 0.0f;
                        evt.button = ApplicationMouseButton::NoneButton;

                        if (event.type == MotionNotify) {
                            evt.type = ApplicationMouseEventType::Move;
                            leftButtonDown = (event.xmotion.state & Button1Mask) != 0;
                            middleButtonDown = (event.xmotion.state & Button2Mask) != 0;
                            rightButtonDown = (event.xmotion.state & Button3Mask) != 0;
                            evt.shiftDown = (event.xmotion.state & ShiftMask) != 0;
                        } else {
                            bool isPressed = (event.type == ButtonPress);
                            evt.type = isPressed ? ApplicationMouseEventType::Press
                                                 : ApplicationMouseEventType::Release;
                            evt.shiftDown = (event.xbutton.state & ShiftMask) != 0;

                            switch (event.xbutton.button) {
                                case Button1:
                                    leftButtonDown = isPressed;
                                    evt.button = ApplicationMouseButton::Left;
                                    break;
                                case Button2:
                                    middleButtonDown = isPressed;
                                    evt.button = ApplicationMouseButton::Middle;
                                    break;
                                case Button3:
                                    rightButtonDown = isPressed;
                                    evt.button = ApplicationMouseButton::Right;
                                    break;
                                case Button4:
                                    if (isPressed) {
                                        evt.scrollDelta = 1.0f;
                                    }
                                    break;
                                case Button5:
                                    if (isPressed) {
                                        evt.scrollDelta = -1.0f;
                                    }
                                    break;
                                default:
                                    break;
                            }
                        }

                        evt.leftDown = leftButtonDown;
                        evt.middleDown = middleButtonDown;
                        evt.rightDown = rightButtonDown;

                        ApplicationHandleMouse(user, evt);
                    }
                    break;
            }
        }
        
        // Update and render
        long long currentTime = (long long)platform_get_milliseconds();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;
        
        Update(user, deltaTime);

        {
            // Clear the window buffer first
            // The application re-draws everything, skip as optimization
            //memset(windowData.pixels, 0, windowData.width * windowData.height * sizeof(u32));

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
#ifdef CARROT_LINUX_RAW_COPY
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
#ifdef CARROT_LINUX_RAW_COPY
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

    if (g_clipboardText) {
        u8str_destroy(g_clipboardText);
        g_clipboardText = nullptr;
    }
    g_clipboardRequest = ClipboardRequestState();
    
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
    if (!g_windowData || !g_windowData->display) {
        if (callback) callback(userData);
        return;
    }

    Display* display = g_windowData->display;
    Window window = g_windowData->window;

    if (g_clipboardText) {
        u8str_destroy(g_clipboardText);
        g_clipboardText = nullptr;
    }

    bool hasContent = content && u32str_length(content) > 0;
    if (hasContent) {
        g_clipboardText = u32str_to_u8str(content);
    } else {
        g_clipboardText = u8str_create();
        if (g_clipboardText) {
            u8str_reserve(g_clipboardText, 0);
        }
    }

    if (!g_clipboardText) {
        g_clipboardText = u8str_create();
        if (g_clipboardText) {
            u8str_reserve(g_clipboardText, 0);
        }
    }

    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);

    XSetSelectionOwner(display, clipboard, window, CurrentTime);
    XSetSelectionOwner(display, XA_PRIMARY, window, CurrentTime);
    XFlush(display);

    if (callback) callback(userData);
}

void platform_clipboard_paste_text(platform_clipboard_paste_text_callback callback, void* userData) {
    if (!g_windowData || !g_windowData->display) {
        if (callback) callback(nullptr, userData);
        return;
    }

    Display* display = g_windowData->display;
    Window window = g_windowData->window;

    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Window owner = XGetSelectionOwner(display, clipboard);

    if (owner == window) {
        u32_string* direct = nullptr;
        if (g_clipboardText) {
            direct = u8str_to_u32str(g_clipboardText);
        }
        if (!direct) {
            direct = u32str_create();
        }

        if (callback) callback(direct, userData);
        if (direct) u32str_destroy(direct);
        return;
    }

    if (g_clipboardRequest.active) {
        if (callback) callback(nullptr, userData);
        return;
    }

    Atom selection = clipboard;
    if (owner == None) {
        Window primaryOwner = XGetSelectionOwner(display, XA_PRIMARY);
        if (primaryOwner == None) {
            if (callback) callback(nullptr, userData);
            return;
        }
        selection = XA_PRIMARY;
    }

    Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
    Atom property = XInternAtom(display, "CARROT_CLIP_TEMP", False);

    g_clipboardRequest.active = true;
    g_clipboardRequest.triedStringFallback = false;
    g_clipboardRequest.callback = callback;
    g_clipboardRequest.userData = userData;
    g_clipboardRequest.selection = selection;
    g_clipboardRequest.target = utf8;
    g_clipboardRequest.property = property;

    XConvertSelection(display, selection, utf8, property, window, CurrentTime);
    XFlush(display);
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
