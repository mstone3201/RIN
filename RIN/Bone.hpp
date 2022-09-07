#pragma once

#include <DirectXMath.h>

namespace RIN {
	class Bone {
		friend class D3D12Renderer;

		alignas(16) DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixIdentity();
		alignas(16) DirectX::XMMATRIX invWorldMatrix = DirectX::XMMatrixIdentity();

		Bone() = default;
		Bone(const Bone&) = delete;
		~Bone() = default;
	public:
		void XM_CALLCONV setWorldMatrix(DirectX::FXMMATRIX M) {
			worldMatrix = M;
			invWorldMatrix = DirectX::XMMatrixInverse(nullptr, M);
		}

		DirectX::XMMATRIX getWorldMatrix() const {
			return worldMatrix;
		}

		DirectX::XMMATRIX getInvWorldMatrix() const {
			return invWorldMatrix;
		}
	};
}