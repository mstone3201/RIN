const float SCREEN_QUAD_VERTICES[] = {
	-1.0f, 1.0f, 0.0f, 0.0f, // top left
	-1.0f, -1.0f, 0.0f, 1.0f, // bottom left
	1.0f, 1.0f, 1.0f, 0.0f, // top right
	1.0f, -1.0f, 1.0f, 1.0f // bottom right
};
const uint32_t SCREEN_QUAD_STRIDE = 16; // Stride in bytes
const uint32_t SCREEN_QUAD_SIZE = sizeof(SCREEN_QUAD_VERTICES);
const uint32_t SCREEN_QUAD_VERTEX_COUNT = 4;

const float SKYBOX_VERTICES[] = {
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
const uint32_t SKYBOX_STRIDE = 12; // Stride in bytes
const uint32_t SKYBOX_SIZE = sizeof(SKYBOX_VERTICES);
const uint32_t SKYBOX_VERTEX_COUNT = 14;