#include "Platform.h"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <emscripten.h>
#include <string>

// Helper structure to pass callback data to JavaScript
struct CallbackData {
    void* callback;
    void* userData;
    size_t userDataSize;
};

// Global variable to store the next save filename
static std::string g_nextSaveAsFilename;

// Helper functions to trigger callbacks from JavaScript
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void TriggerSaveAsCallback(PlatformSaveAsResult callback, const char* path) {
        if (callback) {
            callback(path);
        }
    }

    EMSCRIPTEN_KEEPALIVE
    void TriggerSelectFileCallback(PlatformSelectFileResult callback, const char* path, unsigned char* buffer, unsigned int size) {
        if (callback) {
            callback(path, buffer, size);
        }
    }

    EMSCRIPTEN_KEEPALIVE
    void TriggerYesNoCallback(PlatformYesNoResult callback, bool result) {
        if (callback) {
            callback(result);
        }
    }

    EMSCRIPTEN_KEEPALIVE
    void* AllocateMemory(size_t size) {
        return malloc(size);
    }

    EMSCRIPTEN_KEEPALIVE
    void FreeMemory(void* ptr) {
        free(ptr);
    }

    EMSCRIPTEN_KEEPALIVE
    void CopyToMemory(void* dst, const void* src, size_t size) {
        memcpy(dst, src, size);
    }
    
    // Function to set the next save filename
    void PlatformSetNextSaveAsName(const char* filename) {
        if (filename) {
            g_nextSaveAsFilename = filename;
        } else {
            g_nextSaveAsFilename.clear();
        }
    }
}

extern "C" void PlatformSaveAs(const unsigned char* data, unsigned int size, PlatformSaveAsResult result) {
    const char* filename = g_nextSaveAsFilename.empty() ? nullptr : g_nextSaveAsFilename.c_str();
    
    EM_ASM({
        const dataPtr = $0;
        const dataSize = $1;
        const callbackPtr = $2;
        const filenamePtr = $3;
        
        // Get filename
        let filename = "export.dat"; // Default
        if (filenamePtr !== 0) {
            filename = UTF8ToString(filenamePtr);
        }
        
        // Create blob from WASM memory
        const memoryArray = new Uint8Array(Module.HEAPU8.buffer, dataPtr, dataSize);
        const blobObject = new Blob([memoryArray], {
            type: "application/octet-stream"
        });
        
        // Create download link
        const element = document.createElement('a');
        element.setAttribute('href', window.URL.createObjectURL(blobObject));
        element.setAttribute('download', filename);
        element.style.display = 'none';
        document.body.appendChild(element);
        element.click();
        document.body.removeChild(element);
        
        // Clean up blob URL
        window.URL.revokeObjectURL(element.href);
        
        // Trigger callback with empty path (since we don't get the actual save path in browser)
        if (callbackPtr !== 0) {
            Module._TriggerSaveAsCallback(callbackPtr, 0);
        }
    }, data, size, result, filename);
    
    // Clear the filename after use
    g_nextSaveAsFilename.clear();
}

extern "C" void PlatformSelectFile(const char* filter, PlatformSelectFileResult result) {
    EM_ASM({
        const filterPtr = $0;
        const callbackPtr = $1;
        
        // Get or create file input element
        let fileElement = document.getElementById("platformFileUploader");
        if (!fileElement) {
            fileElement = document.createElement('input');
            fileElement.type = "file";
            fileElement.id = "platformFileUploader";
            fileElement.style.display = 'none';
            document.body.appendChild(fileElement);
            
            // Add event listener
            fileElement.addEventListener('change', function() {
                const callback = this.pendingCallback;
                this.pendingCallback = null;
                
                if (!callback || this.files.length === 0) {
                    if (callback) {
                        Module._TriggerSelectFileCallback(callback, 0, 0, 0);
                    }
                    this.value = "";
                    return;
                }
                
                const file = this.files[0];
                
                file.arrayBuffer().then(arrayBuffer => {
                    // Allocate memory in WASM for file data
                    const uint8Array = new Uint8Array(arrayBuffer);
                    const dataPtr = Module._AllocateMemory(uint8Array.length);
                    const dataArray = new Uint8Array(Module.HEAPU8.buffer, dataPtr, uint8Array.length);
                    
                    // Copy file data to WASM memory
                    for (let i = 0; i < uint8Array.length; i++) {
                        dataArray[i] = uint8Array[i];
                    }
                    
                    // Allocate and copy filename
                    let namePtr = 0;
                    if (file.name) {
                        const encoder = new TextEncoder();
                        const nameBytes = encoder.encode(file.name);
                        namePtr = Module._AllocateMemory(nameBytes.length + 1);
                        const nameArray = new Uint8Array(Module.HEAPU8.buffer, namePtr, nameBytes.length + 1);
                        for (let i = 0; i < nameBytes.length; i++) {
                            nameArray[i] = nameBytes[i];
                        }
                        nameArray[nameBytes.length] = 0;
                    }
                    
                    // Trigger callback
                    Module._TriggerSelectFileCallback(callback, namePtr, dataPtr, uint8Array.length);
                    
                    // Clean up allocated memory
                    if (namePtr !== 0) {
                        Module._FreeMemory(namePtr);
                    }
                    if (dataPtr !== 0) {
                        Module._FreeMemory(dataPtr);
                    }
                    
                    this.value = "";
                }).catch(error => {
                    console.error("Error reading file:", error);
                    Module._TriggerSelectFileCallback(callback, 0, 0, 0);
                    this.value = "";
                });
            });
        }
        
        // Set filter if provided
        if (filterPtr !== 0) {
            const filterStr = UTF8ToString(filterPtr);
            if (filterStr && filterStr.length > 0) {
                fileElement.accept = filterStr;
            }
        }
        
        // Store callback and trigger click
        fileElement.pendingCallback = callbackPtr;
        fileElement.click();
    }, filter, result);
}

