#include "ThirdPersonCamera.hpp"

constexpr float maxPitch = DirectX::XM_PIDIV2 - 0.0000001f;
constexpr float minPitch = -maxPitch;
constexpr float rotSense = DirectX::XM_PI / 2000.0f;
constexpr float panSense = 1.0f / 500.0f;
constexpr float invScrollTime = 1.0f / 0.075f;
constexpr float scrollEpsilon = 0.00001f;

ThirdPersonCamera::ThirdPersonCamera(RIN::Camera& camera, const Input* input) :
	camera(camera),
	input(input),
	viewDirty(false)
{}

void ThirdPersonCamera::update(float elapsedSeconds) {
	// Rotate and pan
	long mdx = input->getMouseDX();
	long mdy = input->getMouseDY();

	float panX = 0.0f;
	float panY = 0.0f;

	if(mdx || mdy) {
		if(input->isKey(KEYBIND::CAMERA_ROTATE)) {
			yaw = fmodf(yaw - rotSense * mdx, DirectX::XM_2PI);
			pitch = std::min(std::max(pitch + rotSense * mdy, minPitch), maxPitch);

			viewDirty = true;
		} else if(input->isKey(KEYBIND::CAMERA_PAN)) {
			panX = panSense * -mdx;
			panY = panSense * mdy;

			viewDirty = true;
		}
	}

	// Zoom
	float scroll = input->getVerticalScroll();
	if(scroll != 0.0f) targetArmLength = std::max(targetArmLength - scroll, 0.0f);

	if(armLength != targetArmLength) {
		float diff = targetArmLength - armLength;
		if(fabsf(diff) < scrollEpsilon || elapsedSeconds * invScrollTime >= 1.0f) armLength = targetArmLength;
		else armLength += diff * elapsedSeconds * invScrollTime;

		viewDirty = true;
	}

	// Matrix calculations
	if(viewDirty) {
		// Yaw and pitch are from the pov of the focus, so the
		// vector created points from the focus to the camera
		float xyLookX = cosf(yaw);
		float xyLookY = sinf(yaw);
		float xyLen = cosf(pitch);
		DirectX::XMVECTOR negLook{ xyLen * xyLookX, xyLen * xyLookY, sinf(pitch) }; // (normalized)
		DirectX::XMVECTOR right{ -xyLookY, xyLookX, 0.0f }; // right = normalize(<0.0, 0.0, 1.0> x negLook)
		DirectX::XMVECTOR up = DirectX::XMVector3Cross(negLook, right); // up = negLook x right (already normalized)

		// focus += right * panX
		focus = DirectX::XMVectorMultiplyAdd(right, { panX, panX, panX }, focus);
		// focus += up * panY;
		focus = DirectX::XMVectorMultiplyAdd(up, { panY, panY, panY }, focus);

		// pos = focus + negLook * armLength
		DirectX::XMVECTOR pos = DirectX::XMVectorMultiplyAdd(negLook, { armLength, armLength, armLength }, focus);
		DirectX::XMVECTOR negPos = DirectX::XMVectorNegate(pos);

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

void ThirdPersonCamera::setFocus(float x, float y, float z) {
	DirectX::XMFLOAT3A pos(x, y, z);
	focus = DirectX::XMLoadFloat3A(&pos);

	viewDirty = true;
}

void ThirdPersonCamera::setLookAngle(float y, float p) {
	yaw = y;
	pitch = p;

	viewDirty = true;
}

void ThirdPersonCamera::setArmLength(float l) {
	armLength = targetArmLength = l;

	viewDirty = true;
}

void ThirdPersonCamera::setPerspective(float fovY, float aspect, float nearZ, float farZ) {
	camera.setPerspective(fovY, aspect, nearZ, farZ);
}