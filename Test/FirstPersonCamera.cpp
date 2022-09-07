#include "FirstPersonCamera.hpp"

constexpr float maxPitch = DirectX::XM_PIDIV2 - 0.0000001f;
constexpr float minPitch = -maxPitch;
constexpr float rotSense = DirectX::XM_PI / 2000.0f;
constexpr float moveSpeed = 10.0f;

FirstPersonCamera::FirstPersonCamera(RIN::Camera& camera, const Input* input) :
	camera(camera),
	input(input)
{}

void FirstPersonCamera::update(float elapsedSeconds) {
	// Rotate
	long mdx = input->getMouseDX();
	long mdy = input->getMouseDY();

	if(mdx || mdy) {
		if(input->isKey(KEYBIND::CAMERA_ROTATE)) {
			yaw = fmodf(yaw - rotSense * mdx, DirectX::XM_2PI);
			pitch = std::min(std::max(pitch + rotSense * mdy, minPitch), maxPitch);

			viewDirty = true;
		}
	}

	float moveX = 0.0f;
	float moveY = 0.0f;
	float moveZ = 0.0f;

	if(input->isKey(KEYBIND::CAMERA_MOVE_RIGHT)) {
		moveX += moveSpeed * elapsedSeconds;

		viewDirty = true;
	}
	if(input->isKey(KEYBIND::CAMERA_MOVE_LEFT)) {
		moveX -= moveSpeed * elapsedSeconds;

		viewDirty = true;
	}
	if(input->isKey(KEYBIND::CAMERA_MOVE_FRONT)) {
		moveY += moveSpeed * elapsedSeconds;

		viewDirty = true;
	}
	if(input->isKey(KEYBIND::CAMERA_MOVE_BACK)) {
		moveY -= moveSpeed * elapsedSeconds;

		viewDirty = true;
	}
	if(input->isKey(KEYBIND::CAMERA_MOVE_UP)) {
		moveZ += moveSpeed * elapsedSeconds;

		viewDirty = true;
	}
	if(input->isKey(KEYBIND::CAMERA_MOVE_DOWN)) {
		moveZ -= moveSpeed * elapsedSeconds;

		viewDirty = true;
	}

	// Matrix calculations
	if(viewDirty) {
		float xyLookX = cosf(yaw);
		float xyLookY = sinf(yaw);
		float xyLen = cosf(pitch);
		DirectX::XMVECTOR negLook{ xyLen * xyLookX, xyLen * xyLookY, sinf(pitch) }; // (normalized)
		DirectX::XMVECTOR right{ -xyLookY, xyLookX, 0.0f }; // right = normalize(<0.0, 0.0, 1.0> x negLook)
		DirectX::XMVECTOR up = DirectX::XMVector3Cross(negLook, right); // up = negLook x right (already normalized)

		// position += right * moveX
		position = DirectX::XMVectorMultiplyAdd(right, { moveX, moveX, moveX }, position);
		// position -= negLook * moveY
		position = DirectX::XMVectorNegativeMultiplySubtract(negLook, { moveY, moveY, moveY }, position);
		// position.z += moveZ
		position = DirectX::XMVectorAdd(position, { 0.0f, 0.0f, moveZ });

		DirectX::XMVECTOR negPos = DirectX::XMVectorNegate(position);

		DirectX::XMVECTOR negRightDot = DirectX::XMVector3Dot(right, negPos);
		DirectX::XMVECTOR negUpDot = DirectX::XMVector3Dot(up, negPos);
		DirectX::XMVECTOR lookDot = DirectX::XMVector3Dot(negLook, negPos);

		/*
		Row Vector Right-Handed View Matrix:
		|          right.x |          up.x |        -look.x | 0.0 |
		|          right.y |          up.y |        -look.y | 0.0 |
		|          right.z |          up.z |        -look.z | 0.0 |
		| -dot(right, pos) | -dot(up, pos) | dot(look, pos) | 1.0 |
		*/
		DirectX::XMMATRIX viewMatrix{};
		// VectorSelect(v1, v2, select) { v1 & ~select | v2 & select; }
		viewMatrix.r[0] = DirectX::XMVectorSelect(negRightDot, right, DirectX::g_XMSelect1110.v);
		viewMatrix.r[1] = DirectX::XMVectorSelect(negUpDot, up, DirectX::g_XMSelect1110.v);
		viewMatrix.r[2] = DirectX::XMVectorSelect(lookDot, negLook, DirectX::g_XMSelect1110.v);
		viewMatrix.r[3] = DirectX::g_XMIdentityR3.v;

		viewMatrix = DirectX::XMMatrixTranspose(viewMatrix);

		camera.setViewMatrix(viewMatrix);

		viewDirty = false;
	}
}

void FirstPersonCamera::setPosition(float x, float y, float z) {
	DirectX::XMFLOAT3A pos(x, y, z);
	position = DirectX::XMLoadFloat3A(&pos);

	viewDirty = true;
}

void FirstPersonCamera::setLookAngle(float y, float p) {
	yaw = y;
	pitch = p;

	viewDirty = true;
}

void FirstPersonCamera::setPerspective(float fovY, float aspect, float nearZ, float farZ) {
	camera.setPerspective(fovY, aspect, nearZ, farZ);
}