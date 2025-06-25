#include <windows.h>
#include <windowsx.h>
#include <shellscalingapi.h>
#include <shellapi.h>
#include <iostream>     
#include <vector>       
#include <string>   
#include <fcntl.h>
#include <io.h>
#include <fstream>
#include <chrono>
#include <thread>
#include "../VisualStudio/Resource.h"

#pragma comment(lib, "OpenGL32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(lib, "Shcore.lib") 

#include "glad.h" 
#include "application.h"

#define WGL_CONTEXT_MAJOR_VERSION_ARB				0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB				0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB					0x2093
#define WGL_CONTEXT_FLAGS_ARB						0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB				0x9126
#define WGL_CONTEXT_DEBUG_BIT_ARB					0x0001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB		0x0002
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB			0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB	0x00000002
#define WGL_DRAW_TO_WINDOW_ARB						0x2001
#define WGL_SUPPORT_OPENGL_ARB						0x2010
#define WGL_DOUBLE_BUFFER_ARB						0x2011
#define WGL_PIXEL_TYPE_ARB							0x2013
#define WGL_COLOR_BITS_ARB							0x2014
#define WGL_DEPTH_BITS_ARB							0x2022
#define WGL_STENCIL_BITS_ARB						0x2023
#define WGL_TYPE_RGBA_ARB							0x202B
#define WGL_SAMPLE_BUFFERS_ARB						0x2041

HWND  g_hWnd = NULL;
HDC   g_hDC = NULL;
HGLRC g_hRC = NULL;
bool  g_running = true;
bool  g_hasTouch = false;
bool  g_isMaximized = false;
bool  g_isDragging = false;
POINT g_dragStartPoint;
RECT  g_windowRectBeforeMaximize;

typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC) (HDC hDC, HGLRC hShareContext, const int* attribList);
typedef BOOL(WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC hdc, const int* piAttribIList, const FLOAT* pfAttribFList, UINT nMaxFormats, int* piFormats, UINT* nNumFormats);

PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
PFNWGLCHOOSEPIXELFORMATARBPROC    wglChoosePixelFormatARB = nullptr;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void CreateOpenGLWindow(HINSTANCE hInstance, int nCmdShow, int width, int height, const wchar_t* title); 
void CleanupOpenGLWindow();
void InitGlad();
void CalculateFrameStats(LARGE_INTEGER& lastTime, float& deltaTime);

void FatalError(const std::string& message) {
	MessageBoxA(NULL, message.c_str(), "Fatal Error", MB_ICONERROR | MB_OK);
	std::cerr << "Fatal Error: " << message << std::endl;
	exit(0);
}


float GetWindowScaleFactor(HWND hWnd) {
	UINT dpi = GetDpiForWindow(hWnd);
	if (dpi == 0) {
		return 1.0f;
	}
	return static_cast<float>(dpi) / 96.0f;
}

void OnApplicationCloseButtonClicked() {
	g_running = false;
	PostQuitMessage(0);
}

void OnApplicationMaximizeButtonClicked() {
	if (!g_isMaximized) {
		// Save current window position and size
		GetWindowRect(g_hWnd, &g_windowRectBeforeMaximize);

		// Get the work area of the monitor containing the window
		HMONITOR hMonitor = MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		if (GetMonitorInfo(hMonitor, &mi)) {
			// Set window to cover the entire work area
			SetWindowPos(g_hWnd, NULL,
				mi.rcWork.left, mi.rcWork.top,
				mi.rcWork.right - mi.rcWork.left,
				mi.rcWork.bottom - mi.rcWork.top,
				SWP_NOZORDER | SWP_FRAMECHANGED);
		}
		g_isMaximized = true;
	}
}

void OnApplicationRestoreButtonClicked() {
	if (g_isMaximized) {
		// Restore to previous position and size
		SetWindowPos(g_hWnd, NULL,
			g_windowRectBeforeMaximize.left,
			g_windowRectBeforeMaximize.top,
			g_windowRectBeforeMaximize.right - g_windowRectBeforeMaximize.left,
			g_windowRectBeforeMaximize.bottom - g_windowRectBeforeMaximize.top,
			SWP_NOZORDER | SWP_FRAMECHANGED);
		g_isMaximized = false;
	}
}

