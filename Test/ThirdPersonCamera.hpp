#pragma once

#include <DirectXMath.h>

#include "Renderer.hpp"

#include "Input.hpp"

class ThirdPersonCamera {
	RIN::Camera& camera;
	const Input* input;
	// View
	DirectX::XMVECTOR focus{};
	float yaw{};
	float pitch{};
	float armLength{};
	float targetArmLength{};
	bool viewDirty;
public:
	ThirdPersonCamera(RIN::Camera& camera, const Input* input);
	ThirdPersonCamera(const ThirdPersonCamera&) = delete;
	void update(float elapsedSeconds);
	void setFocus(float x, float y, float z);
	void setLookAngle(float yaw, float pitch);
	void setArmLength(float armLength);
	// aspect = width / height
	void setPerspective(float fovY, float aspect, float nearZ, float farZ);
};