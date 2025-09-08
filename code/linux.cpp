#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include "renderer.h"
#include "strings.h"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define WINDOW_TITLE "Double Buffered X11 Sample"

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
    
    // Create window
    Window rootWindow = DefaultRootWindow(windowData.display);
    
    XSetWindowAttributes windowAttributes;
    windowAttributes.background_pixel = BlackPixel(windowData.display, windowData.screen);
    windowAttributes.border_pixel = BlackPixel(windowData.display, windowData.screen);
    windowAttributes.backing_store = Always;
    windowAttributes.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;
    
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
    Atom wmDeleteMessage = XInternAtom(windowData.display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(windowData.display, windowData.window, &wmDeleteMessage, 1);
    
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
                        
                        // Reallocate back buffer
                        free(windowData.pixels);
                        windowData.pixels = (unsigned int*)malloc(windowData.width * windowData.height * sizeof(unsigned int));
                        
                        // Recreate XImage
                        XDestroyImage(windowData.backBuffer);
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
    return user;
}

void Update(void* userData, float deltaTime) {
    UserData* user = (UserData*)userData;
    user->offset += deltaTime * 100.0f; // Move 100 pixels per second
    while (user->offset > 400.0f) {
        user->offset -= 400.0f;
    }
    user->frameCount++;
}

void Render(void* userData, WindowData& windowData) {
    UserData* user = (UserData*)userData;
    
    // Clear canvas to dark gray
    canvas_clear(user->cnvs, 30, 30, 30);
    
    // Draw several colored squares
    // Red square
    canvas_draw_rect(user->cnvs, 50, 50, 100, 100, 255, 68, 68);
    
    // Green square
    canvas_draw_rect(user->cnvs, 200, 50, 100, 100, 68, 255, 68);
    
    // Blue square
    canvas_draw_rect(user->cnvs, 350, 50, 100, 100, 68, 68, 255);
    
    // Yellow square
    canvas_draw_rect(user->cnvs, 500, 50, 100, 100, 255, 255, 68);
    
    // Moving purple square
    int movingX = (int)(user->offset * 2) % (windowData.width - 80);
    canvas_draw_rect(user->cnvs, movingX, 250, 80, 80, 200, 68, 255);
    
    // Draw text strings
    u32 hello_data[] = {'h', 'e', 'l', 'l', 'o', '\t', 'l', 'a', 'n', 'd', 0};
    u32_string* hello_str = u32str_init(hello_data);
    canvas_draw_text(user->cnvs, user->fnt, hello_str, 50, 400, 255, 255, 255);
    u32str_destroy(hello_str);
    
    u32 world_data[] = {'h', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r', 'l', 'd', 0};
    u32_string* world_str = u32str_init(world_data);
    canvas_draw_text(user->cnvs, user->fnt, world_str, 50, 450, 255, 200, 100);
    u32str_destroy(world_str);
    
    // Draw animated text
    u32 anim_data[] = {'M', 'o', 'v', 'i', 'n', 'g', '!', 0};
    u32_string* anim_str = u32str_init(anim_data);
    canvas_draw_text(user->cnvs, user->fnt, anim_str, movingX, 350, 100, 255, 200);
    u32str_destroy(anim_str);
    
    // Copy canvas pixels to window buffer
    // The canvas struct has pixels as its first member, so we can access it by dereferencing
    // after casting to u32**
    u32* canvas_pixels = *((u32**)user->cnvs);
    memcpy(windowData.pixels, canvas_pixels, windowData.width * windowData.height * sizeof(u32));
}

void Shutdown(void* userData) {
    UserData* user = (UserData*)userData;
    canvas_destroy(user->cnvs);
    font_destroy(user->fnt);
    delete user;
}