void OnApplicationMinimizeButtonClicked() {
	ShowWindow(g_hWnd, SW_MINIMIZE);
}

void PreciseSleep(double milliseconds) {
	using namespace std::chrono;

	// Sleep most of the time using Sleep()
	if (milliseconds > 5.0) {
		Sleep(static_cast<DWORD>(milliseconds - 5.0));
	}

	// Spin-wait for the remaining time for precision
	auto start = high_resolution_clock::now();
	while (duration<double, std::milli>(high_resolution_clock::now() - start).count() < milliseconds) {
		// Yield to other threads
		std::this_thread::yield();
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	unsigned int windowWidth = 1280;
	unsigned int windowHeight = 720;

	CreateOpenGLWindow(hInstance, nCmdShow, windowWidth, windowHeight, L"Carrot.Code");
	InitGlad(); // This must be after a valid context is made current

	if (RegisterTouchWindow(g_hWnd, 0)) {
		g_hasTouch = true;
		//std::cout << "Touch input registered." << std::endl;
	}
	else {
		g_hasTouch = false;
		DWORD error = GetLastError();
		/*if (error == ERROR_CALL_NOT_IMPLEMENTED) {
			std::cout << "Touch input not supported or available on this system." << std::endl;
		}
		else {
			std::cerr << "Warning: Failed to register for touch input. Error: " << error << std::endl;
		}*/
	}

	RECT clientRect = { 0 };
	if (GetClientRect(g_hWnd, &clientRect)) {
		windowWidth = clientRect.right - clientRect.left;
		windowHeight = clientRect.bottom - clientRect.top;
	}

	glViewport(0, 0, windowWidth, windowHeight);
	if (!Initialize(GetWindowScaleFactor(g_hWnd))) {
		//std::cerr << "User Initialize() returned false. Shutting down." << std::endl;
		g_running = false;
	}


	LARGE_INTEGER lastTime;
	QueryPerformanceCounter(&lastTime);
	float deltaTime = 0.0f;

	MSG msg = { 0 };
	while (g_running) {
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				g_running = false;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (!g_running) break;



		CalculateFrameStats(lastTime, deltaTime);

		RECT currentClientRect;
		GetClientRect(g_hWnd, &currentClientRect);
		unsigned int screenWidth = currentClientRect.right - currentClientRect.left;
		unsigned int screenHeight = currentClientRect.bottom - currentClientRect.top;

		if (screenWidth > 0 && screenHeight > 0) {
			if (!Tick(screenWidth, screenHeight, deltaTime)) {
				//std::cerr << "User Tick() returned false. Shutting down." << std::endl;
				g_running = false;
			}
		}

		SwapBuffers(g_hDC);
		Sleep(1);
		//PreciseSleep(5);
	}

	Shutdown();
	CleanupOpenGLWindow();

	return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	InputEvent event;
	POINT pt;
	event.time = GetTickCount();

	static bool created = false;
	switch (message) {
	case WM_CREATE:
		SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
		break;
	case WM_SHOWWINDOW:
		created = true;
		break;
	case WM_CLOSE:
		g_running = false;
		PostQuitMessage(0);
		return 0; // Indicate we handled it
	case WM_DESTROY:
		// g_hRC might already be cleaned up if WM_CLOSE led to CleanupOpenGLWindow.
		// PostQuitMessage is usually sufficient from WM_CLOSE.
		break;
	case WM_SIZE:
		// The Tick function gets width/height, so glViewport can be there.
		// If you need immediate viewport update on resize:
		//if (g_hRC) { // Check if GL context is initialized
	//		glViewport(0, 0, LOWORD(lParam), HIWORD(lParam));
		//}
		if (created) {
			// TODO: ONLY TICK IF 16 ms passed
			Tick(LOWORD(lParam), HIWORD(lParam), 0.0f);
			SwapBuffers(g_hDC);
		}
		break;
	case WM_ERASEBKGND:
		//SwapBuffers(g_hDC);
		return 1; // Indicate we "handled" background erase (by doing nothing)
	case WM_NCCALCSIZE:
		if (wParam == TRUE) {
			// When wParam is TRUE, lParam points to an NCCALCSIZE_PARAMS struct.
			// By returning 0, you're telling Windows that the client area
			// should occupy the entire area of the window.
			// This effectively removes any space reserved for a standard title bar or borders,
			// making it all available to your client area.
			// You are then fully responsible for drawing any visual elements
			// that look like a title bar or borders.
			return 0;
		}
		// If wParam is FALSE, lParam points to a RECT.
		// In this case, and for any other conditions you don't explicitly handle,
		// let DefWindowProc take care of it.
		break; // This will fall through to your default DefWindowProc call.

	case WM_NCHITTEST:
	{
		// This allows us to handle window dragging and resizing with a custom title bar
		LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);
		if (hit == HTCLIENT) {
			// Get the point in screen coordinates
			POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			ScreenToClient(hWnd, &pt);

			// Get title bar interactive area
			unsigned int titleX, titleY, titleW, titleH;
			GetTitleBarInteractiveRect(&titleX, &titleY, &titleW, &titleH);

			// Check if we're in the title bar area
			if (pt.x >= (int)titleX && pt.x < (int)(titleX + titleW) &&
				pt.y >= (int)titleY && pt.y < (int)(titleY + titleH)) {
				return HTCAPTION;
			}

			// Check for resize borders (8 pixel border)
			RECT rcWindow;
			GetWindowRect(hWnd, &rcWindow);
			const int borderWidth = 8;

			// Convert back to screen coordinates for border testing
			ClientToScreen(hWnd, &pt);

			// Top border
			if (pt.y >= rcWindow.top && pt.y < rcWindow.top + borderWidth) {
				if (pt.x < rcWindow.left + borderWidth)
					return HTTOPLEFT;
				else if (pt.x >= rcWindow.right - borderWidth)
					return HTTOPRIGHT;
				else
					return HTTOP;
			}
			// Bottom border
			else if (pt.y >= rcWindow.bottom - borderWidth && pt.y < rcWindow.bottom) {
				if (pt.x < rcWindow.left + borderWidth)
					return HTBOTTOMLEFT;
				else if (pt.x >= rcWindow.right - borderWidth)
					return HTBOTTOMRIGHT;
				else
					return HTBOTTOM;
			}
			// Left border
			else if (pt.x >= rcWindow.left && pt.x < rcWindow.left + borderWidth) {
				return HTLEFT;
			}
			// Right border
			else if (pt.x >= rcWindow.right - borderWidth && pt.x < rcWindow.right) {
				return HTRIGHT;
			}
		}
		return hit;
	}

	case WM_DROPFILES:
	{
		HDROP hDrop = (HDROP)wParam;
		// Get the number of files dropped. We'll process only the first one.
		UINT nFiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);

		if (nFiles > 0) {
			WCHAR szFilePath[MAX_PATH];

			for (UINT file_index = 0; file_index < nFiles; ++file_index) {
				if (DragQueryFileW(hDrop, file_index, szFilePath, MAX_PATH)) {
					// Read the file content
					std::ifstream file(szFilePath, std::ios::binary | std::ios::ate);
					if (file.is_open()) {
						std::streamsize size = file.tellg();
						file.seekg(0, std::ios::beg);

						std::vector<char> buffer(static_cast<unsigned int>(size));
						if (file.read(buffer.data(), size)) {
							// Convert the wide-char path to a UTF-8 string for our function
							char mbFilePath[MAX_PATH * 4] = { 0 };
							WideCharToMultiByte(CP_UTF8, 0, szFilePath, -1, mbFilePath, sizeof(mbFilePath), NULL, NULL);

							// Call the application-level handler
							OnFileDropped(mbFilePath, buffer.data(), static_cast<unsigned int>(size));
						}
					}
				}
			}
		}
		// Release the memory that the system allocated for the file drop
		DragFinish(hDrop);
		return 0; // Indicate we handled the message
	}

	// Keyboard Input
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) { // Allow ESC to quit
			g_running = false;
			PostQuitMessage(0); // Ensure clean exit
			return 0;
		}
		event.type = InputEvent::Type::KEY_DOWN;
		event.key.keyCode = (unsigned int)wParam;
		event.key.isRepeat = (lParam & 0x40000000) != 0;

		{
			// modifier state
			BYTE kbState[256];
			GetKeyboardState(kbState);

			// translate VK+scancode ➜ UTF-16 code units
			WCHAR buf[4] = { 0 };          // enough for one UTF-16 code point
			int   n = ToUnicodeEx(
				static_cast<UINT>(wParam),          // virtual-key
				static_cast<UINT>((lParam >> 16) & 0xFF),// scan code
				kbState,                             // current keyboard state
				buf,                                 // out buffer
				4,                                   // buffer size
				0,                                   // flags
				GetKeyboardLayout(0));               // active layout

			char32_t cp = 0;
			if (n == 1) {
				cp = buf[0];                       // BMP char
			}
			else if (n == 2 && IS_HIGH_SURROGATE(buf[0]) && IS_LOW_SURROGATE(buf[1])) {
				cp = 0x10000u +                    // decode surrogate pair
					(((buf[0] & 0x3FFu) << 10) | (buf[1] & 0x3FFu));
			}
			event.key.unicode = cp;                   // 0 if key had no character
		}

		event.key.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		event.key.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		event.key.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
		OnInput(event);
		break;
	case WM_KEYUP:
		event.type = InputEvent::Type::KEY_UP;
		event.key.keyCode = (unsigned int)wParam;
		event.key.isRepeat = false;

		{
			// modifier state
			BYTE kbState[256];
			GetKeyboardState(kbState);

			// translate VK+scancode ➜ UTF-16 code units
			WCHAR buf[4] = { 0 };          // enough for one UTF-16 code point
			int   n = ToUnicodeEx(
				static_cast<UINT>(wParam),          // virtual-key
				static_cast<UINT>((lParam >> 16) & 0xFF),// scan code
				kbState,                             // current keyboard state
				buf,                                 // out buffer
				4,                                   // buffer size
				0,                                   // flags
				GetKeyboardLayout(0));               // active layout

			char32_t cp = 0;
			if (n == 1) {
				cp = buf[0];                       // BMP char
			}
			else if (n == 2 && IS_HIGH_SURROGATE(buf[0]) && IS_LOW_SURROGATE(buf[1])) {
				cp = 0x10000u +                    // decode surrogate pair
					(((buf[0] & 0x3FFu) << 10) | (buf[1] & 0x3FFu));
			}
			event.key.unicode = cp;                   // 0 if key had no character
		}

		event.key.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		event.key.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		event.key.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
		OnInput(event);
		break;

		// Mouse Input
	case WM_MOUSEMOVE:
		event.type = InputEvent::Type::MOUSE_MOVE;
		event.mouse.x = (short)(LOWORD(lParam));
		event.mouse.y = (short)(HIWORD(lParam));
		event.mouse.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		event.mouse.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		event.mouse.button = (UINT)wParam;
		OnInput(event);
		break;
	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
		event.type = InputEvent::Type::MOUSE_DOWN;
		event.mouse.x = (short)(LOWORD(lParam));
		event.mouse.y = (short)(HIWORD(lParam));
		event.mouse.button = message == WM_LBUTTONDOWN ? MK_LBUTTON : MK_RBUTTON;
		event.mouse.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		event.mouse.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		OnInput(event);
		SetCapture(hWnd);
		break;
	case WM_LBUTTONUP:
	case WM_RBUTTONUP:
		event.type = InputEvent::Type::MOUSE_UP;
		event.mouse.x = (short)(LOWORD(lParam));
		event.mouse.y = (short)(HIWORD(lParam));
		event.mouse.button = message == WM_LBUTTONDOWN ? MK_LBUTTON : MK_RBUTTON;
		event.mouse.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		event.mouse.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		OnInput(event);
		ReleaseCapture();
		break;
		// Add RBUTTON, MBUTTON handlers similarly
	case WM_MOUSEWHEEL:
		event.type = InputEvent::Type::MOUSE_WHEEL;
		pt.x = (short)(LOWORD(lParam));
		pt.y = (short)(HIWORD(lParam));
		ScreenToClient(hWnd, &pt);
		event.mouse.x = pt.x;
		event.mouse.y = pt.y;
		event.mouse.delta = GET_WHEEL_DELTA_WPARAM(wParam);
		event.mouse.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		event.mouse.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		OnInput(event);
		break;

		// Touch Input
	case WM_TOUCH:
	{
		UINT cInputs = LOWORD(wParam);
		if (cInputs > 0) {
			std::vector<TOUCHINPUT> pInputs(cInputs);
			if (GetTouchInputInfo((HTOUCHINPUT)lParam, cInputs, pInputs.data(), sizeof(TOUCHINPUT))) {
				for (UINT i = 0; i < cInputs; ++i) {
					TOUCHINPUT ti = pInputs[i];
					pt.x = TOUCH_COORD_TO_PIXEL(ti.x);
					pt.y = TOUCH_COORD_TO_PIXEL(ti.y);
					ScreenToClient(hWnd, &pt);

					event.touch.id = ti.dwID;
					event.touch.x = pt.x;
					event.touch.y = pt.y;

					if (ti.dwFlags & TOUCHEVENTF_DOWN) event.type = InputEvent::Type::TOUCH_DOWN;
					else if (ti.dwFlags & TOUCHEVENTF_UP) event.type = InputEvent::Type::TOUCH_UP;
					else if (ti.dwFlags & TOUCHEVENTF_MOVE) event.type = InputEvent::Type::TOUCH_MOVE;
					else continue;
					OnInput(event);
				}
			}
			CloseTouchInputHandle((HTOUCHINPUT)lParam); // CRITICAL
		}
	}
	return 0; // Indicate WM_TOUCH is handled

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	// This path should ideally not be reached if all messages are handled or defaulted.
	// However, to be safe, ensure DefWindowProc is called if a message isn't fully processed.
	return DefWindowProc(hWnd, message, wParam, lParam);
}

