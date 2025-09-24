#include "application.h"
#include "platform.h"
#include "strings.h"
#include "renderer.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <cstdint>

// ------------------------------------------------------------------------------------------------- 
// JavaScript bridge helpers
// -------------------------------------------------------------------------------------------------

EM_JS(void, js_platform_blit, (uintptr_t pixelPtr, int width, int height, float scale), {
    Module.platform.blitCanvas(pixelPtr, width, height, scale);
});

EM_JS(void, js_platform_show_copy_modal, (const char* textPtr, int textLen), {
    Module.platform.showCopyModal(textPtr, textLen);
});

EM_JS(void, js_platform_show_paste_modal, (), {
    Module.platform.showPasteModal();
});

EM_JS(void, js_platform_show_yesno_modal, (const char* messagePtr, int messageLen), {
    Module.platform.showYesNoModal(messagePtr, messageLen);
});

EM_JS(void, js_platform_show_save_modal, (const char* defaultNamePtr, int defaultNameLen, const uint8_t* dataPtr, int dataLen), {
    Module.platform.showSaveModal(defaultNamePtr, defaultNameLen, dataPtr, dataLen);
});

EM_JS(void, js_platform_begin_open_file, (), {
    Module.platform.beginOpenFile();
});

EM_JS(void, js_platform_launch_url, (const char* urlPtr, int urlLen), {
    Module.platform.launchUrl(urlPtr, urlLen);
});

EM_JS(void, js_platform_download_file, (const char* namePtr, int nameLen, const uint8_t* dataPtr, int dataLen), {
    Module.platform.downloadFile(namePtr, nameLen, dataPtr, dataLen);
});

EM_JS(void, js_get_window_size, (int* outWidth, int* outHeight), {
    var size = Module.platform.getWindowSize();
    HEAP32[outWidth >> 2] = size[0] | 0;
    HEAP32[outHeight >> 2] = size[1] | 0;
});

EM_JS(float, js_get_device_pixel_ratio, (), {
    return (typeof window !== 'undefined' && window.devicePixelRatio) ? window.devicePixelRatio : 1.0;
});

// ------------------------------------------------------------------------------------------------- 
// Global state
// -------------------------------------------------------------------------------------------------

struct AsyncClipboardCopy {
    platform_clipboard_copy_text_callback callback = nullptr;
    void* userData = nullptr;
};

struct AsyncClipboardPaste {
    platform_clipboard_paste_text_callback callback = nullptr;
    void* userData = nullptr;
};

struct AsyncYesNo {
    platform_modal_yesno_callback callback = nullptr;
    void* userData = nullptr;
};

struct AsyncSaveAs {
    platform_save_file_as_callback callback = nullptr;
    void* userData = nullptr;
};

struct AsyncOpenFile {
    platform_open_file_callback callback = nullptr;
    void* userData = nullptr;
};

static UserData* g_user = nullptr;
static double g_lastTimeMs = 0.0;
static bool g_mouseLeft = false;
static bool g_mouseMiddle = false;
static bool g_mouseRight = false;
static int g_windowWidth = 1600;
static int g_windowHeight = 1200;
static bool g_requestQuit = false;

static AsyncClipboardCopy g_clipboardCopy;
static AsyncClipboardPaste g_clipboardPaste;
static AsyncYesNo g_yesNo;
static AsyncSaveAs g_saveAs;
static AsyncOpenFile g_openFile;

static float g_devicePixelRatio = 1.0f;
static void UpdateCanvasElementSize(u32 width, u32 height) {
    emscripten_set_canvas_element_size("#carrot-canvas", static_cast<int>(width), static_cast<int>(height));
}

// ------------------------------------------------------------------------------------------------- 
// Utility helpers
// -------------------------------------------------------------------------------------------------

static float GetZoomScale() {
    if (!g_user) return 1.0f;
    switch (g_user->zoom_level) {
        case 0: return 0.5f;
        case 2: return 2.0f;
        default: return 1.0f;
    }
}

