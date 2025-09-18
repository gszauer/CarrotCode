#ifndef _H_PLATFORTM_CARROT_
#define _H_PLATFORTM_CARROT_

// Common functions for platform specific functionality.
// implementation in linux.cpp, windows.cpp, macos.cpp, and emscripten.cpp

typedef void (*platform_clipboard_copy_text_callback)(void* userData);
void platform_clipboard_copy_text(u32_string* content, platform_clipboard_copy_text_callback callback, void* userData);

typedef void (*platform_clipboard_paste_text_callback)(u32_string* content, void* userData); // content can be null
void platform_clipboard_paste_text(platform_clipboard_paste_text_callback callback, void* userData);

void platform_exit();

#endif
