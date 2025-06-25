#pragma once

struct InputEvent {
	enum class Type {
		KEY_DOWN, KEY_UP,
		MOUSE_MOVE, MOUSE_DOWN, MOUSE_UP, MOUSE_WHEEL,
		TOUCH_DOWN, TOUCH_UP, TOUCH_MOVE
	};

	Type type;
	unsigned long time; // DWORD

	union {
		struct {
			unsigned int keyCode; // VK codes from windows
			char32_t unicode;
			bool ctrl;
			bool shift;
			bool isRepeat;
			bool alt;
		} key;
		struct {
			unsigned int button; // VK_MIDDLE, not 0, 1 or 2
			bool ctrl;
			bool shift;
			int delta;
			int x;
			int y;
		} mouse;
		struct {
			int id; 
			int x; 
			int y;
		} touch;
	};
};

bool Initialize(float dpi);
bool Tick(unsigned int screenWidth, unsigned int screenHeight, float deltaTime);
void Shutdown();
void OnInput(const InputEvent& event);
void OnFileDropped(const char* path, const void* data, unsigned int bytes);

void GetTitleBarInteractiveRect(unsigned int* outX, unsigned int* outY, unsigned int* outW, unsigned int* outH); 