static float GetDeviceScale() {
    return g_devicePixelRatio <= 0.0f ? 1.0f : g_devicePixelRatio;
}

static std::string U32StringToUtf8(u32_string* str) {
    std::string result;
    if (!str) return result;

    u8_string* utf8 = u32str_to_u8str(str);
    if (utf8) {
        u8* buffer = u8str_getBuffer(utf8);
        if (buffer) {
            result.assign(reinterpret_cast<char*>(buffer), static_cast<size_t>(u8str_size_bytes(utf8)));
        }
        u8str_destroy(utf8);
    }
    return result;
}

static u32_string* Utf8ToU32String(const char* data, int length) {
    if (!data || length <= 0) {
        return u32str_create();
    }

    std::vector<u8> temp(static_cast<size_t>(length) + 1);
    memcpy(temp.data(), data, static_cast<size_t>(length));
    temp[length] = 0;

    u8_string* utf8 = u8str_init(temp.data());
    if (!utf8) {
        return u32str_create();
    }

    u32_string* result = u8str_to_u32str(utf8);
    u8str_destroy(utf8);
    return result ? result : u32str_create();
}

static void RecreateCanvasForWindowSize() {
    if (!g_user) return;

    float scale = GetZoomScale();
    float dpi = GetDeviceScale();
    float denom = (scale > 0.0f ? scale : 1.0f);
    u32 desiredWidth = static_cast<u32>(std::max(1.0f, g_windowWidth * dpi / denom));
    u32 desiredHeight = static_cast<u32>(std::max(1.0f, g_windowHeight * dpi / denom));

    if (desiredWidth == 0) desiredWidth = 1;
    if (desiredHeight == 0) desiredHeight = 1;

    canvas_destroy(g_user->cnvs);
    g_user->cnvs = canvas_create(desiredWidth, desiredHeight);
    canvas_set_tile_debug_enabled(g_user->cnvs, g_user->tile_debug_enabled);
    ImGuiSetTargets(g_user->imgui_context, g_user->cnvs, g_user->fnt);
    UpdateCanvasElementSize(desiredWidth, desiredHeight);

    for (auto* view : g_user->views) {
        if (view) {
            document_view_set_display_size(view,
                                          desiredWidth,
                                          desiredHeight >= 51 ? desiredHeight - 51 : 0);
        }
    }
}

static void EnsureCanvasMatchesWindow() {
    if (!g_user) return;
    float scale = GetZoomScale();
    float dpi = GetDeviceScale();
    float denom = (scale > 0.0f ? scale : 1.0f);
    u32 currentWidth = canvas_get_width(g_user->cnvs);
    u32 currentHeight = canvas_get_height(g_user->cnvs);
    u32 desiredWidth = static_cast<u32>(std::max(1.0f, g_windowWidth * dpi / denom));
    u32 desiredHeight = static_cast<u32>(std::max(1.0f, g_windowHeight * dpi / denom));

    if (currentWidth != desiredWidth || currentHeight != desiredHeight) {
        RecreateCanvasForWindowSize();
    } else {
        UpdateCanvasElementSize(desiredWidth, desiredHeight);
    }
}

