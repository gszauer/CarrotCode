// SDL2Window.cpp
#define SDL_MAIN_HANDLED  // Tell SDL we'll handle main/WinMain ourselves
#include "sdl2/include/SDL.h"
#include "sdl2/include/SDL_syswm.h"
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/fetch.h>
#endif

#include "Renderer.h"

#ifdef _WIN32
#include "glad.h"
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "SDL2.lib")
#pragma comment(lib, "OpenGL32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#endif

#include "application.h"

// Global variables
SDL_Window* g_window = nullptr;
SDL_GLContext g_glContext = nullptr;
bool g_running = true;
bool g_isMaximized = false;
bool g_isDragging = false;
SDL_Point g_dragStartPoint;
SDL_Rect g_windowRectBeforeMaximize;
int g_windowID = 0;
Uint64 g_lastTime = 0;
float g_deltaTime = 0.0f;

// Function declarations
void CreateSDL2Window(int width, int height, const char* title);
void CleanupSDL2Window();
void InitGlad();
void CalculateFrameStats(Uint64& lastTime, float& deltaTime);
float GetWindowScaleFactor(SDL_Window* window);
void ProcessSDLEvent(const SDL_Event& e);
void EnableFileDrops(SDL_Window* window);
void MainLoop();

// Platform-specific functions
void OnApplicationCloseButtonClicked() {
    g_running = false;
#ifdef __EMSCRIPTEN__
    emscripten_cancel_main_loop();
#endif
}

void OnApplicationMaximizeButtonClicked() {
#ifdef __EMSCRIPTEN__
    // Use HTML5 fullscreen API for web
    EmscriptenFullscreenStrategy strategy;
    strategy.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_DEFAULT;
    strategy.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_STDDEF;
    strategy.filteringMode = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
    emscripten_request_fullscreen_strategy("#canvas", 1, &strategy);
    g_isMaximized = true;
#else
    if (!g_isMaximized) {
        SDL_GetWindowPosition(g_window, &g_windowRectBeforeMaximize.x, &g_windowRectBeforeMaximize.y);
        SDL_GetWindowSize(g_window, &g_windowRectBeforeMaximize.w, &g_windowRectBeforeMaximize.h);
        SDL_MaximizeWindow(g_window);
        g_isMaximized = true;
    }
#endif
}

void OnApplicationRestoreButtonClicked() {
#ifdef __EMSCRIPTEN__
    emscripten_exit_fullscreen();
    g_isMaximized = false;
#else
    if (g_isMaximized) {
        SDL_RestoreWindow(g_window);
        SDL_SetWindowPosition(g_window, g_windowRectBeforeMaximize.x, g_windowRectBeforeMaximize.y);
        SDL_SetWindowSize(g_window, g_windowRectBeforeMaximize.w, g_windowRectBeforeMaximize.h);
        g_isMaximized = false;
    }
#endif
}

void OnApplicationMinimizeButtonClicked() {
#ifndef __EMSCRIPTEN__
    SDL_MinimizeWindow(g_window);
#endif
}

void FatalError(const std::string& message) {
#ifdef __EMSCRIPTEN__
    EM_ASM({
        console.error(UTF8ToString($0));
        alert(UTF8ToString($0));
        }, message.c_str());
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal Error", message.c_str(), nullptr);
#endif
    std::cerr << "Fatal Error: " << message << std::endl;
    exit(1);
}

float GetWindowScaleFactor(SDL_Window* window) {
#ifdef __EMSCRIPTEN__
    // Get device pixel ratio from JavaScript
    double pixelRatio = EM_ASM_DOUBLE({
        return window.devicePixelRatio || 1.0;
        });
    return static_cast<float>(pixelRatio);
#else
    // Get the drawable size vs window size to determine the pixel ratio
    int windowW, windowH;
    int drawableW, drawableH;
    SDL_GetWindowSize(window, &windowW, &windowH);
    SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);

    // Calculate pixel ratio (this handles Retina/HDPI displays)
    float pixelRatio = 1.0f;
    if (windowW > 0) {
        pixelRatio = (float)drawableW / (float)windowW;
    }

    // Also get the DPI scale
    int displayIndex = SDL_GetWindowDisplayIndex(window);
    float ddpi = 96.0f, hdpi = 96.0f, vdpi = 96.0f;
    if (displayIndex >= 0) {
        if (SDL_GetDisplayDPI(displayIndex, &ddpi, &hdpi, &vdpi) == 0) {
            // Return the higher of pixel ratio or DPI scale
            float dpiScale = hdpi / 96.0f;
            return (pixelRatio > dpiScale) ? pixelRatio : dpiScale;
        }
    }

    return pixelRatio;
