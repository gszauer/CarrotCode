#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>

#include "application.h"
#include "platform.h"
#include "strings.h"
#include "renderer.h"
#include "document.h"
#include "imgui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <vector>
#include <string>

#define WINDOW_WIDTH 1600
#define WINDOW_HEIGHT 1200
#define WINDOW_CLASS "CarrotCode"
#define WINDOW_TITLE "Carrot Code"

struct WindowData {
    HWND hwnd;
    HDC hdcDisplay;
    HDC hdcMemory;
    HBITMAP hbmBackBuffer;
    HBITMAP hbmOld;
    HINSTANCE hInstance;
    bool closeWindow;
    int width;
    int height;
    unsigned int* pixels;
};

static WindowData* g_windowData = nullptr;
static UserData* g_userData = nullptr;
static u8_string* g_clipboardText = nullptr;
static bool g_leftButtonDown = false;
static bool g_middleButtonDown = false;
static bool g_rightButtonDown = false;

u64 platform_get_milliseconds() {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (u64)(counter.QuadPart * 1000 / freq.QuadPart);
}

static PlatformKey TranslateVirtualKey(WPARAM vk) {
    switch (vk) {
        case VK_BACK: return PlatformKey::Backspace;
        case VK_TAB: return PlatformKey::Tab;
        case VK_RETURN: return PlatformKey::Return;
        case VK_DELETE: return PlatformKey::Delete;
        case VK_LEFT: return PlatformKey::Left;
        case VK_RIGHT: return PlatformKey::Right;
        case VK_UP: return PlatformKey::Up;
        case VK_DOWN: return PlatformKey::Down;
        case VK_HOME: return PlatformKey::Home;
        case VK_END: return PlatformKey::End;
        case VK_ESCAPE: return PlatformKey::Escape;
        default: break;
    }

    if (vk >= 'A' && vk <= 'Z') {
        return static_cast<PlatformKey>(static_cast<u32>(PlatformKey::KeyA) + (vk - 'A'));
    }

    return PlatformKey::Unknown;
}

