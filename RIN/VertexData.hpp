#pragma once

#include <DirectXMath.h>
#include <DirectXPackedVector.h>

namespace RIN {
	// Vertex definitions
	// These are used to enforce that meshes which are created
	// manually strictly follow the required layout
	struct StaticVertex {
		DirectX::XMFLOAT3 position;
		// It would have been nice to use the HALF4 type, but since
		// it is aligned to 8 bytes it adds unwanted padding
		DirectX::PackedVector::HALF normalX;
		DirectX::PackedVector::HALF normalY;
		DirectX::PackedVector::HALF normalZ;
		DirectX::PackedVector::HALF texX;
		DirectX::PackedVector::HALF tangentX;
		DirectX::PackedVector::HALF tangentY;
		DirectX::PackedVector::HALF tangentZ;
		DirectX::PackedVector::HALF texY;
	};

	struct DynamicVertex {
		DirectX::XMFLOAT3 position;
		DirectX::PackedVector::HALF normalX;
		DirectX::PackedVector::HALF normalY;
		DirectX::PackedVector::HALF normalZ;
		DirectX::PackedVector::HALF texX;
		DirectX::PackedVector::HALF tangentX;
		DirectX::PackedVector::HALF tangentY;
		DirectX::PackedVector::HALF tangentZ;
		DirectX::PackedVector::HALF texY;
	};

	struct SkinnedVertex {
		DirectX::XMFLOAT3 position;
		DirectX::PackedVector::HALF normalX;
		DirectX::PackedVector::HALF normalY;
		DirectX::PackedVector::HALF normalZ;
		DirectX::PackedVector::HALF texX;
		DirectX::PackedVector::HALF tangentX;
		DirectX::PackedVector::HALF tangentY;
		DirectX::PackedVector::HALF tangentZ;
		DirectX::PackedVector::HALF texY;
		DirectX::PackedVector::XMUBYTE4 boneIndices;
		DirectX::PackedVector::XMUBYTEN4 boneWeights;
	};
}