extern "C" void PlatformYesNoAlert(const char* message, PlatformYesNoResult callback) {
    bool result = EM_ASM_INT({
        const messagePtr = $0;
        const messageStr = messagePtr ? UTF8ToString(messagePtr) : "Are you sure?";
        
        // Use native confirm dialog which blocks
        return confirm(messageStr) ? 1 : 0;
    }, message);
    
    // Callback is called synchronously since confirm blocks
    if (callback) {
        callback(result);
    }
}

void PlatformWriteClipboardU16(const std::wstring& text) {
    // Convert wide string to UTF-8
    std::string utf8Text;
    for (wchar_t ch : text) {
        if (ch <= 0x7F) {
            utf8Text += static_cast<char>(ch);
        } else if (ch <= 0x7FF) {
            utf8Text += static_cast<char>(0xC0 | (ch >> 6));
            utf8Text += static_cast<char>(0x80 | (ch & 0x3F));
        } else {
            utf8Text += static_cast<char>(0xE0 | (ch >> 12));
            utf8Text += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            utf8Text += static_cast<char>(0x80 | (ch & 0x3F));
        }
    }
    
    EM_ASM({
        const textPtr = $0;
        const textStr = UTF8ToString(textPtr);
        
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(textStr).catch(err => {
                console.error('Failed to write to clipboard:', err);
            });
        } else {
            // Fallback for older browsers
            const textArea = document.createElement('textarea');
            textArea.value = textStr;
            textArea.style.position = 'fixed';
            textArea.style.left = '-999999px';
            document.body.appendChild(textArea);
            textArea.select();
            try {
                document.execCommand('copy');
            } catch (err) {
                console.error('Failed to copy to clipboard:', err);
            }
            document.body.removeChild(textArea);
        }
    }, utf8Text.c_str());
}

std::wstring PlatformReadClipboardU16() {
    // Use native prompt dialog which blocks
    char* clipboardText = (char*)EM_ASM_PTR({
        const userInput = prompt("Paste your text here (Ctrl+V or Cmd+V):", "");
        
        if (userInput === null || userInput === "") {
            return 0;
        }
        
        const len = lengthBytesUTF8(userInput) + 1;
        const ptr = Module._AllocateMemory(len);
        stringToUTF8(userInput, ptr, len);
        return ptr;
    });
    
    if (!clipboardText) {
        return L"";
    }
    
    // Convert UTF-8 to wide string
    std::wstring wideText;
    std::string utf8Text(clipboardText);
    free(clipboardText);  // Use standard free()
    
    for (size_t i = 0; i < utf8Text.length(); ) {
        unsigned char ch = utf8Text[i];
        if (ch <= 0x7F) {
            wideText += static_cast<wchar_t>(ch);
            i++;
        } else if ((ch & 0xE0) == 0xC0) {
            if (i + 1 < utf8Text.length()) {
                wideText += static_cast<wchar_t>(((ch & 0x1F) << 6) | (utf8Text[i + 1] & 0x3F));
                i += 2;
            } else {
                break;
            }
        } else if ((ch & 0xF0) == 0xE0) {
            if (i + 2 < utf8Text.length()) {
                wideText += static_cast<wchar_t>(((ch & 0x0F) << 12) | 
                                                ((utf8Text[i + 1] & 0x3F) << 6) | 
                                                (utf8Text[i + 2] & 0x3F));
                i += 3;
            } else {
                break;
            }
        } else {
            i++;
        }
    }
    
    return wideText;
}