static void HandleFileDrop(HDROP hDrop) {
    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);

    for (UINT i = 0; i < fileCount; i++) {
        UINT pathLength = DragQueryFileW(hDrop, i, NULL, 0) + 1;
        wchar_t* widePath = (wchar_t*)malloc(pathLength * sizeof(wchar_t));

        if (widePath && DragQueryFileW(hDrop, i, widePath, pathLength)) {
            int utf8Length = WideCharToMultiByte(CP_UTF8, 0, widePath, -1, NULL, 0, NULL, NULL);
            char* utf8Path = (char*)malloc(utf8Length);

            if (utf8Path) {
                WideCharToMultiByte(CP_UTF8, 0, widePath, -1, utf8Path, utf8Length, NULL, NULL);

                std::ifstream file(utf8Path, std::ios::binary);
                if (file.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
                    file.close();

                    std::vector<u32> u32content;
                    for (char c : content) {
                        u32content.push_back((u32)(unsigned char)c);
                    }
                    u32content.push_back(0);

                    u32_string* file_str = u32str_init(u32content.data());
                    document* new_doc = doc_from_str32(file_str, 100);
                    u32str_destroy(file_str);

                    AddDocumentView(g_userData, new_doc, utf8Path);
                }

                free(utf8Path);
            }
        }

        if (widePath) free(widePath);
    }

    DragFinish(hDrop);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    switch (iMsg) {
        case WM_CREATE:
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)g_windowData);
            DragAcceptFiles(hwnd, TRUE);
            return 0;

        case WM_SIZE:
            if (g_windowData && g_userData) {
                RECT rect;
                GetClientRect(hwnd, &rect);
                int newWidth = rect.right - rect.left;
                int newHeight = rect.bottom - rect.top;

                if (newWidth != g_windowData->width || newHeight != g_windowData->height) {
                    g_windowData->width = newWidth;
                    g_windowData->height = newHeight;

                    if (g_windowData->pixels) {
                        free(g_windowData->pixels);
                    }
                    g_windowData->pixels = (unsigned int*)malloc(newWidth * newHeight * sizeof(unsigned int));
                    memset(g_windowData->pixels, 0, newWidth * newHeight * sizeof(unsigned int));

                    if (g_windowData->hbmBackBuffer) {
                        SelectObject(g_windowData->hdcMemory, g_windowData->hbmOld);
                        DeleteObject(g_windowData->hbmBackBuffer);
                    }

                    g_windowData->hbmBackBuffer = CreateCompatibleBitmap(g_windowData->hdcDisplay, newWidth, newHeight);
                    g_windowData->hbmOld = (HBITMAP)SelectObject(g_windowData->hdcMemory, g_windowData->hbmBackBuffer);

                    float scale = 1.0f;
                    if (g_userData->zoom_level == 0) scale = 0.5f;
                    else if (g_userData->zoom_level == 1) scale = 1.0f;
                    else if (g_userData->zoom_level == 2) scale = 2.0f;

                    canvas_destroy(g_userData->cnvs);
                    g_userData->cnvs = canvas_create((u32)(newWidth / scale), (u32)(newHeight / scale));
                    ImGuiSetTargets(g_userData->imgui_context, g_userData->cnvs, g_userData->fnt);
                }
            }
            return 0;

        case WM_DROPFILES:
            HandleFileDrop((HDROP)wParam);
            return 0;

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_CHAR:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            {
                bool isKeyDown = (iMsg == WM_KEYDOWN || iMsg == WM_SYSKEYDOWN);
                bool isCharMsg = (iMsg == WM_CHAR);

                PlatformKey platformKey = PlatformKey::Unknown;
                u32 characterCode = 0;
                u32 nativeKey = (u32)wParam;

                if (isCharMsg) {
                    characterCode = (u32)wParam;
                } else {
                    platformKey = TranslateVirtualKey(wParam);
                    if (isKeyDown && wParam >= 0x20 && wParam < 0x7F) {
                        characterCode = (u32)wParam;
                    }
                }

                bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
                bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                if (!isCharMsg || characterCode != 0) {
                    ApplicationHandleKeyboard(g_userData, characterCode, platformKey,
                                            nativeKey, isKeyDown, altDown, ctrlDown, shiftDown);

                    if (g_userData->should_quit) {
                        PostQuitMessage(0);
                        g_userData->should_quit = false;
                    }
                }
            }
            return 0;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
            {
                float scale = 1.0f;
                if (g_userData->zoom_level == 0) scale = 0.5f;
                else if (g_userData->zoom_level == 1) scale = 1.0f;
                else if (g_userData->zoom_level == 2) scale = 2.0f;

                int rawX = LOWORD(lParam);
                int rawY = HIWORD(lParam);

                if (iMsg == WM_MOUSEWHEEL) {
                    POINT pt;
                    pt.x = LOWORD(lParam);
                    pt.y = HIWORD(lParam);
                    ScreenToClient(hwnd, &pt);
                    rawX = pt.x;
                    rawY = pt.y;
                }

                u32 mouseX = static_cast<u32>(rawX / scale);
                u32 mouseY = static_cast<u32>(rawY / scale);
                f32 normX = static_cast<f32>(mouseX) / g_windowData->width;
                f32 normY = static_cast<f32>(mouseY) / g_windowData->height;

                ApplicationMouseEvent evt{};
                evt.x = mouseX;
                evt.y = mouseY;
                evt.normX = normX;
                evt.normY = normY;
                evt.scrollDelta = 0.0f;
                evt.button = ApplicationMouseButton::NoneButton;

                switch (iMsg) {
                    case WM_LBUTTONDOWN:
                        SetCapture(hwnd);
                        g_leftButtonDown = true;
                        evt.type = ApplicationMouseEventType::Press;
                        evt.button = ApplicationMouseButton::Left;
                        break;
                    case WM_LBUTTONUP:
                        ReleaseCapture();
                        g_leftButtonDown = false;
                        evt.type = ApplicationMouseEventType::Release;
                        evt.button = ApplicationMouseButton::Left;
                        break;
                    case WM_RBUTTONDOWN:
                        g_rightButtonDown = true;
                        evt.type = ApplicationMouseEventType::Press;
                        evt.button = ApplicationMouseButton::Right;
                        break;
                    case WM_RBUTTONUP:
                        g_rightButtonDown = false;
                        evt.type = ApplicationMouseEventType::Release;
                        evt.button = ApplicationMouseButton::Right;
                        break;
                    case WM_MBUTTONDOWN:
                        g_middleButtonDown = true;
                        evt.type = ApplicationMouseEventType::Press;
                        evt.button = ApplicationMouseButton::Middle;
                        break;
                    case WM_MBUTTONUP:
                        g_middleButtonDown = false;
                        evt.type = ApplicationMouseEventType::Release;
                        evt.button = ApplicationMouseButton::Middle;
                        break;
                    case WM_MOUSEMOVE:
                        evt.type = ApplicationMouseEventType::Move;
                        break;
                    case WM_MOUSEWHEEL:
                        {
                            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                            evt.scrollDelta = (float)delta / WHEEL_DELTA;
                            evt.type = ApplicationMouseEventType::Move;
                        }
                        break;
                }

                evt.leftDown = g_leftButtonDown;
                evt.middleDown = g_middleButtonDown;
                evt.rightDown = g_rightButtonDown;
                evt.shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                ApplicationHandleMouse(g_userData, evt);
            }
            return 0;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            {
                if (g_windowData && g_windowData->hdcMemory) {
                    RECT rClient;
                    GetClientRect(hwnd, &rClient);
                    BitBlt(g_windowData->hdcDisplay, 0, 0, rClient.right - rClient.left,
                           rClient.bottom - rClient.top, g_windowData->hdcMemory, 0, 0, SRCCOPY);
                    ValidateRect(hwnd, NULL);
                }
            }
            return 0;

        case WM_CLOSE:
            g_windowData->closeWindow = true;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, iMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow);

