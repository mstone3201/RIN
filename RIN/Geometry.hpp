#pragma once

namespace RIN::Geometry {
	constexpr float SCREEN_QUAD_VERTICES[] = {
		-1.0f, 1.0f, 0.0f, 0.0f, // top left
		-1.0f, -1.0f, 0.0f, 1.0f, // bottom left
		1.0f, 1.0f, 1.0f, 0.0f, // top right
		1.0f, -1.0f, 1.0f, 1.0f // bottom right
	};
	constexpr uint32_t SCREEN_QUAD_STRIDE = 16; // Stride in bytes
	constexpr uint32_t SCREEN_QUAD_SIZE = sizeof(SCREEN_QUAD_VERTICES);
	constexpr uint32_t SCREEN_QUAD_VERTEX_COUNT = 4;

	constexpr float SKYBOX_VERTICES[] = {
		1.0f, -1.0f, -1.0f,
		1.0f, 1.0f, -1.0f,
		1.0f, -1.0f, 1.0f,
		1.0f, 1.0f, 1.0f,
		-1.0f, 1.0f, 1.0f,
		1.0f, 1.0f, -1.0f,
		-1.0f, 1.0f, -1.0f,
		1.0f, -1.0f, -1.0f,
		-1.0f, -1.0f, -1.0f,
		1.0f, -1.0f, 1.0f,
		-1.0f, -1.0f, 1.0f,
		-1.0f, 1.0f, 1.0f,
		-1.0f, -1.0f, -1.0f,
		-1.0f, 1.0f, -1.0f
	};
	constexpr uint32_t SKYBOX_STRIDE = 12; // Stride in bytes
	constexpr uint32_t SKYBOX_SIZE = sizeof(SKYBOX_VERTICES);
	constexpr uint32_t SKYBOX_VERTEX_COUNT = 14;
}