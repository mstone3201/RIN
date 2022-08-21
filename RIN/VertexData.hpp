#pragma once

#include <DirectXMath.h>

namespace RIN {
	// Vertex definitions
	// These are used to enforce that meshes which are created
	// manually strictly follow the required layout
	struct StaticVertex {
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 normal;
		DirectX::XMFLOAT3 tangent;
		DirectX::XMFLOAT3 binormal;
		DirectX::XMFLOAT2 tex;
	};

	struct DynamicVertex {
		DirectX::XMFLOAT3 position;
		DirectX::XMFLOAT3 normal;
		DirectX::XMFLOAT3 tangent;
		DirectX::XMFLOAT3 binormal;
		DirectX::XMFLOAT2 tex;
	};
}