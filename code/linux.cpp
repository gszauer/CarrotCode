#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xos.h>
#include <X11/Xatom.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

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
    
    // Clear to dark gray background
    unsigned int bgColor = 0xFF1E1E1E; // ARGB format
    for (int i = 0; i < windowData.width * windowData.height; i++) {
        windowData.pixels[i] = bgColor;
    }
    
    // Draw checker pattern (128x128 tiles)
    const int tileSize = 128;
    unsigned int color1 = 0xFF404040; // Dark gray
    unsigned int color2 = 0xFF606060; // Light gray
    
    // Add animated offset to make it more interesting
    int offsetX = (int)user->offset;
    
    for (int y = 0; y < windowData.height; y++) {
        for (int x = 0; x < windowData.width; x++) {
            int tileX = (x + offsetX) / tileSize;
            int tileY = y / tileSize;
            
            // Checker pattern logic
            bool isLight = (tileX + tileY) % 2 == 0;
            unsigned int color = isLight ? color2 : color1;
            
            windowData.pixels[y * windowData.width + x] = color;
        }
    }
    
    // Draw a moving red square to show animation
    int squareSize = 100;
    int squareX = (int)(user->offset * 2) % (windowData.width - squareSize);
    int squareY = windowData.height / 2 - squareSize / 2;
    unsigned int squareColor = 0xFFFF4444; // Red
    
    for (int y = squareY; y < squareY + squareSize && y < windowData.height; y++) {
        for (int x = squareX; x < squareX + squareSize && x < windowData.width; x++) {
            if (x >= 0 && y >= 0) {
                windowData.pixels[y * windowData.width + x] = squareColor;
            }
        }
    }
    
    // Draw frame counter in top left (simple block representation)
    int counterX = 10;
    int counterY = 10;
    int blockSize = 5;
    int digitWidth = 20;
    unsigned int counterColor = 0xFFFFFFFF; // White
    
    // Simple visual frame counter (shows blocks for every 10 frames)
    int blocks = (user->frameCount / 10) % 20;
    for (int i = 0; i < blocks; i++) {
        for (int y = counterY; y < counterY + blockSize && y < windowData.height; y++) {
            for (int x = counterX + i * (blockSize + 2); x < counterX + i * (blockSize + 2) + blockSize && x < windowData.width; x++) {
                windowData.pixels[y * windowData.width + x] = counterColor;
            }
        }
    }
}

void Shutdown(void* userData) {
    UserData* user = (UserData*)userData;
    delete user;
}