int main(int argc, char** argv) {
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow) {
    SetProcessDPIAware();

    WNDCLASSA wndclass;
    wndclass.style = CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc = WndProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = hInstance;
    wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wndclass.lpszMenuName = 0;
    wndclass.lpszClassName = WINDOW_CLASS;
    RegisterClassA(&wndclass);

    RECT rClient;
    SetRect(&rClient, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    AdjustWindowRect(&rClient, WS_OVERLAPPEDWINDOW | WS_VISIBLE, FALSE);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowWidth = rClient.right - rClient.left;
    int windowHeight = rClient.bottom - rClient.top;

    HWND hwnd = CreateWindowA(wndclass.lpszClassName, (char*)(WINDOW_TITLE), WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        (screenWidth / 2) - (windowWidth / 2), (screenHeight / 2) - (windowHeight / 2),
        windowWidth, windowHeight, NULL, NULL, hInstance, 0);

    WindowData windowData = { 0 };
    windowData.hwnd = hwnd;
    windowData.hInstance = hInstance;
    windowData.closeWindow = false;
    windowData.width = WINDOW_WIDTH;
    windowData.height = WINDOW_HEIGHT;

    g_windowData = &windowData;

    {
        windowData.hdcDisplay = GetDC(hwnd);
        windowData.hdcMemory = CreateCompatibleDC(windowData.hdcDisplay);
        windowData.hbmBackBuffer = CreateCompatibleBitmap(windowData.hdcDisplay, WINDOW_WIDTH, WINDOW_HEIGHT);
        windowData.hbmOld = (HBITMAP)SelectObject(windowData.hdcMemory, windowData.hbmBackBuffer);
    }

    windowData.pixels = (unsigned int*)malloc(WINDOW_WIDTH * WINDOW_HEIGHT * sizeof(unsigned int));
    memset(windowData.pixels, 0, WINDOW_WIDTH * WINDOW_HEIGHT * sizeof(unsigned int));

    ShowWindow(hwnd, SW_NORMAL);
    UpdateWindow(hwnd);

    g_userData = Initialize(WINDOW_WIDTH, WINDOW_HEIGHT);

    MSG msg;
    bool quit = false;
    u64 lastTime = platform_get_milliseconds();

    while (!quit) {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (windowData.closeWindow) {
            quit = true;
        }

        if (!quit) {
            u64 currentTime = platform_get_milliseconds();
            float deltaTime = (currentTime - lastTime) / 1000.0f;
            lastTime = currentTime;

            Update(g_userData, deltaTime);

            canvas* toDraw = Render(g_userData);

            u32* doc_canvas_pixels = canvas_get_raw_pixels(toDraw);
            u32 doc_canvas_width = canvas_get_width(toDraw);
            u32 doc_canvas_height = canvas_get_height(toDraw);

            if (doc_canvas_pixels && doc_canvas_width > 0 && doc_canvas_height > 0) {
                float scale = 1.0f;
                if (g_userData->zoom_level == 0) scale = 0.5f;
                else if (g_userData->zoom_level == 1) scale = 1.0f;
                else if (g_userData->zoom_level == 2) scale = 2.0f;

                u32 scaled_width = (u32)(doc_canvas_width * scale);
                u32 scaled_height = (u32)(doc_canvas_height * scale);

                BITMAPINFO bmi = { 0 };
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = windowData.width;
                bmi.bmiHeader.biHeight = -windowData.height;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;

                memset(windowData.pixels, 0, windowData.width * windowData.height * sizeof(u32));

                if (scale == 1.0f) {
                    u32 copy_width = (doc_canvas_width < (u32)windowData.width) ? doc_canvas_width : windowData.width;
                    u32 copy_height = (doc_canvas_height < (u32)windowData.height) ? doc_canvas_height : windowData.height;

                    for (u32 y = 0; y < copy_height; y++) {
                        for (u32 x = 0; x < copy_width; x++) {
                            u32 pixel = doc_canvas_pixels[y * doc_canvas_width + x];
                            u32 r = pixel & 0xFF;
                            u32 g = (pixel >> 8) & 0xFF;
                            u32 b = (pixel >> 16) & 0xFF;
                            u32 a = (pixel >> 24) & 0xFF;
                            windowData.pixels[y * windowData.width + x] = (a << 24) | (r << 16) | (g << 8) | b;
                        }
                    }
                } else {
                    u32 copy_width = (scaled_width < (u32)windowData.width) ? scaled_width : windowData.width;
                    u32 copy_height = (scaled_height < (u32)windowData.height) ? scaled_height : windowData.height;

                    for (u32 y = 0; y < copy_height; y++) {
                        u32 src_y = (u32)(y / scale);
                        if (src_y >= doc_canvas_height) continue;

                        for (u32 x = 0; x < copy_width; x++) {
                            u32 src_x = (u32)(x / scale);
                            if (src_x >= doc_canvas_width) continue;

                            u32 pixel = doc_canvas_pixels[src_y * doc_canvas_width + src_x];
                            u32 r = pixel & 0xFF;
                            u32 g = (pixel >> 8) & 0xFF;
                            u32 b = (pixel >> 16) & 0xFF;
                            u32 a = (pixel >> 24) & 0xFF;
                            windowData.pixels[y * windowData.width + x] = (a << 24) | (r << 16) | (g << 8) | b;
                        }
                    }
                }

                SetDIBits(windowData.hdcMemory, windowData.hbmBackBuffer, 0, windowData.height,
                          windowData.pixels, &bmi, DIB_RGB_COLORS);
            }

            InvalidateRect(hwnd, NULL, FALSE);
            Sleep(1);
        }
    }

    Shutdown(g_userData);

    if (g_clipboardText) {
        u8str_destroy(g_clipboardText);
        g_clipboardText = nullptr;
    }

    if (windowData.pixels) {
        free(windowData.pixels);
    }

    SelectObject(windowData.hdcMemory, windowData.hbmOld);
    DeleteObject(windowData.hbmBackBuffer);
    DeleteDC(windowData.hdcMemory);
    ReleaseDC(hwnd, windowData.hdcDisplay);

    return (int)msg.wParam;
}

void platform_get_window_size(u32* width, u32* height) {
    if (g_windowData && width && height) {
        *width = g_windowData->width;
        *height = g_windowData->height;
    }
}

void platform_clipboard_copy_text(u32_string* content, platform_clipboard_copy_text_callback callback, void* userData) {
    if (g_clipboardText) {
        u8str_destroy(g_clipboardText);
        g_clipboardText = nullptr;
    }

    if (content && u32str_length(content) > 0) {
        g_clipboardText = u32str_to_u8str(content);
    } else {
        g_clipboardText = u8str_create();
    }

    if (OpenClipboard(g_windowData->hwnd)) {
        EmptyClipboard();

        if (g_clipboardText && u8str_size_bytes(g_clipboardText) > 0) {
            u8* utf8Buffer = u8str_getBuffer(g_clipboardText);
            u32 utf8Length = u8str_size_bytes(g_clipboardText);

            int wideLength = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)utf8Buffer, utf8Length, NULL, 0);
            if (wideLength > 0) {
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wideLength + 1) * sizeof(wchar_t));
                if (hMem) {
                    wchar_t* wideBuffer = (wchar_t*)GlobalLock(hMem);
                    if (wideBuffer) {
                        MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)utf8Buffer, utf8Length, wideBuffer, wideLength);
                        wideBuffer[wideLength] = 0;
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                    }
                }
            }
        }

        CloseClipboard();
    }

    if (callback) callback(userData);
}

