#pragma once

#define NOMINMAX

#include <cstdint>
#include <Windows.h>
#include <bitset>

enum class KEYBIND : uint32_t {
	CAMERA_ROTATE,
	CAMERA_PAN
};

constexpr uint32_t KEYBIND_COUNT = 2;

class Input {
	HWND hwnd;
	uint8_t keybinds[KEYBIND_COUNT];
	std::bitset<256> keys;
	float verticalScroll;
	float horizontalScroll;
	long mdx;
	long mdy;
	int mx;
	int my;
public:
	Input(HWND hwnd);
	Input(const Input&) = delete;
	void update(RAWMOUSE* mouse);
	void update(RAWKEYBOARD* keyboard);
	void updateMousePos(int, int);
	void reset();
	bool isKey(KEYBIND keybind) const;
	float getVerticalScroll() const;
	float getHorizontalScroll() const;
	long getMouseDX() const;
	long getMouseDY() const;
	int getMouseX() const;
	int getMouseY() const;
};