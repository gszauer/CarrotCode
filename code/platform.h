#ifndef _H_PLATFORTM_CARROT_
#define _H_PLATFORTM_CARROT_

// Common functions for platform specific functionality.
// implementation in linux.cpp, windows.cpp, macos.cpp, and emscripten.cpp

typedef void (*platform_clipboard_copy_text_callback)(void* userData);
void platform_clipboard_copy_text(u32_string* content, platform_clipboard_copy_text_callback callback, void* userData);

typedef void (*platform_clipboard_paste_text_callback)(u32_string* content, void* userData); // content can be null
void platform_clipboard_paste_text(platform_clipboard_paste_text_callback callback, void* userData);

void platform_exit();
void platform_launch_browser(const char* url);

typedef void (*platform_open_file_callback)(u32_string* filePath, void* fileData, u32 fileBytes, void* userData); // path, data, and bytes will be 0 on fail
void platform_open_file(platform_open_file_callback callback, void* userData);

#endif
