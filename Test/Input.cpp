#include <stdexcept>

#include "Input.hpp"

// Need to include Windows.h before this
#include <hidusage.h>

Input::Input(HWND hwnd) :
	hwnd(hwnd),
	keybinds{
		VK_LBUTTON,
		VK_RBUTTON
	},
	verticalScroll(0.0f),
	horizontalScroll(0.0f),
	mdx(0),
	mdy(0),
	mx(0),
	my(0)
{
	RAWINPUTDEVICE rid[2]{};
	rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
	rid[0].usUsage = HID_USAGE_GENERIC_MOUSE;
	rid[0].dwFlags = 0;
	rid[0].hwndTarget = hwnd;
	rid[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
	rid[1].usUsage = HID_USAGE_GENERIC_KEYBOARD;
	rid[1].dwFlags = 0;
	rid[1].hwndTarget = hwnd;

	if(!RegisterRawInputDevices(rid, _countof(rid), sizeof(RAWINPUTDEVICE)))
		throw std::runtime_error("Failed to register raw input devices");
}

void Input::update(RAWMOUSE* mouse) {
	auto buttonFlags = mouse->usButtonFlags;
	if(buttonFlags & RI_MOUSE_BUTTON_1_DOWN) keys.set(VK_LBUTTON);
	else if(buttonFlags & RI_MOUSE_BUTTON_1_UP) keys.reset(VK_LBUTTON);
	if(buttonFlags & RI_MOUSE_BUTTON_2_DOWN) keys.set(VK_RBUTTON);
	else if(buttonFlags & RI_MOUSE_BUTTON_2_UP) keys.reset(VK_RBUTTON);
	if(buttonFlags & RI_MOUSE_BUTTON_3_DOWN) keys.set(VK_MBUTTON);
	else if(buttonFlags & RI_MOUSE_BUTTON_3_UP) keys.reset(VK_MBUTTON);
	if(buttonFlags & RI_MOUSE_BUTTON_4_DOWN) keys.set(VK_XBUTTON1);
	else if(buttonFlags & RI_MOUSE_BUTTON_4_UP) keys.reset(VK_XBUTTON1);
	if(buttonFlags & RI_MOUSE_BUTTON_5_DOWN) keys.set(VK_XBUTTON2);
	else if(buttonFlags & RI_MOUSE_BUTTON_5_UP) keys.reset(VK_XBUTTON2);

	if(buttonFlags & RI_MOUSE_WHEEL) verticalScroll += (float)(short)mouse->usButtonData / WHEEL_DELTA;
	if(buttonFlags & RI_MOUSE_HWHEEL) horizontalScroll += (float)(short)mouse->usButtonData / WHEEL_DELTA;

	if(mouse->usFlags == MOUSE_MOVE_RELATIVE) {
		mdx += mouse->lLastX;
		mdy += mouse->lLastY;
	}
}

void Input::update(RAWKEYBOARD* keyboard) {
	if(keyboard->Flags == RI_KEY_MAKE)
		keys.set(keyboard->VKey);
	else if(keyboard->Flags == RI_KEY_BREAK)
		keys.reset(keyboard->VKey);
}

void Input::updateMousePos(int x, int y) {
	mx = x;
	my = y;
}

void Input::reset() {
	verticalScroll = horizontalScroll = 0.0f;
	mdx = mdy = 0;
}

bool Input::isKey(KEYBIND keybind) const {
	return keys.test(keybinds[static_cast<uint32_t>(keybind)]);
}

float Input::getVerticalScroll() const {
	return verticalScroll;
}

float Input::getHorizontalScroll() const {
	return horizontalScroll;
}

long Input::getMouseDX() const {
	return mdx;
}

long Input::getMouseDY() const {
	return mdy;
}

int Input::getMouseX() const {
	return mx;
}

int Input::getMouseY() const {
	return my;
}