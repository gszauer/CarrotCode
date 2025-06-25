#include <Windows.h>
#include "Platform.h"
#include <cstring>
#include <iostream>
#include <algorithm>

std::string ReplaceBackSlashesWithForwardSlashes(const char* path) {
    /* defensive, but shouldn't be needed...
    if (path == 0) {
        return "";
    }*/
    std::string absolutePath = path;
    if (absolutePath.find('\\') != std::string::npos) {
        std::replace(absolutePath.begin(), absolutePath.end(), '\\', '/');
    }

    return absolutePath;
}

std::pair<std::string, std::string> SplitPath(const std::string& fullPath) {
    size_t pos = fullPath.find_last_of('/');
    if (pos == std::string::npos) {
        return { "", fullPath };
    }

    std::string path = fullPath.substr(0, pos + 1); // Include the trailing slash
    std::string fileName = fullPath.substr(pos + 1);

    return { path, fileName };
}

std::string RemoveExtension(const std::string& path) {
    size_t pos = path.find_last_of('.');
    if (pos == std::string::npos) {
        return path;
    }

    return path.substr(0, pos);
}

extern "C" void PlatformSetNextSaveAsName(const char* filename) {

}

extern "C" void PlatformAssert(bool cond) {
    if (!cond) {
        char* knull = (char*)0;
        *knull = 0;
    }
}

std::string PlatformReadClipboardU8() {
    if (OpenClipboard(NULL) == 0) {
        return "";
    }

    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData == NULL) {
        CloseClipboard();
        return "";
    }

    char* str = (char*)GlobalLock(hData);
    if (str == NULL) {
        CloseClipboard();
        return "";
    }

    std::string result = str;
    GlobalUnlock(hData);
    CloseClipboard();
    return result;
}

void PlatformWriteClipboardU8(const char* buffer) {
    if (buffer == 0) {
        return;
    }
    const char* null = buffer;
    while (*null) {
        ++null;
    }
    int len = (int)(null - buffer);

    HGLOBAL hdst;
    LPWSTR dst;

    // Allocate string
    hdst = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (len + 1) * sizeof(WCHAR));
    dst = (LPWSTR)GlobalLock(hdst);
    if (dst == 0) {
        return;
    } // Make the compiler shut up

    for (int i = 0; i < len; ++i) {
        dst[i] = buffer[i];
    }
    dst[len] = 0;
    GlobalUnlock(hdst);

    // Set clipboard data
    if (!OpenClipboard(NULL)) {
        PlatformAssert(false);
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, hdst)) {
        PlatformAssert(false);
    }
    CloseClipboard();
}

std::string GetExecutablePath() {
    CHAR* pathBuffer = new CHAR[1024];
    memset(pathBuffer, 0, 1024);
    GetModuleFileNameA(NULL, pathBuffer, MAX_PATH);
    std::string path = ReplaceBackSlashesWithForwardSlashes(pathBuffer);
    delete[] pathBuffer;
    return SplitPath(path).first;
}

extern "C" void PlatformSaveAs(const unsigned char* data, unsigned int size, PlatformSaveAsResult result) {
    static char path[1024] = { 0 };
    ZeroMemory(path, 1024);

    OPENFILENAMEA saveFileName;
    ZeroMemory(&saveFileName, sizeof(OPENFILENAMEA));
    saveFileName.lStructSize = sizeof(OPENFILENAMEA);
    saveFileName.hwndOwner = GetActiveWindow();// gHwnd;
    saveFileName.lpstrFile = path;
    saveFileName.lpstrFilter = "All\0*.*\0\0";
    saveFileName.nMaxFile = 1024;
    saveFileName.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (FALSE != GetSaveFileNameA(&saveFileName)) {
        DWORD written = 0;
        HANDLE hFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        WriteFile(hFile, data, (DWORD)size, &written, NULL);
        CloseHandle(hFile);

        if (result != 0) {
            result(path);
        }
    }
    else {
        if (result != 0) {
            result(0);
        }
    }
}

extern "C" void PlatformSelectFile(const char* filter, PlatformSelectFileResult result) {
    static CHAR UI_fileNameBuffer[1024] = { 0 };
    memset(UI_fileNameBuffer, 0, 1024);
    OPENFILENAMEA ofn;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = UI_fileNameBuffer;
    ofn.nMaxFile = sizeof(UI_fileNameBuffer);
    if (filter != 0) {
        ofn.lpstrFilter = (filter);
    }
    else {
        ofn.lpstrFilter = ("All\0*.*\0\0");
    }

    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        HANDLE hFile = CreateFileA(UI_fileNameBuffer, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD bytesInFile = GetFileSize(hFile, 0);
        DWORD bytesRead = 0;

        if (hFile == INVALID_HANDLE_VALUE) {
            return;
        }
        unsigned char* fileBuffer = new unsigned char[bytesInFile + 1];

        if (ReadFile(hFile, fileBuffer, bytesInFile, &bytesRead, NULL) != 0) {
            fileBuffer[bytesInFile] = 0; // Force json parser to stop
            if (result != 0) {
                result(UI_fileNameBuffer, fileBuffer, bytesInFile);
            }
        }
        else {
            if (result != 0) {
                result(UI_fileNameBuffer, 0, 0);
            }
        }

        delete[] fileBuffer;
        CloseHandle(hFile);
    }
    else {
        if (result != 0) {
            result(0, 0, 0);
        }
    }
}