void CreateOpenGLWindow(HINSTANCE hInstance, int nCmdShow, int width, int height, const wchar_t* title) {
	// Register dummy window class
	WNDCLASSEX dummy_wc;
	ZeroMemory(&dummy_wc, sizeof(WNDCLASSEX));
	dummy_wc.cbSize = sizeof(WNDCLASSEX);
	dummy_wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	dummy_wc.lpfnWndProc = DefWindowProc; // Simple default proc for dummy
	dummy_wc.hInstance = hInstance;
	dummy_wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	dummy_wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	dummy_wc.lpszClassName = L"Carrot.Code.Dummy";

	if (!RegisterClassEx(&dummy_wc)) {
		FatalError("Failed to register dummy window class.");
	}

	// Create dummy window
	HWND dummy_hwnd = CreateWindowEx(
		0, dummy_wc.lpszClassName, L"Dummy Window", WS_OVERLAPPED,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, hInstance, NULL);

	if (!dummy_hwnd) {
		UnregisterClass(dummy_wc.lpszClassName, hInstance);
		FatalError("Failed to create dummy window.");
	}

	HDC dummy_hdc = GetDC(dummy_hwnd);
	if (!dummy_hdc) {
		DestroyWindow(dummy_hwnd);
		UnregisterClass(dummy_wc.lpszClassName, hInstance);
		FatalError("Failed to get dummy window DC.");
	}

	// Pixel format for the dummy window
	PIXELFORMATDESCRIPTOR pfd_dummy = {
		sizeof(PIXELFORMATDESCRIPTOR), 1,
		PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
		PFD_TYPE_RGBA,
		32,                     // 24-bit color depth
		0, 0, 0, 0, 0, 0,       // color bits ignored
		0,                      // no alpha buffer
		0,                      // shift bit ignored
		0,                      // no accumulation buffer
		0, 0, 0, 0,             // accum bits ignored
		24,                     // 32-bit z-buffer
		8,                      // no stencil buffer
		0,                      // no auxiliary buffer
		PFD_MAIN_PLANE, 0, 0, 0, 0
	};

	int dummy_pixel_format = ChoosePixelFormat(dummy_hdc, &pfd_dummy);
	if (!dummy_pixel_format) {
		ReleaseDC(dummy_hwnd, dummy_hdc);
		DestroyWindow(dummy_hwnd);
		UnregisterClass(dummy_wc.lpszClassName, hInstance);
		FatalError("ChoosePixelFormat failed for dummy window.");
	}

	if (!SetPixelFormat(dummy_hdc, dummy_pixel_format, &pfd_dummy)) {
		ReleaseDC(dummy_hwnd, dummy_hdc);
		DestroyWindow(dummy_hwnd);
		UnregisterClass(dummy_wc.lpszClassName, hInstance);
		FatalError("SetPixelFormat failed for dummy window.");
	}

	HGLRC dummy_context = wglCreateContext(dummy_hdc);
	if (!dummy_context) {
		ReleaseDC(dummy_hwnd, dummy_hdc);
		DestroyWindow(dummy_hwnd);
		UnregisterClass(dummy_wc.lpszClassName, hInstance);
		FatalError("wglCreateContext failed for dummy context.");
	}

	if (!wglMakeCurrent(dummy_hdc, dummy_context)) {
		wglDeleteContext(dummy_context);
		ReleaseDC(dummy_hwnd, dummy_hdc);
		DestroyWindow(dummy_hwnd);
		UnregisterClass(dummy_wc.lpszClassName, hInstance);
		FatalError("wglMakeCurrent failed for dummy context.");
	}

	// Load WGL ARB functions
	wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
	wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

	if (!wglChoosePixelFormatARB || !wglCreateContextAttribsARB) {
		wglMakeCurrent(NULL, NULL);
		wglDeleteContext(dummy_context);
		ReleaseDC(dummy_hwnd, dummy_hdc);
		DestroyWindow(dummy_hwnd);
		UnregisterClass(dummy_wc.lpszClassName, hInstance);
		FatalError("Failed to load WGL ARB functions (wglChoosePixelFormatARB or wglCreateContextAttribsARB).\n"
			"Ensure graphics drivers are up to date and support OpenGL 3.3+ WGL extensions.");
	}

	float dpi = GetWindowScaleFactor(dummy_hwnd);
	int windowPosX = CW_USEDEFAULT;
	int windowPosY = CW_USEDEFAULT;
	int calculatedWindowWidth = width;
	int calculatedWindowHeight = height;

	{ // Handle DPI sizing
		// --- Get Primary Monitor Info and DPI ---
		POINT ptZero = { 0, 0 };
		HMONITOR hMonitor = MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
		UINT monitorDpiX = 96, monitorDpiY = 96; // Default to 96 DPI

		if (hMonitor) {
			MONITORINFOEX monitorInfo;
			monitorInfo.cbSize = sizeof(MONITORINFOEX);
			if (GetMonitorInfo(hMonitor, &monitorInfo)) {
				// GetDpiForMonitor requires Windows 10, version 1607 or later
				HRESULT hr = GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &monitorDpiX, &monitorDpiY);
				if (FAILED(hr)) {
					// Fallback for older systems or if GetDpiForMonitor fails
					HDC screenDC = GetDC(NULL);
					monitorDpiX = GetDeviceCaps(screenDC, LOGPIXELSX);
					ReleaseDC(NULL, screenDC);
					if (monitorDpiX == 0) monitorDpiX = 96; // Further fallback
				}
			}
			else {
				FatalError("Failed to get primary monitor info.");
				wglMakeCurrent(NULL, NULL);
				wglDeleteContext(dummy_context);
				ReleaseDC(dummy_hwnd, dummy_hdc);
				DestroyWindow(dummy_hwnd);
				UnregisterClass(dummy_wc.lpszClassName, hInstance);
				return;
			}
		}
		else {
			// Fallback if no primary monitor found (highly unlikely)
			HDC screenDC = GetDC(NULL);
			monitorDpiX = GetDeviceCaps(screenDC, LOGPIXELSX);
			ReleaseDC(NULL, screenDC);
			if (monitorDpiX == 0) monitorDpiX = 96;
		}

		// --- Calculate Window Size and Position ---
		RECT windowRect = { 0, 0, static_cast<LONG>(width * dpi), static_cast<LONG>(height * dpi) };
		DWORD dwStyle = WS_POPUP | WS_THICKFRAME | WS_VISIBLE;
		DWORD dwExStyle = WS_EX_APPWINDOW;

		// AdjustWindowRectExForDpi calculates the required window rectangle
		if (!AdjustWindowRectExForDpi(&windowRect, dwStyle, FALSE, dwExStyle, monitorDpiX)) {
			FatalError("AdjustWindowRectExForDpi failed.");
			wglMakeCurrent(NULL, NULL);
			wglDeleteContext(dummy_context);
			ReleaseDC(dummy_hwnd, dummy_hdc);
			DestroyWindow(dummy_hwnd);
			UnregisterClass(dummy_wc.lpszClassName, hInstance);
			return;
		}

		calculatedWindowWidth = windowRect.right - windowRect.left;
		calculatedWindowHeight = windowRect.bottom - windowRect.top;

		// Center the window on the primary monitor's work area
		MONITORINFOEX primaryMonitorInfo;
		primaryMonitorInfo.cbSize = sizeof(MONITORINFOEX);
		if (!GetMonitorInfo(MonitorFromPoint({ 0,0 }, MONITOR_DEFAULTTOPRIMARY), &primaryMonitorInfo)) {
			FatalError("Failed to get primary monitor info for centering.");
			wglMakeCurrent(NULL, NULL);
			wglDeleteContext(dummy_context);
			ReleaseDC(dummy_hwnd, dummy_hdc);
			DestroyWindow(dummy_hwnd);
			UnregisterClass(dummy_wc.lpszClassName, hInstance);
			return;
		}

		LONG workAreaWidth = primaryMonitorInfo.rcWork.right - primaryMonitorInfo.rcWork.left;
		LONG workAreaHeight = primaryMonitorInfo.rcWork.bottom - primaryMonitorInfo.rcWork.top;

		windowPosX = primaryMonitorInfo.rcWork.left + (workAreaWidth - calculatedWindowWidth) / 2;
		windowPosY = primaryMonitorInfo.rcWork.top + (workAreaHeight - calculatedWindowHeight) / 2;

		// Ensure the window is not positioned off-screen
		if (windowPosX < primaryMonitorInfo.rcWork.left) windowPosX = primaryMonitorInfo.rcWork.left;
		if (windowPosY < primaryMonitorInfo.rcWork.top) windowPosY = primaryMonitorInfo.rcWork.top;
	}

	// Clean up dummy window resources
	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(dummy_context);
	ReleaseDC(dummy_hwnd, dummy_hdc);
	DestroyWindow(dummy_hwnd);
	UnregisterClass(dummy_wc.lpszClassName, hInstance);

	// Register main window class
	WNDCLASSEX wc;
	ZeroMemory(&wc, sizeof(WNDCLASSEX));
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = L"Carrot.Code.OpenGL33";
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CARROTCODE));
	wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_CARROTCODE));

	if (!RegisterClassEx(&wc)) {
		FatalError("Failed to register window class.");
	}

	// --- Create the main window ---
	RECT rect = { 0, 0, width, height };
	AdjustWindowRect(&rect, WS_POPUP | WS_THICKFRAME, FALSE);

	g_hWnd = CreateWindowEx(
		WS_EX_APPWINDOW,
		wc.lpszClassName, title,
		WS_POPUP | WS_THICKFRAME | WS_VISIBLE,
		windowPosX, windowPosY,
		calculatedWindowWidth, calculatedWindowHeight,
		NULL, NULL, hInstance, NULL);

	if (!g_hWnd) {
		UnregisterClass(wc.lpszClassName, hInstance);
		FatalError("Failed to create main window.");
	}

	DragAcceptFiles(g_hWnd, TRUE);

	g_hDC = GetDC(g_hWnd);
	if (!g_hDC) {
		DestroyWindow(g_hWnd);
		UnregisterClass(wc.lpszClassName, hInstance);
		FatalError("Failed to get main window DC.");
	}

	// Get a modern OpenGL pixel format
	const int pixel_attribs[] = {
		WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
		WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
		WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
		WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
		WGL_COLOR_BITS_ARB, 32,
		WGL_DEPTH_BITS_ARB, 24,
		WGL_STENCIL_BITS_ARB, 8,
		0
	};

	int pixel_format_id;
	UINT num_formats;
	if (!wglChoosePixelFormatARB(g_hDC, pixel_attribs, NULL, 1, &pixel_format_id, &num_formats) || num_formats == 0) {
		ReleaseDC(g_hWnd, g_hDC);
		DestroyWindow(g_hWnd);
		UnregisterClass(wc.lpszClassName, hInstance);
		FatalError("wglChoosePixelFormatARB failed for main window. Check driver support for attributes.");
	}

	PIXELFORMATDESCRIPTOR pfd_main;
	ZeroMemory(&pfd_main, sizeof(PIXELFORMATDESCRIPTOR));
	pfd_main.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	if (!DescribePixelFormat(g_hDC, pixel_format_id, sizeof(PIXELFORMATDESCRIPTOR), &pfd_main)) {
		ReleaseDC(g_hWnd, g_hDC);
		DestroyWindow(g_hWnd);
		UnregisterClass(wc.lpszClassName, hInstance);
		FatalError("DescribePixelFormat failed for main window's pixel format.");
	}

	if (!SetPixelFormat(g_hDC, pixel_format_id, &pfd_main)) {
		ReleaseDC(g_hWnd, g_hDC);
		DestroyWindow(g_hWnd);
		UnregisterClass(wc.lpszClassName, hInstance);
		FatalError("SetPixelFormat failed for main window.");
	}

	// Create modern OpenGL context (3.3 Core, Forward-Compatible)
	const int context_attribs[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
		WGL_CONTEXT_MINOR_VERSION_ARB, 3,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
		WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
		0
	};

	g_hRC = wglCreateContextAttribsARB(g_hDC, NULL, context_attribs);
	if (!g_hRC) {
		DWORD error = GetLastError();
		wchar_t buffer[256];
		wsprintfW(buffer, L"wglCreateContextAttribsARB failed to create OpenGL 3.3 Core context. Error: %lu\n"
			L"Ensure graphics drivers support OpenGL 3.3 Core Profile.", error);
		ReleaseDC(g_hWnd, g_hDC);
		DestroyWindow(g_hWnd);
		UnregisterClass(wc.lpszClassName, hInstance);
		FatalError(std::string("OpenGL Context Creation Error (wglCreateContextAttribsARB): ") + std::to_string(error));
	}

	if (!wglMakeCurrent(g_hDC, g_hRC)) {
		wglDeleteContext(g_hRC);
		ReleaseDC(g_hWnd, g_hDC);
		DestroyWindow(g_hWnd);
		UnregisterClass(wc.lpszClassName, hInstance);
		FatalError("wglMakeCurrent failed for main GL context.");
	}

	ShowWindow(g_hWnd, nCmdShow);
	UpdateWindow(g_hWnd);
	//std::cout << "Main window and OpenGL context initialized." << std::endl;
}

