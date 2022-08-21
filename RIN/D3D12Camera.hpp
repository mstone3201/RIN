#pragma once

#include "Camera.hpp"

namespace RIN {
	class D3D12Camera : public Camera {
		friend class D3D12Renderer;

		float frustumXX;
		float frustumXZ;
		float frustumYY;
		float frustumYZ;
		float nearZ;
		float farZ;

		D3D12Camera() {
			setPerspective(DirectX::XM_PIDIV2, 1.0f, 0.1f, 100.0f);
		}

		D3D12Camera(const D3D12Camera&) = delete;
		~D3D12Camera() = default;
	public:
		void setPerspective(float fovY, float aspect, float n, float f) override {
			/*
			Row Vector Right-Handed Projection Matrix:
			| 1 / (ar * tan(fov / 2)) |              0.0 |                       0.0 |  0.0 |
			|                     0.0 | 1 / tan(fov / 2) |                       0.0 |  0.0 |
			|                     0.0 |              0.0 |        far / (near - far) | -1.0 |
			|                     0.0 |              0.0 | near * far / (near - far) |  0.0 |
			*/
			projMatrix = DirectX::XMMatrixPerspectiveFovRH(fovY, aspect, n, f);
			invProjMatrix = DirectX::XMMatrixInverse(nullptr, projMatrix);

			// Get frustum planes in view space
			// http://www8.cs.umu.se/kurser/5DV051/HT12/lab/plane_extraction.pdf
			DirectX::XMMATRIX projMatrixT = DirectX::XMMatrixTranspose(projMatrix);
			DirectX::XMFLOAT3 right, top;
			DirectX::XMStoreFloat3(&right, DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(projMatrixT.r[3], projMatrixT.r[0])));
			DirectX::XMStoreFloat3(&top, DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(projMatrixT.r[3], projMatrixT.r[1])));

			frustumXX = right.x;
			frustumXZ = right.z;
			frustumYY = top.y;
			frustumYZ = top.z;
			nearZ = -n;
			farZ = -f;
		}
	};
}