#endif
}

// Convert SDL keycode to Windows VK code for compatibility
unsigned int SDLKeyToVK(SDL_Keycode key) {
    // Map common keys to Windows VK codes
    switch (key) {
    case SDLK_ESCAPE: return 0x1B; // VK_ESCAPE
    case SDLK_RETURN: return 0x0D; // VK_RETURN
    case SDLK_SPACE: return 0x20;  // VK_SPACE
    case SDLK_BACKSPACE: return 0x08; // VK_BACK
    case SDLK_TAB: return 0x09; // VK_TAB
    case SDLK_DELETE: return 0x2E; // VK_DELETE
    case SDLK_LEFT: return 0x25; // VK_LEFT
    case SDLK_UP: return 0x26; // VK_UP
    case SDLK_RIGHT: return 0x27; // VK_RIGHT
    case SDLK_DOWN: return 0x28; // VK_DOWN
    case SDLK_HOME: return 0x24; // VK_HOME
    case SDLK_END: return 0x23; // VK_END
    case SDLK_PAGEUP: return 0x21; // VK_PRIOR
    case SDLK_PAGEDOWN: return 0x22; // VK_NEXT
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: return 0x10; // VK_SHIFT
    case SDLK_LCTRL:
    case SDLK_RCTRL: return 0x11; // VK_CONTROL
    case SDLK_LALT:
    case SDLK_RALT: return 0x12; // VK_MENU
    default:
        // For letters and numbers, SDL uses ASCII
        if (key >= SDLK_a && key <= SDLK_z) {
            return 'A' + (key - SDLK_a); // VK codes use uppercase
        }
        if (key >= SDLK_0 && key <= SDLK_9) {
            return '0' + (key - SDLK_0);
        }
        if (key >= SDLK_F1 && key <= SDLK_F12) {
            return 0x70 + (key - SDLK_F1); // VK_F1 = 0x70
        }
        return (unsigned int)key; // Fallback
    }
}

// Convert SDL mouse button to Windows MK_ constants
unsigned int SDLButtonToMK(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT: return 0x0001; // MK_LBUTTON
    case SDL_BUTTON_RIGHT: return 0x0002; // MK_RBUTTON
    case SDL_BUTTON_MIDDLE: return 0x0010; // MK_MBUTTON
    default: return 0;
    }
}

char32_t GetUnicodeFromSDLEvent(const SDL_Event& e) {
    if (e.type == SDL_TEXTINPUT) {
        // SDL provides UTF-8 text, convert to UTF-32
        const char* text = e.text.text;
        char32_t codepoint = 0;

        // Simple UTF-8 to UTF-32 conversion for first character
        unsigned char c = text[0];
        if (c < 0x80) {
            codepoint = c;
        }
        else if ((c & 0xE0) == 0xC0) {
            codepoint = ((c & 0x1F) << 6) | (text[1] & 0x3F);
        }
        else if ((c & 0xF0) == 0xE0) {
            codepoint = ((c & 0x0F) << 12) | ((text[1] & 0x3F) << 6) | (text[2] & 0x3F);
        }
        else if ((c & 0xF8) == 0xF0) {
            codepoint = ((c & 0x07) << 18) | ((text[1] & 0x3F) << 12) |
                ((text[2] & 0x3F) << 6) | (text[3] & 0x3F);
        }
        return codepoint;
    }
    return 0;
}

#ifdef __EMSCRIPTEN__
// Emscripten file upload handling
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void emscripten_file_dropped(const char* filename, void* buffer, int size) {
        OnFileDropped(filename, buffer, size);
    }
}