extern "C" void PlatformWriteFile(const char* path, unsigned char* buffer, unsigned int size, PlatformWriteFileResult callback, void* userData, unsigned int userDataSize) {
    DWORD written = 0;
    bool called = false;

    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        if (!WriteFile(hFile, buffer, (DWORD)size, &written, NULL)) {
            if (callback != 0) {
                callback(path, false, userData);
            }
            called = true;
            CloseHandle(hFile);
            return;
        }
        CloseHandle(hFile);
    }

    if (written != size && written > 0) {
        if (callback != 0) {
            callback(path, false, userData);
        }
        called = true;
    }
    else {
        if (callback != 0) {
            callback(path, true, userData);
        }
        called = true;
    }

    PlatformAssert(called);
}

extern "C" void PlatformReadFile(const char* path, PlatformReadFileResult callback) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (callback) {
            callback(path, 0, 0);
        }
        return;
    }
    DWORD bytesInFile = GetFileSize(hFile, 0);
    DWORD bytesRead = 0;

    void* data = malloc(bytesInFile + 1);
    if (ReadFile(hFile, data, bytesInFile, &bytesRead, NULL) != 0) {
        if (callback) {
            callback(path, data, bytesInFile);
        }
    }
    else {
        if (callback) {
            callback(path, 0, 0);
        }
    }

    free(data);
    CloseHandle(hFile);
}

void PlatformExit() {
    SendMessageA(GetActiveWindow(), WM_CLOSE, 0, 0);
}

extern "C" void PlatformHasFile(const char* url, PlatofrmHasFileResult callback) {
    if (callback != 0) {
        DWORD attributes = GetFileAttributesA(url);
        callback(url, attributes != INVALID_FILE_ATTRIBUTES);
    }
}

void PlatformWriteClipboardU16(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) {
        std::cerr << "ClipboardUtil Error: Cannot open clipboard (OpenClipboard failed)." << std::endl;
        return;
    }

    if (!EmptyClipboard()) {
        std::cerr << "ClipboardUtil Error: Cannot empty clipboard (EmptyClipboard failed)." << std::endl;
        CloseClipboard();
        return;
    }

    // Calculate required buffer size in bytes (including null terminator for the string)
    SIZE_T sizeInBytes = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeInBytes);

    if (hGlobal == nullptr) {
        std::cerr << "ClipboardUtil Error: GlobalAlloc failed." << std::endl;
        CloseClipboard();
        return;
    }

    // Lock the handle to get a pointer to the memory.
    {
        wchar_t* pGlobalLock = static_cast<wchar_t*>(GlobalLock(hGlobal));
        if (pGlobalLock == nullptr) {
            std::cerr << "ClipboardUtil Error: GlobalLock failed." << std::endl;
            GlobalFree(hGlobal); // Free the allocated memory
            CloseClipboard();
            return;
        }

        // Copy the string data into the allocated memory.
        memcpy(pGlobalLock, text.c_str(), sizeInBytes);

        // Unlock the memory.
        GlobalUnlock(hGlobal);
    }

    // Place the handle on the clipboard.
    // The system now owns the memory if SetClipboardData is successful.
    if (SetClipboardData(CF_UNICODETEXT, hGlobal) == nullptr) {
        std::cerr << "ClipboardUtil Error: SetClipboardData failed. Win32 Error: " << GetLastError() << std::endl;
        GlobalFree(hGlobal); // We must free it if SetClipboardData failed.
    }

    // If successful, hGlobal is now owned by the clipboard, do not free it.
    CloseClipboard();
}

std::wstring PlatformReadClipboardU16() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        // This is not necessarily an error, just means no text is available in this format.
        return L"";
    }

    if (!OpenClipboard(nullptr)) {
        std::cerr << "Error: Cannot open clipboard for reading (OpenClipboard failed)." << std::endl;
        return L"";
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == nullptr) { // Could happen if, e.g., another app empties clipboard after IsClipboardFormatAvailable
        std::cerr << "Error: GetClipboardData failed. Win32 Error: " << GetLastError() << std::endl;
        CloseClipboard();
        return L"";
    }

    wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
    if (pText == nullptr) { // Should not happen if hData was valid, but check anyway.
        std::cerr << "Error: GlobalLock failed for reading clipboard data." << std::endl;
        CloseClipboard();
        return L"";
    }

    // Construct wstring from the locked C-style string.
    std::wstring clipboardText(pText);

    // Unlock the handle.
    GlobalUnlock(hData);
    CloseClipboard();

    return clipboardText;
}

extern "C" void PlatformYesNoAlert(const char* message, PlatformYesNoResult callback) {
    // Convert parameters to wide strings for Windows MessageBox
    int messageLen = MultiByteToWideChar(CP_UTF8, 0, message, -1, NULL, 0);
    wchar_t* wMessage = new wchar_t[messageLen];
    MultiByteToWideChar(CP_UTF8, 0, message, -1, wMessage, messageLen);

    // Show message box with Yes/No buttons
    // MB_DEFBUTTON2 makes "No" the default (also applies when closing with X)
    int result = MessageBoxW(GetActiveWindow(), wMessage, L"Select action",
        MB_OKCANCEL| MB_DEFBUTTON2);

    // Clean up allocated memory
    delete[] wMessage;

    // Call the callback with the result
    if (callback != 0) {
        // Only IDYES returns true, both IDNO and IDCANCEL (close button) return false
        callback(result == IDOK);
    }
}