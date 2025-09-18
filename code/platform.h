#ifndef _H_PLATFORTM_CARROT_
#define _H_PLATFORTM_CARROT_

// Common functions for platform specific functionality.
// implementation in linux.cpp, windows.cpp, macos.cpp, and emscripten.cpp

void platform_clipboard_copy_text(u32_string* content, callback);
void platform_clipboard_paste_text(callback);

#endif
