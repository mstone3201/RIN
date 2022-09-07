#pragma once

#include <DirectXMath.h>

#include "Renderer.hpp"

#include "Input.hpp"

class FirstPersonCamera {
	RIN::Camera& camera;
	const Input* input;
	// View
	DirectX::XMVECTOR position{};
	float yaw{};
	float pitch{};
	bool viewDirty = false;
public:
	FirstPersonCamera(RIN::Camera& camera, const Input* input);
	FirstPersonCamera(const FirstPersonCamera&) = delete;
	void update(float elapsedSeconds);
	void setPosition(float x, float y, float z);
	void setLookAngle(float yaw, float pitch);
	void setPerspective(float fovY, float aspect, float nearZ, float farZ);
};