// Initialize JavaScript file drop handling
EM_JS(void, setupFileDrop, (), {
    if (!Module.fileDropSetup) {
        Module.fileDropSetup = true;
        
        var canvas = Module.canvas;
        var dropArea = canvas.parentElement || document.body;
        
        var events = ['dragenter', 'dragover', 'dragleave', 'drop'];
        for (var i = 0; i < events.length; i++) {
            dropArea.addEventListener(events[i], preventDefaults, false);
        }
        
        function preventDefaults(e) {
            e.preventDefault();
            e.stopPropagation();
        }
        
        var highlightEvents = ['dragenter', 'dragover'];
        for (var i = 0; i < highlightEvents.length; i++) {
            dropArea.addEventListener(highlightEvents[i], highlight, false);
        }
        
        var unhighlightEvents = ['dragleave', 'drop'];
        for (var i = 0; i < unhighlightEvents.length; i++) {
            dropArea.addEventListener(unhighlightEvents[i], unhighlight, false);
        }
        
        function highlight(e) {
            dropArea.classList.add('drag-over');
        }
        
        function unhighlight(e) {
            dropArea.classList.remove('drag-over');
        }
        
        dropArea.addEventListener('drop', handleDrop, false);
        
        function handleDrop(e) {
            var dt = e.dataTransfer;
            var files = dt.files;
            
            for (var i = 0; i < files.length; i++) {
                uploadFile(files[i]);
            }
        }
        
        function uploadFile(file) {
            var reader = new FileReader();
            reader.onload = function(e) {
                var data = new Uint8Array(e.target.result);
                var buffer = Module._malloc(data.length);
                Module.HEAPU8.set(data, buffer);
                
                Module.ccall('emscripten_file_dropped', null, 
                    ['string', 'number', 'number'],
                    [file.name, buffer, data.length]);
                
                Module._free(buffer);
            };
            reader.readAsArrayBuffer(file);
        }
    }
});
#endif

void ScaleMouseCoordinates(int& x, int& y) {
    // Get window size and drawable size
    int windowW, windowH;
    int drawableW, drawableH;
    SDL_GetWindowSize(g_window, &windowW, &windowH);
    SDL_GL_GetDrawableSize(g_window, &drawableW, &drawableH);
    
    // Calculate scale factors
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (windowW > 0) scaleX = (float)drawableW / (float)windowW;
    if (windowH > 0) scaleY = (float)drawableH / (float)windowH;
    
    // Scale the coordinates
    x = (int)(x * scaleX);
    y = (int)(y * scaleY);
}