static void DispatchMouseEvent(ApplicationMouseEventType type, const EmscriptenMouseEvent* e,
                               float scrollDelta = 0.0f,
                               ApplicationMouseButton button = ApplicationMouseButton::NoneButton) {
    if (!g_user) return;

    u32 canvasWidth = canvas_get_width(g_user->cnvs);
    u32 canvasHeight = canvas_get_height(g_user->cnvs);

    float targetX = e ? e->targetX : 0.0f;
    float targetY = e ? e->targetY : 0.0f;
    if (!std::isfinite(targetX) || std::fabs(targetX) > 1e6f) {
        targetX = e ? e->canvasX : 0.0f;
    }
    if (!std::isfinite(targetY) || std::fabs(targetY) > 1e6f) {
        targetY = e ? e->canvasY : 0.0f;
    }

    double cssWidth = 0.0;
    double cssHeight = 0.0;
    if (emscripten_get_element_css_size("#carrot-canvas", &cssWidth, &cssHeight) != EMSCRIPTEN_RESULT_SUCCESS ||
        cssWidth <= 0.0 || cssHeight <= 0.0) {
        cssWidth = static_cast<double>(g_windowWidth);
        cssHeight = static_cast<double>(g_windowHeight);
    }

    float widthFactor = (canvasWidth > 0 && cssWidth > 0.0) ? static_cast<float>(canvasWidth / cssWidth) : 1.0f;
    float heightFactor = (canvasHeight > 0 && cssHeight > 0.0) ? static_cast<float>(canvasHeight / cssHeight) : 1.0f;

    int mappedX = static_cast<int>(std::lround(targetX * widthFactor));
    int mappedY = static_cast<int>(std::lround(targetY * heightFactor));

    if (mappedX < 0) mappedX = 0;
    if (mappedY < 0) mappedY = 0;
    if (canvasWidth > 0 && mappedX >= static_cast<int>(canvasWidth)) mappedX = static_cast<int>(canvasWidth) - 1;
    if (canvasHeight > 0 && mappedY >= static_cast<int>(canvasHeight)) mappedY = static_cast<int>(canvasHeight) - 1;

    u32 mouseX = static_cast<u32>(mappedX);
    u32 mouseY = static_cast<u32>(mappedY);

    ApplicationMouseEvent evt{};
    evt.type = type;
    evt.button = button;
    evt.x = mouseX;
    evt.y = mouseY;
    evt.normX = canvasWidth ? static_cast<f32>(mouseX) / static_cast<f32>(canvasWidth) : 0.0f;
    evt.normY = canvasHeight ? static_cast<f32>(mouseY) / static_cast<f32>(canvasHeight) : 0.0f;
    evt.scrollDelta = scrollDelta;
    evt.leftDown = g_mouseLeft;
    evt.middleDown = g_mouseMiddle;
    evt.rightDown = g_mouseRight;
    evt.shiftDown = e ? e->shiftKey : false;

    ApplicationHandleMouse(g_user, evt);
}

static PlatformKey TranslateKey(const EmscriptenKeyboardEvent* e) {
    if (!e) return PlatformKey::Unknown;

    const char* code = e->code;
    if (!code) return PlatformKey::Unknown;

    if (code[0] == 'K' && code[1] == 'e' && code[2] == 'y' && code[3]) {
        char c = code[3];
        if (c >= 'A' && c <= 'Z') {
            return static_cast<PlatformKey>(static_cast<u32>(PlatformKey::KeyA) + (c - 'A'));
        }
    }

    if (std::strcmp(code, "Backspace") == 0) return PlatformKey::Backspace;
    if (std::strcmp(code, "Tab") == 0) return PlatformKey::Tab;
    if (std::strcmp(code, "Enter") == 0 || std::strcmp(code, "NumpadEnter") == 0) return PlatformKey::Return;
    if (std::strcmp(code, "Delete") == 0) return PlatformKey::Delete;
    if (std::strcmp(code, "ArrowLeft") == 0) return PlatformKey::Left;
    if (std::strcmp(code, "ArrowRight") == 0) return PlatformKey::Right;
    if (std::strcmp(code, "ArrowUp") == 0) return PlatformKey::Up;
    if (std::strcmp(code, "ArrowDown") == 0) return PlatformKey::Down;
    if (std::strcmp(code, "Home") == 0) return PlatformKey::Home;
    if (std::strcmp(code, "End") == 0) return PlatformKey::End;
    if (std::strcmp(code, "Escape") == 0) return PlatformKey::Escape;
    return PlatformKey::Unknown;
}

// ------------------------------------------------------------------------------------------------- 
// Event callbacks
// -------------------------------------------------------------------------------------------------

