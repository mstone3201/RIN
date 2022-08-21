#pragma once

#include <cstdint>

namespace RIN {
	constexpr uint32_t LOD_COUNT = 3;

	enum class RENDER_ENGINE : uint32_t {
		D3D12 // Uses the standard D3D12 render engine
	};

	struct Config {
		RENDER_ENGINE engine = RENDER_ENGINE::D3D12;
		uint64_t uploadStreamSize = 0; // Streaming budget for each frame in bytes
		uint32_t staticVertexCount = 0;
		uint32_t staticIndexCount = 0;
		uint32_t staticMeshCount = 0;
		uint32_t staticObjectCount = 0;
		uint32_t dynamicVertexCount = 0;
		uint32_t dynamicIndexCount = 0;
		uint32_t dynamicMeshCount = 0;
		uint32_t dynamicObjectCount = 0;
		uint64_t texturesSize = 0; // Cumulative size of all textures in bytes
		uint32_t textureCount = 0;
		uint32_t materialCount = 0;
	};
}