#ifndef _H_PLATFORTM_CARROT_
#define _H_PLATFORTM_CARROT_

// Common functions for platform specific functionality.
// implementation in linux.cpp, windows.cpp, macos.cpp, and emscripten.cpp

#include "types.h"

typedef void (*platform_clipboard_copy_text_callback)(void* userData);
void platform_clipboard_copy_text(u32_string* content, platform_clipboard_copy_text_callback callback, void* userData);

typedef void (*platform_clipboard_paste_text_callback)(u32_string* content, void* userData); // content can be null
void platform_clipboard_paste_text(platform_clipboard_paste_text_callback callback, void* userData);

void platform_exit();
void platform_launch_browser(const char* url);

typedef void (*platform_open_file_callback)(u32_string* filePath, void* fileData, u32 fileBytes, void* userData); // path, data, and bytes will be 0 on fail
void platform_open_file(platform_open_file_callback callback, void* userData);

typedef void (*platform_modal_yesno_callback)(bool result, void* userData);
void platform_modal_yesno(const char* message, platform_modal_yesno_callback callback, void* userData);

typedef void (*platform_save_file_as_callback)(u32_string* filePath, void* userData); // filePath is either 0 for cancel, or the path to the file.
void platform_save_file_as(void* fileData, u32 fileSizeBytes, platform_save_file_as_callback callback, void* userData); // Show save as dialog, and write data

typedef void (*platform_write_file_callback)(bool result, void* userData);
void platform_write_file(u32_string* filePath, void* fileData, u32 fileSizeBytes, platform_write_file_callback callback, void* userData);

void platform_get_window_size(u32* width, u32* height);
u64 platform_get_milliseconds();

enum class PlatformKey : u32 {
    Unknown = 0,
    Backspace,
    Tab,
    Return,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    Escape,
    KeyA,
    KeyB,
    KeyC,
    KeyD,
    KeyE,
    KeyF,
    KeyG,
    KeyH,
    KeyI,
    KeyJ,
    KeyK,
    KeyL,
    KeyM,
    KeyN,
    KeyO,
    KeyP,
    KeyQ,
    KeyR,
    KeyS,
    KeyT,
    KeyU,
    KeyV,
    KeyW,
    KeyX,
    KeyY,
    KeyZ,
};


#endif
