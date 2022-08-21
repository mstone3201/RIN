#pragma once

#include <profileapi.h>

class Timer {
	LARGE_INTEGER frequency;
	LARGE_INTEGER startTime;
public:
	Timer() {
		QueryPerformanceFrequency(&frequency);
		start();
	}

	void start() {
		QueryPerformanceCounter(&startTime);
	}

	float elapsedSeconds() {
		LARGE_INTEGER current{};
		QueryPerformanceCounter(&current);
		return (float)(current.QuadPart - startTime.QuadPart) / frequency.QuadPart;
	}
};