void CleanupOpenGLWindow() {
	if (g_hRC) {
		wglMakeCurrent(NULL, NULL); // Release context from current thread
		wglDeleteContext(g_hRC);
		g_hRC = NULL;
	}
	if (g_hDC && g_hWnd) { // Only release DC if window handle is valid
		ReleaseDC(g_hWnd, g_hDC);
		g_hDC = NULL;
	}
	if (g_hWnd) {
		if (g_hasTouch) { // Unregister touch only if registered and window is valid
			UnregisterTouchWindow(g_hWnd);
			g_hasTouch = false;
		}
		DestroyWindow(g_hWnd);
		g_hWnd = NULL;
	}
	UnregisterClass(L"Carrot.Code.OpenGL33", GetModuleHandle(NULL)); // Use the correct class name
	//std::cout << "Window and OpenGL context cleaned up." << std::endl;
}

void InitGlad() {
	int version = gladLoadGL();
	if (version == 0) { // gladLoadGL returns 0 on error.
		FatalError("Failed to initialize GLAD (gladLoadGL returned 0).\n"
			"Ensure an OpenGL context is current and GLAD files are correctly set up.");
	}

#if 0
	if (!GLAD_GL_VERSION_3_3) {
		std::cerr << "Warning: GLAD reports OpenGL 3.3 not fully available, but GLAD loaded." << std::endl;
	}
	else {
		std::cout << "OpenGL 3.3 Core Profile functions should be available via GLAD." << std::endl;
	}
	std::cout << "Actual GL Version (glGetString): " << (const char*)glGetString(GL_VERSION) << std::endl;
	std::cout << "GLSL Version: " << (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
#endif
}


void CalculateFrameStats(LARGE_INTEGER& lastTime, float& deltaTime) {
	LARGE_INTEGER currentTime, frequency;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&currentTime);
	deltaTime = (float)(currentTime.QuadPart - lastTime.QuadPart) / frequency.QuadPart;
	lastTime = currentTime;
	if (deltaTime < 0) deltaTime = 0; // Clamp in case of timer issues
}