void platform_clipboard_paste_text(platform_clipboard_paste_text_callback callback, void* userData) {
    u32_string* result = nullptr;

    if (OpenClipboard(g_windowData->hwnd)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* wideBuffer = (wchar_t*)GlobalLock(hData);
            if (wideBuffer) {
                int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideBuffer, -1, NULL, 0, NULL, NULL);
                if (utf8Length > 0) {
                    u8* utf8Buffer = (u8*)malloc(utf8Length);
                    if (utf8Buffer) {
                        WideCharToMultiByte(CP_UTF8, 0, wideBuffer, -1, (LPSTR)utf8Buffer, utf8Length, NULL, NULL);
                        u8_string* temp = u8str_init(utf8Buffer);
                        if (temp) {
                            result = u8str_to_u32str(temp);
                            u8str_destroy(temp);
                        }
                        free(utf8Buffer);
                    }
                }
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }

    if (!result) {
        result = u32str_create();
    }

    if (callback) callback(result, userData);
    if (result) u32str_destroy(result);
}

void platform_exit() {
    if (g_windowData) {
        PostMessage(g_windowData->hwnd, WM_CLOSE, 0, 0);
    }
}

void platform_launch_browser(const char* url) {
    if (!url) return;
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

void platform_open_file(platform_open_file_callback callback, void* userData) {
    if (!callback) return;

    OPENFILENAMEW ofn;
    wchar_t szFile[MAX_PATH] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_windowData ? g_windowData->hwnd : NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.TXT\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        int utf8Length = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, NULL, 0, NULL, NULL);
        char* utf8Path = (char*)malloc(utf8Length);

        if (utf8Path) {
            WideCharToMultiByte(CP_UTF8, 0, szFile, -1, utf8Path, utf8Length, NULL, NULL);

            HANDLE hFile = CreateFileW(szFile, GENERIC_READ, FILE_SHARE_READ, NULL,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD fileSize = GetFileSize(hFile, NULL);
                if (fileSize != INVALID_FILE_SIZE && fileSize > 0) {
                    void* fileData = malloc(fileSize);
                    if (fileData) {
                        DWORD bytesRead;
                        if (ReadFile(hFile, fileData, fileSize, &bytesRead, NULL)) {
                            u32 pathLen = strlen(utf8Path);
                            u32* path_u32 = (u32*)malloc((pathLen + 1) * sizeof(u32));
                            for (u32 i = 0; i <= pathLen; i++) {
                                path_u32[i] = (u32)(unsigned char)utf8Path[i];
                            }
                            u32_string* path_str = u32str_init(path_u32);
                            free(path_u32);

                            callback(path_str, fileData, bytesRead, userData);

                            u32str_destroy(path_str);
                            free(fileData);
                        } else {
                            free(fileData);
                            callback(nullptr, nullptr, 0, userData);
                        }
                    } else {
                        callback(nullptr, nullptr, 0, userData);
                    }
                } else {
                    callback(nullptr, nullptr, 0, userData);
                }
                CloseHandle(hFile);
            } else {
                callback(nullptr, nullptr, 0, userData);
            }
            free(utf8Path);
        } else {
            callback(nullptr, nullptr, 0, userData);
        }
    } else {
        callback(nullptr, nullptr, 0, userData);
    }
}