void ProcessSDLEvent(const SDL_Event& e) {
    InputEvent event;
    event.time = SDL_GetTicks();

    switch (e.type) {
    case SDL_QUIT:
        g_running = false;
        break;

    case SDL_WINDOWEVENT:
        if (e.window.windowID == g_windowID) {
            switch (e.window.event) {
            case SDL_WINDOWEVENT_CLOSE:
                g_running = false;
                break;
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            {
                // Get the drawable size for proper HDPI rendering
                int drawableW, drawableH;
                SDL_GL_GetDrawableSize(g_window, &drawableW, &drawableH);
                static Uint64 lastResizeTime = 0;
                Uint64 currentTime = SDL_GetPerformanceCounter();
                Uint64 freq = SDL_GetPerformanceFrequency();

                // Only tick if 16ms have passed since last resize
                if ((currentTime - lastResizeTime) > (freq / 60)) {
                    Tick(drawableW, drawableH, 0.0f);
                    SDL_GL_SwapWindow(g_window);
                    lastResizeTime = currentTime;
                }
            }
            break;
            case SDL_WINDOWEVENT_MAXIMIZED:
                g_isMaximized = true;
                break;
            case SDL_WINDOWEVENT_RESTORED:
                g_isMaximized = false;
                break;
            }
        }
        break;

    case SDL_KEYDOWN:
        event.type = InputEvent::Type::KEY_DOWN;
        event.key.keyCode = SDLKeyToVK(e.key.keysym.sym);
        event.key.isRepeat = e.key.repeat != 0;
        event.key.unicode = 0; // Will be filled by SDL_TEXTINPUT
        event.key.shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
        event.key.ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;
        event.key.alt = (e.key.keysym.mod & KMOD_ALT) != 0;
        OnInput(event);
        break;

    case SDL_KEYUP:
        event.type = InputEvent::Type::KEY_UP;
        event.key.keyCode = SDLKeyToVK(e.key.keysym.sym);
        event.key.isRepeat = false;
        event.key.unicode = 0;
        event.key.shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
        event.key.ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;
        event.key.alt = (e.key.keysym.mod & KMOD_ALT) != 0;
        OnInput(event);
        break;

    case SDL_TEXTINPUT:
        // Send a special KEY_DOWN event with unicode
        event.type = InputEvent::Type::KEY_DOWN;
        event.key.keyCode = 0; // No VK code for text input
        event.key.isRepeat = false;
        event.key.unicode = GetUnicodeFromSDLEvent(e);
        event.key.shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
        event.key.ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
        event.key.alt = (SDL_GetModState() & KMOD_ALT) != 0;
        if (event.key.unicode != 0) {
            OnInput(event);
        }
        break;

    case SDL_MOUSEMOTION:
        {
            int scaledX = e.motion.x;
            int scaledY = e.motion.y;
            ScaleMouseCoordinates(scaledX, scaledY);
            
            event.type = InputEvent::Type::MOUSE_MOVE;
            event.mouse.x = scaledX;
            event.mouse.y = scaledY;
            event.mouse.ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
            event.mouse.shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
            event.mouse.button = 0;
            if (e.motion.state & SDL_BUTTON_LMASK) event.mouse.button |= 0x0001;
            if (e.motion.state & SDL_BUTTON_RMASK) event.mouse.button |= 0x0002;
            if (e.motion.state & SDL_BUTTON_MMASK) event.mouse.button |= 0x0010;
            OnInput(event);
        }
        break;

        case SDL_MOUSEBUTTONDOWN:
        {
            int scaledX = e.button.x;
            int scaledY = e.button.y;
            ScaleMouseCoordinates(scaledX, scaledY);
            
            event.type = InputEvent::Type::MOUSE_DOWN;
            event.mouse.x = scaledX;
            event.mouse.y = scaledY;
            event.mouse.button = SDLButtonToMK(e.button.button);
            event.mouse.ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
            event.mouse.shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
            OnInput(event);
    #ifndef __EMSCRIPTEN__
            SDL_CaptureMouse(SDL_TRUE);
    #endif
        }
        break;
    
        case SDL_MOUSEBUTTONUP:
        {
            int scaledX = e.button.x;
            int scaledY = e.button.y;
            ScaleMouseCoordinates(scaledX, scaledY);
            
            event.type = InputEvent::Type::MOUSE_UP;
            event.mouse.x = scaledX;
            event.mouse.y = scaledY;
            event.mouse.button = SDLButtonToMK(e.button.button);
            event.mouse.ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
            event.mouse.shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
            OnInput(event);
    #ifndef __EMSCRIPTEN__
            SDL_CaptureMouse(SDL_FALSE);
    #endif
        }
        break;
    
        case SDL_MOUSEWHEEL:
        {
            int x, y;
            SDL_GetMouseState(&x, &y);
            ScaleMouseCoordinates(x, y);
            
            event.type = InputEvent::Type::MOUSE_WHEEL;
            event.mouse.x = x;
            event.mouse.y = y;
            event.mouse.delta = e.wheel.y * 120; // Match Windows wheel delta
            event.mouse.ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
            event.mouse.shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
            OnInput(event);
        }
        break;

    case SDL_FINGERDOWN:
    {
        int windowW, windowH;
        SDL_GetWindowSize(g_window, &windowW, &windowH);
        event.type = InputEvent::Type::TOUCH_DOWN;
        event.touch.id = static_cast<int>(e.tfinger.fingerId);
        event.touch.x = static_cast<int>(e.tfinger.x * windowW);
        event.touch.y = static_cast<int>(e.tfinger.y * windowH);
        OnInput(event);
    }
    break;

    case SDL_FINGERUP:
    {
        int windowW, windowH;
        SDL_GetWindowSize(g_window, &windowW, &windowH);
        event.type = InputEvent::Type::TOUCH_UP;
        event.touch.id = static_cast<int>(e.tfinger.fingerId);
        event.touch.x = static_cast<int>(e.tfinger.x * windowW);
        event.touch.y = static_cast<int>(e.tfinger.y * windowH);
        OnInput(event);
    }
    break;

    case SDL_FINGERMOTION:
    {
        int windowW, windowH;
        SDL_GetWindowSize(g_window, &windowW, &windowH);
        event.type = InputEvent::Type::TOUCH_MOVE;
        event.touch.id = static_cast<int>(e.tfinger.fingerId);
        event.touch.x = static_cast<int>(e.tfinger.x * windowW);
        event.touch.y = static_cast<int>(e.tfinger.y * windowH);
        OnInput(event);
    }
    break;

#ifndef __EMSCRIPTEN__
    case SDL_DROPFILE:
    {
        char* droppedFile = e.drop.file;
        std::ifstream file(droppedFile, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            std::vector<char> buffer(static_cast<unsigned int>(size));
            if (file.read(buffer.data(), size)) {
                OnFileDropped(droppedFile, buffer.data(), static_cast<unsigned int>(size));
            }
        }
        SDL_free(droppedFile);
    }
    break;
#endif
    }
}

