#pragma once

#include <DirectXMath.h>

#include "Debug.hpp"

namespace RIN {
	/*
	This guarantees certain assumptions such as
	frustum symmetry and right-handedness
	*/
	class Camera {
	protected:
		alignas(16) DirectX::XMMATRIX viewMatrix = DirectX::XMMatrixIdentity();
		alignas(16) DirectX::XMMATRIX invViewMatrix = DirectX::XMMatrixIdentity();
		alignas(16) DirectX::XMMATRIX projMatrix = DirectX::XMMatrixIdentity();
		alignas(16) DirectX::XMMATRIX invProjMatrix = DirectX::XMMatrixIdentity();

		Camera() = default;
		Camera(const Camera&) = delete;
		virtual ~Camera() = default;
	public:
		virtual void XM_CALLCONV setViewMatrix(DirectX::FXMMATRIX M) {
		#ifdef RIN_DEBUG
			// View matrix validation
			DirectX::XMMATRIX viewMatrixT = DirectX::XMMatrixTranspose(M);

			if(!DirectX::XMVector4Equal(viewMatrixT.r[3], { 0.0f, 0.0f, 0.0f, 1.0f }))
				RIN_ERROR("Column 4 of view matrix is not <0.0, 0.0, 0.0, 1.0>");

			float len2;
			DirectX::XMStoreFloat(&len2, DirectX::XMVector3LengthSq(viewMatrixT.r[0]));
			if(!DirectX::XMScalarNearEqual(len2, 1.0f, 0.00001f))
				RIN_ERROR("Column 1 (right) of view matrix is not normalized");
			DirectX::XMStoreFloat(&len2, DirectX::XMVector3LengthSq(viewMatrixT.r[1]));
			if(!DirectX::XMScalarNearEqual(len2, 1.0f, 0.00001f))
				RIN_ERROR("Column 2 (up) of view matrix is not normalized");
			DirectX::XMStoreFloat(&len2, DirectX::XMVector3LengthSq(viewMatrixT.r[2]));
			if(!DirectX::XMScalarNearEqual(len2, 1.0f, 0.00001f))
				RIN_ERROR("Column 3 (look) of view matrix is not normalized");

			DirectX::XMVECTOR epsilon{ 0.00001f, 0.00001f, 0.00001f };
			DirectX::XMVECTOR cross = DirectX::XMVector3Cross(viewMatrixT.r[0], viewMatrixT.r[1]); // right x up = look
			if(!DirectX::XMVector3NearEqual(cross, viewMatrixT.r[2], epsilon))
				RIN_ERROR("Column 3 (look) of view matrix does not equal column 1 (right) x column 2 (up)");
			cross = DirectX::XMVector3Cross(viewMatrixT.r[1], viewMatrixT.r[2]);
			if(!DirectX::XMVector3NearEqual(cross, viewMatrixT.r[0], epsilon)) // up x look = right
				RIN_ERROR("Column 1 (right) of view matrix does not equal column 2 (up) x column 3 (look)");
		#endif
			/*
			Row Vector Right-Handed View Matrix:
			|          right.x |          up.x |        -look.x | 0.0 |
			|          right.y |          up.y |        -look.y | 0.0 |
			|          right.z |          up.z |        -look.z | 0.0 |
			| -dot(right, pos) | -dot(up, pos) | dot(look, pos) | 1.0 |
			*/
			viewMatrix = M;
			invViewMatrix = DirectX::XMMatrixInverse(nullptr, M);
		}

		virtual void XM_CALLCONV setViewMatrix(DirectX::FXMVECTOR eye, DirectX::FXMVECTOR focus, DirectX::FXMVECTOR up) {
			viewMatrix = DirectX::XMMatrixLookAtRH(eye, focus, up);
			invViewMatrix = DirectX::XMMatrixInverse(nullptr, viewMatrix);
		}

		// aspect = width / height
		virtual void setPerspective(float fovY, float aspect, float nearZ, float farZ) {
			/*
			Row Vector Right-Handed Projection Matrix:
			| 1 / (ar * tan(fov / 2)) |              0.0 |                       0.0 |  0.0 |
			|                     0.0 | 1 / tan(fov / 2) |                       0.0 |  0.0 |
			|                     0.0 |              0.0 |        far / (near - far) | -1.0 |
			|                     0.0 |              0.0 | near * far / (near - far) |  0.0 |
			*/
			projMatrix = DirectX::XMMatrixPerspectiveFovRH(fovY, aspect, nearZ, farZ);
			invProjMatrix = DirectX::XMMatrixInverse(nullptr, projMatrix);
		}

		DirectX::XMMATRIX getViewMatrix() const {
			return viewMatrix;
		}

		DirectX::XMMATRIX getInvViewMatrix() const {
			return invViewMatrix;
		}

		DirectX::XMMATRIX getProjMatrix() const {
			return projMatrix;
		}

		DirectX::XMMATRIX getInvProjMatrix() const {
			return invProjMatrix;
		}

		DirectX::XMVECTOR getPosition() const {
			return invViewMatrix.r[3];
		}
	};
}