void platform_modal_yesno(const char* message, platform_modal_yesno_callback callback, void* userData) {
    if (!callback) return;

    int result = MessageBoxA(g_windowData ? g_windowData->hwnd : NULL,
                             message ? message : "Are you sure?",
                             "Confirm",
                             MB_YESNO | MB_ICONQUESTION);

    callback(result == IDYES, userData);
}

void platform_save_file_as(void* fileData, u32 fileSizeBytes, platform_save_file_as_callback callback, void* userData) {
    if (!callback) return;

    OPENFILENAMEW ofn;
    wchar_t szFile[MAX_PATH] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_windowData ? g_windowData->hwnd : NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"All Files\0*.*\0Text Files\0*.TXT\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameW(&ofn)) {
        HANDLE hFile = CreateFileW(szFile, GENERIC_WRITE, 0, NULL,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD bytesWritten = 0;
            BOOL writeSuccess = TRUE;
            if (fileData && fileSizeBytes > 0) {
                writeSuccess = WriteFile(hFile, fileData, fileSizeBytes, &bytesWritten, NULL);
            }
            CloseHandle(hFile);

            if (writeSuccess && (bytesWritten == fileSizeBytes || fileSizeBytes == 0)) {
                int utf8Length = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, NULL, 0, NULL, NULL);
                char* utf8Path = (char*)malloc(utf8Length);
                if (utf8Path) {
                    WideCharToMultiByte(CP_UTF8, 0, szFile, -1, utf8Path, utf8Length, NULL, NULL);
                    u32 pathLen = strlen(utf8Path);
                    u32* path_u32 = (u32*)malloc((pathLen + 1) * sizeof(u32));
                    for (u32 i = 0; i <= pathLen; i++) {
                        path_u32[i] = (u32)(unsigned char)utf8Path[i];
                    }
                    u32_string* path_str = u32str_init(path_u32);
                    free(path_u32);

                    callback(path_str, userData);

                    u32str_destroy(path_str);
                    free(utf8Path);
                } else {
                    callback(nullptr, userData);
                }
            } else {
                callback(nullptr, userData);
            }
        } else {
            callback(nullptr, userData);
        }
    } else {
        callback(nullptr, userData);
    }
}

void platform_write_file(u32_string* filePath, void* fileData, u32 fileSizeBytes, platform_write_file_callback callback, void* userData) {
    if (!callback) return;

    bool success = false;

    if (filePath) {
        u32 path_len = u32str_length(filePath);
        char* utf8Path = (char*)malloc(path_len + 1);
        for (u32 i = 0; i < path_len; i++) {
            utf8Path[i] = (char)u32str_get(filePath, i);
        }
        utf8Path[path_len] = '\0';

        int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, NULL, 0);
        wchar_t* widePath = (wchar_t*)malloc(wideLength * sizeof(wchar_t));
        if (widePath) {
            MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, widePath, wideLength);

            HANDLE hFile = CreateFileW(widePath, GENERIC_WRITE, 0, NULL,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD bytesWritten = 0;
                BOOL writeSuccess = TRUE;
                if (fileData && fileSizeBytes > 0) {
                    writeSuccess = WriteFile(hFile, fileData, fileSizeBytes, &bytesWritten, NULL);
                }
                CloseHandle(hFile);

                if (writeSuccess && (bytesWritten == fileSizeBytes || fileSizeBytes == 0)) {
                    success = true;
                }
            }
            free(widePath);
        }
        free(utf8Path);
    }

    callback(success, userData);
}