static EM_BOOL KeyboardCallback(int eventType, const EmscriptenKeyboardEvent* e, void* /*userData*/) {
    if (!g_user || !e) return EM_FALSE;

    // Check if any modal is visible by checking if any element with class "modal" is not hidden
    // If a modal is visible, don't capture keyboard events so browser shortcuts work
    bool modalVisible = EM_ASM_INT({
        var modals = document.querySelectorAll('.modal:not(.hidden)');
        return modals.length > 0 ? 1 : 0;
    });

    if (modalVisible) {
        return EM_FALSE; // Let the browser handle the event
    }

    bool isKeyDown = (eventType == EMSCRIPTEN_EVENT_KEYDOWN);
    PlatformKey platformKey = TranslateKey(e);
    u32 nativeKey = static_cast<u32>(e->keyCode);

    u32 characterCode = 0;
    if (isKeyDown && e->key && e->key[0] && !e->ctrlKey && !e->altKey) {
        if (std::strlen(e->key) == 1) {
            characterCode = static_cast<unsigned char>(e->key[0]);
        } else if (std::strcmp(e->key, "Enter") == 0) {
            characterCode = '\n';
        } else if (std::strcmp(e->key, "Tab") == 0) {
            characterCode = '\t';
        } else if (std::strcmp(e->key, "Backspace") == 0) {
            characterCode = 0x08;
        }
    }

    ApplicationHandleKeyboard(g_user, characterCode, platformKey, nativeKey,
                              isKeyDown, e->altKey, e->ctrlKey, e->shiftKey);

    return EM_TRUE;
}

static EM_BOOL MouseCallback(int eventType, const EmscriptenMouseEvent* e, void* /*userData*/) {
    if (!g_user || !e) return EM_FALSE;

    switch (eventType) {
        case EMSCRIPTEN_EVENT_MOUSEMOVE:
            DispatchMouseEvent(ApplicationMouseEventType::Move, e, 0.0f, ApplicationMouseButton::NoneButton);
            break;
        case EMSCRIPTEN_EVENT_MOUSEDOWN:
            if (e->button == 0) {
                g_mouseLeft = true;
                DispatchMouseEvent(ApplicationMouseEventType::Press, e, 0.0f, ApplicationMouseButton::Left);
            } else if (e->button == 1) {
                g_mouseMiddle = true;
                DispatchMouseEvent(ApplicationMouseEventType::Press, e, 0.0f, ApplicationMouseButton::Middle);
            } else if (e->button == 2) {
                g_mouseRight = true;
                DispatchMouseEvent(ApplicationMouseEventType::Press, e, 0.0f, ApplicationMouseButton::Right);
            } else {
                DispatchMouseEvent(ApplicationMouseEventType::Press, e, 0.0f, ApplicationMouseButton::NoneButton);
            }
            break;
        case EMSCRIPTEN_EVENT_MOUSEUP:
            if (e->button == 0) {
                g_mouseLeft = false;
                DispatchMouseEvent(ApplicationMouseEventType::Release, e, 0.0f, ApplicationMouseButton::Left);
            } else if (e->button == 1) {
                g_mouseMiddle = false;
                DispatchMouseEvent(ApplicationMouseEventType::Release, e, 0.0f, ApplicationMouseButton::Middle);
            } else if (e->button == 2) {
                g_mouseRight = false;
                DispatchMouseEvent(ApplicationMouseEventType::Release, e, 0.0f, ApplicationMouseButton::Right);
            } else {
                DispatchMouseEvent(ApplicationMouseEventType::Release, e, 0.0f, ApplicationMouseButton::NoneButton);
            }
            break;
        default:
            break;
    }
    return EM_TRUE;
}