void CreateSDL2Window(int width, int height, const char* title) {
#ifndef __EMSCRIPTEN__
    // Enable HDPI support before creating window
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        FatalError("SDL_Init failed: " + std::string(SDL_GetError()));
    }

    // Set OpenGL attributes
#ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#ifndef __EMSCRIPTEN__
    // Get display DPI for initial window sizing
    int displayIndex = 0;
    float ddpi = 96.0f, hdpi = 96.0f, vdpi = 96.0f;
    SDL_GetDisplayDPI(displayIndex, &ddpi, &hdpi, &vdpi);
    float dpiScale = hdpi / 96.0f;
#endif

    // Create window with HDPI support
    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

#ifdef __EMSCRIPTEN__
    // For Emscripten, let the canvas size determine the window size
    int canvasWidth, canvasHeight;
    emscripten_get_canvas_element_size("#canvas", &canvasWidth, &canvasHeight);
    g_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        canvasWidth,
        canvasHeight,
        windowFlags
    );
#else
    g_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,  // Use logical size, SDL will handle the scaling
        height,
        windowFlags
    );
#endif

    if (!g_window) {
        FatalError("SDL_CreateWindow failed: " + std::string(SDL_GetError()));
    }

    g_windowID = SDL_GetWindowID(g_window);

    // Create OpenGL context
    g_glContext = SDL_GL_CreateContext(g_window);
    if (!g_glContext) {
        SDL_DestroyWindow(g_window);
        FatalError("SDL_GL_CreateContext failed: " + std::string(SDL_GetError()));
    }

    if (SDL_GL_MakeCurrent(g_window, g_glContext) < 0) {
        SDL_GL_DeleteContext(g_glContext);
        SDL_DestroyWindow(g_window);
        FatalError("SDL_GL_MakeCurrent failed: " + std::string(SDL_GetError()));
    }

    // Enable file drops
#ifdef __EMSCRIPTEN__
    setupFileDrop();
#else
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
#endif

    // Enable text input for unicode support
    SDL_StartTextInput();

    // Store initial window rect
    SDL_GetWindowPosition(g_window, &g_windowRectBeforeMaximize.x, &g_windowRectBeforeMaximize.y);
    SDL_GetWindowSize(g_window, &g_windowRectBeforeMaximize.w, &g_windowRectBeforeMaximize.h);
}

void CleanupSDL2Window() {
    if (g_glContext) {
        SDL_GL_DeleteContext(g_glContext);
        g_glContext = nullptr;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_Quit();
}

void InitGlad() {
#ifndef __EMSCRIPTEN__
    int version = gladLoadGL();
    if (version == 0) {
        FatalError("Failed to initialize GLAD (gladLoadGL returned 0).\n"
            "Ensure an OpenGL context is current and GLAD files are correctly set up.");
    }
#endif
    // For Emscripten, we don't need GLAD as WebGL functions are available directly
}

void CalculateFrameStats(Uint64& lastTime, float& deltaTime) {
    Uint64 currentTime = SDL_GetPerformanceCounter();
    Uint64 frequency = SDL_GetPerformanceFrequency();
    deltaTime = (float)(currentTime - lastTime) / frequency;
    lastTime = currentTime;
    if (deltaTime < 0) deltaTime = 0;
}

void PreciseSleep(double milliseconds) {
#ifndef __EMSCRIPTEN__
    using namespace std::chrono;

    if (milliseconds > 5.0) {
        SDL_Delay(static_cast<Uint32>(milliseconds - 5.0));
    }

    auto start = high_resolution_clock::now();
    while (duration<double, std::milli>(high_resolution_clock::now() - start).count() < milliseconds) {
        std::this_thread::yield();
    }
#endif
}

void MainLoop() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ProcessSDLEvent(event);
    }

    if (!g_running) {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        return;
    }

    CalculateFrameStats(g_lastTime, g_deltaTime);

    // Get the actual drawable size for rendering
    int drawableWidth, drawableHeight;
    SDL_GL_GetDrawableSize(g_window, &drawableWidth, &drawableHeight);

    if (drawableWidth > 0 && drawableHeight > 0) {
        if (!Tick(drawableWidth, drawableHeight, g_deltaTime)) {
            g_running = false;
#ifdef __EMSCRIPTEN__
            emscripten_cancel_main_loop();
#endif
        }
    }

    SDL_GL_SwapWindow(g_window);