static EM_BOOL WheelCallback(int /*eventType*/, const EmscriptenWheelEvent* e, void* /*userData*/) {
    if (!g_user || !e) return EM_FALSE;

    float delta = 0.0f;
    double dy = e->deltaY;
    if (e->deltaMode == DOM_DELTA_LINE) {
        dy *= 32.0;
    } else if (e->deltaMode == DOM_DELTA_PAGE) {
        dy *= 320.0;
    }

    if (dy < 0) delta = 1.0f;
    else if (dy > 0) delta = -1.0f;

    EmscriptenMouseEvent mouseEquivalent{};
    mouseEquivalent.canvasX = e->mouse.canvasX;
    mouseEquivalent.canvasY = e->mouse.canvasY;
    mouseEquivalent.shiftKey = e->mouse.shiftKey;

    DispatchMouseEvent(ApplicationMouseEventType::Move, &mouseEquivalent, delta, ApplicationMouseButton::NoneButton);
    return EM_TRUE;
}

// ------------------------------------------------------------------------------------------------- 
// Main loop
// -------------------------------------------------------------------------------------------------

static void MainLoop() {
    if (!g_user) return;

    if (g_requestQuit || g_user->should_quit) {
        emscripten_cancel_main_loop();
        Shutdown(g_user);
        g_user = nullptr;
        return;
    }

    EnsureCanvasMatchesWindow();

    double now = emscripten_get_now();
    float delta = static_cast<float>((now - g_lastTimeMs) / 1000.0);
    if (delta < 0.0f) delta = 0.0f;
    if (delta > 0.25f) delta = 0.25f;
    g_lastTimeMs = now;

    Update(g_user, delta);

    canvas* cnvs = Render(g_user);
    if (!cnvs) return;

    u32* pixels = canvas_get_raw_pixels(cnvs);
    u32 width = canvas_get_width(cnvs);
    u32 height = canvas_get_height(cnvs);
    float scale = GetZoomScale();

    if (pixels && width > 0 && height > 0) {
        uintptr_t pixelPtr = reinterpret_cast<uintptr_t>(pixels);
        js_platform_blit(pixelPtr, static_cast<int>(width), static_cast<int>(height), scale);
    }
}

// ------------------------------------------------------------------------------------------------- 
// Platform API implementations
// -------------------------------------------------------------------------------------------------

u64 platform_get_milliseconds() {
    return static_cast<u64>(emscripten_get_now());
}

void platform_get_window_size(u32* width, u32* height) {
    if (width) *width = static_cast<u32>(g_windowWidth);
    if (height) *height = static_cast<u32>(g_windowHeight);
}

void platform_clipboard_copy_text(u32_string* content, platform_clipboard_copy_text_callback callback, void* userData) {
    g_clipboardCopy.callback = callback;
    g_clipboardCopy.userData = userData;

    // Clear mouse button state when showing modal to prevent stuck selection
    g_mouseLeft = false;
    g_mouseMiddle = false;
    g_mouseRight = false;

    std::string utf8 = U32StringToUtf8(content);
    js_platform_show_copy_modal(utf8.c_str(), static_cast<int>(utf8.size()));
}

void platform_clipboard_paste_text(platform_clipboard_paste_text_callback callback, void* userData) {
    g_clipboardPaste.callback = callback;
    g_clipboardPaste.userData = userData;

    // Clear mouse button state when showing modal to prevent stuck selection
    g_mouseLeft = false;
    g_mouseMiddle = false;
    g_mouseRight = false;

    js_platform_show_paste_modal();
}

void platform_exit() {
    g_requestQuit = true;
}

void platform_launch_browser(const char* url) {
    if (!url) return;
    js_platform_launch_url(url, static_cast<int>(std::strlen(url)));
}

void platform_open_file(platform_open_file_callback callback, void* userData) {
    g_openFile.callback = callback;
    g_openFile.userData = userData;
    js_platform_begin_open_file();
}

void platform_modal_yesno(const char* message, platform_modal_yesno_callback callback, void* userData) {
    g_yesNo.callback = callback;
    g_yesNo.userData = userData;

    if (!message) {
        js_platform_show_yesno_modal(nullptr, 0);
    } else {
        js_platform_show_yesno_modal(message, static_cast<int>(std::strlen(message)));
    }
}