#ifndef __EMSCRIPTEN__
    SDL_Delay(1);
#endif
}

#ifdef __EMSCRIPTEN__
// Callback for canvas resize
EM_BOOL emscripten_resize_callback(int eventType, const EmscriptenUiEvent* uiEvent, void* userData) {
    int width, height;
    emscripten_get_canvas_element_size("#canvas", &width, &height);
    SDL_SetWindowSize(g_window, width, height);

    // Get drawable size and update viewport
    int drawableW, drawableH;
    SDL_GL_GetDrawableSize(g_window, &drawableW, &drawableH);
    glViewport(0, 0, drawableW, drawableH);

    return EM_TRUE;
}
#endif

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Set DPI awareness for Windows to match the Win32 version
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#else
int main(int argc, char* argv[]) {
#endif
    unsigned int windowWidth = 1280;
    unsigned int windowHeight = 720;

    CreateSDL2Window(windowWidth, windowHeight, "Carrot.Code");
    InitGlad();

    // Get actual drawable size for OpenGL viewport
    int drawableWidth, drawableHeight;
    SDL_GL_GetDrawableSize(g_window, &drawableWidth, &drawableHeight);

    glViewport(0, 0, drawableWidth, drawableHeight);
    if (!Initialize(GetWindowScaleFactor(g_window))) {
        g_running = false;
    }

#ifdef __EMSCRIPTEN__
    // Set up resize callback
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, emscripten_resize_callback);

    // Initialize timing
    g_lastTime = SDL_GetPerformanceCounter();

    // Start the main loop
    emscripten_set_main_loop(MainLoop, 0, 1);
#else
    Uint64 lastTime = SDL_GetPerformanceCounter();
    float deltaTime = 0.0f;

    while (g_running) {
        MainLoop();
    }

    Shutdown();
    CleanupSDL2Window();
#endif

    return 0;
}

// Handle window dragging for custom title bar
void HandleTitleBarDragging() {
#ifndef __EMSCRIPTEN__
    // Get title bar interactive area
    unsigned int titleX, titleY, titleW, titleH;
    GetTitleBarInteractiveRect(&titleX, &titleY, &titleW, &titleH);

    int mouseX, mouseY;
    Uint32 mouseState = SDL_GetMouseState(&mouseX, &mouseY);

    static bool wasDragging = false;
    static int dragOffsetX = 0, dragOffsetY = 0;

    bool inTitleBar = (mouseX >= (int)titleX && mouseX < (int)(titleX + titleW) &&
        mouseY >= (int)titleY && mouseY < (int)(titleY + titleH));

    if ((mouseState & SDL_BUTTON_LMASK) && inTitleBar && !wasDragging) {
        // Start dragging
        wasDragging = true;
        int windowX, windowY;
        SDL_GetWindowPosition(g_window, &windowX, &windowY);
        dragOffsetX = mouseX;
        dragOffsetY = mouseY;
    }
    else if ((mouseState & SDL_BUTTON_LMASK) && wasDragging) {
        // Continue dragging
        int globalX, globalY;
        SDL_GetGlobalMouseState(&globalX, &globalY);
        SDL_SetWindowPosition(g_window, globalX - dragOffsetX, globalY - dragOffsetY);
    }
    else {
        wasDragging = false;
    }
#endif
}

#ifdef _WIN32
// Enable file drops on Windows using native API
void EnableFileDrops(SDL_Window * window) {
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo)) {
        HWND hwnd = wmInfo.info.win.window;
        DragAcceptFiles(hwnd, TRUE);
    }
}
#else
void EnableFileDrops(SDL_Window * window) {
    // SDL handles file drops natively on non-Windows platforms
}
#endif