void platform_save_file_as(void* fileData, u32 fileSizeBytes, platform_save_file_as_callback callback, void* userData) {
    g_saveAs.callback = callback;
    g_saveAs.userData = userData;

    const char* defaultName = "document.txt";
    js_platform_show_save_modal(defaultName, static_cast<int>(std::strlen(defaultName)),
                                reinterpret_cast<const uint8_t*>(fileData), static_cast<int>(fileSizeBytes));
}

void platform_write_file(u32_string* filePath, void* fileData, u32 fileSizeBytes, platform_write_file_callback callback, void* userData) {
    std::string utf8Name = U32StringToUtf8(filePath);
    if (utf8Name.empty()) {
        utf8Name = "document.txt";
    }

    js_platform_download_file(utf8Name.c_str(), static_cast<int>(utf8Name.size()),
                              reinterpret_cast<const uint8_t*>(fileData), static_cast<int>(fileSizeBytes));

    if (callback) {
        callback(true, userData);
    }
}

// ------------------------------------------------------------------------------------------------- 
// JavaScript callbacks
// -------------------------------------------------------------------------------------------------

extern "C" {

EMSCRIPTEN_KEEPALIVE void CarrotPlatformSetDevicePixelRatio(float dpi) {
    if (!std::isfinite(dpi) || dpi <= 0.0f) {
        dpi = 1.0f;
    }
    g_devicePixelRatio = dpi;
    EnsureCanvasMatchesWindow();
}

EMSCRIPTEN_KEEPALIVE void CarrotPlatformOnCopyFinished() {
    if (g_clipboardCopy.callback) {
        auto cb = g_clipboardCopy.callback;
        void* data = g_clipboardCopy.userData;
        g_clipboardCopy.callback = nullptr;
        g_clipboardCopy.userData = nullptr;
        cb(data);
    }

    // Send a synthetic mouse release event to clear any stuck selection state
    if (g_user) {
        EmscriptenMouseEvent syntheticEvent = {};
        syntheticEvent.canvasX = 0;
        syntheticEvent.canvasY = 0;
        DispatchMouseEvent(ApplicationMouseEventType::Release, &syntheticEvent, 0.0f, ApplicationMouseButton::Left);
    }
}

EMSCRIPTEN_KEEPALIVE void CarrotPlatformOnPasteResult(const char* textPtr, int textLen) {
    if (!g_clipboardPaste.callback) return;

    u32_string* converted = Utf8ToU32String(textPtr, textLen);
    auto cb = g_clipboardPaste.callback;
    void* data = g_clipboardPaste.userData;

    g_clipboardPaste.callback = nullptr;
    g_clipboardPaste.userData = nullptr;

    cb(converted, data);
    if (converted) {
        u32str_destroy(converted);
    }

    // Send a synthetic mouse release event to clear any stuck selection state
    if (g_user) {
        EmscriptenMouseEvent syntheticEvent = {};
        syntheticEvent.canvasX = 0;
        syntheticEvent.canvasY = 0;
        DispatchMouseEvent(ApplicationMouseEventType::Release, &syntheticEvent, 0.0f, ApplicationMouseButton::Left);
    }
}

EMSCRIPTEN_KEEPALIVE void CarrotPlatformOnPasteCanceled() {
    if (!g_clipboardPaste.callback) return;
    auto cb = g_clipboardPaste.callback;
    void* data = g_clipboardPaste.userData;
    g_clipboardPaste.callback = nullptr;
    g_clipboardPaste.userData = nullptr;
    cb(nullptr, data);

    // Send a synthetic mouse release event to clear any stuck selection state
    if (g_user) {
        EmscriptenMouseEvent syntheticEvent = {};
        syntheticEvent.canvasX = 0;
        syntheticEvent.canvasY = 0;
        DispatchMouseEvent(ApplicationMouseEventType::Release, &syntheticEvent, 0.0f, ApplicationMouseButton::Left);
    }
}

EMSCRIPTEN_KEEPALIVE void CarrotPlatformOnYesNoResult(int yes) {
    if (!g_yesNo.callback) return;
    auto cb = g_yesNo.callback;
    void* data = g_yesNo.userData;
    g_yesNo.callback = nullptr;
    g_yesNo.userData = nullptr;
    cb(yes != 0, data);
}

EMSCRIPTEN_KEEPALIVE void CarrotPlatformOnSaveResult(const char* namePtr, int nameLen) {
    if (!g_saveAs.callback) return;

    u32_string* path = Utf8ToU32String(namePtr, nameLen);
    auto cb = g_saveAs.callback;
    void* data = g_saveAs.userData;

    g_saveAs.callback = nullptr;
    g_saveAs.userData = nullptr;

    cb(path, data);
    if (path) {
        u32str_destroy(path);
    }
}

EMSCRIPTEN_KEEPALIVE void CarrotPlatformOnSaveCanceled() {
    if (!g_saveAs.callback) return;
    auto cb = g_saveAs.callback;
    void* data = g_saveAs.userData;
    g_saveAs.callback = nullptr;
    g_saveAs.userData = nullptr;
    cb(nullptr, data);
}

EMSCRIPTEN_KEEPALIVE void CarrotPlatformOnOpenFileResult(const char* namePtr, int nameLen, const uint8_t* dataPtr, int dataLen) {
    if (!g_openFile.callback) return;

    u32_string* path = Utf8ToU32String(namePtr, nameLen);

    void* copy = nullptr;
    if (dataPtr && dataLen > 0) {
        copy = malloc(static_cast<size_t>(dataLen));
        if (copy) {
            memcpy(copy, dataPtr, static_cast<size_t>(dataLen));
        }
    }

    auto cb = g_openFile.callback;
    void* userData = g_openFile.userData;

    g_openFile.callback = nullptr;
    g_openFile.userData = nullptr;

    if (cb) {
        u32 byteCount = (copy && dataLen > 0) ? static_cast<u32>(dataLen) : 0u;
        cb(path, copy, byteCount, userData);
    }

    if (path) {
        u32str_destroy(path);
    }
    if (copy) {
        free(copy);
    }
}

EMSCRIPTEN_KEEPALIVE void CarrotPlatformOnOpenFileCanceled() {
    if (!g_openFile.callback) return;
    auto cb = g_openFile.callback;
    void* data = g_openFile.userData;
    g_openFile.callback = nullptr;
    g_openFile.userData = nullptr;
    cb(nullptr, nullptr, 0, data);
}

EMSCRIPTEN_KEEPALIVE void CarrotPlatformOnWindowResized(int width, int height) {
    g_windowWidth = width;
    g_windowHeight = height;
    RecreateCanvasForWindowSize();
}

}

// ------------------------------------------------------------------------------------------------- 
// Entry point
// -------------------------------------------------------------------------------------------------

int main() {
    js_get_window_size(&g_windowWidth, &g_windowHeight);
    g_devicePixelRatio = js_get_device_pixel_ratio();
    g_user = Initialize(static_cast<u32>(g_windowWidth), static_cast<u32>(g_windowHeight));
    if (!g_user) {
        return 1;
    }

    EnsureCanvasMatchesWindow();

    EM_ASM({
        Module._carrotMain = Module._main;
        if (Module.runtimeInitialized) {
            Module._carrotMain();
            Module._carrotMain = undefined;
        }
    });

    g_lastTimeMs = emscripten_get_now();

    emscripten_set_main_loop(MainLoop, 0, false);

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, KeyboardCallback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, true, KeyboardCallback);
    emscripten_set_mousedown_callback("#carrot-canvas", nullptr, true, MouseCallback);
    emscripten_set_mouseup_callback("#carrot-canvas", nullptr, true, MouseCallback);
    emscripten_set_mousemove_callback("#carrot-canvas", nullptr, true, MouseCallback);
    emscripten_set_wheel_callback("#carrot-canvas", nullptr, true, WheelCallback);
    return 0;
}
