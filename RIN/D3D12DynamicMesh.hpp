#pragma once

#include "DynamicMesh.hpp"
#include "FreeListAllocator.hpp"
#include "Config.hpp"

namespace RIN {
	template<class T> class DynamicPool;

	class D3D12DynamicMesh : public DynamicMesh {
		friend class D3D12Renderer;
		friend class DynamicPool<D3D12DynamicMesh>;

		struct LOD {
			FreeListAllocator::Allocation vertexAlloc;
			FreeListAllocator::Allocation indexAlloc;

			LOD(FreeListAllocator::Allocation& vertexAlloc, FreeListAllocator::Allocation& indexAlloc) :
				vertexAlloc(vertexAlloc),
				indexAlloc(indexAlloc)
			{}
		};

		std::optional<LOD> lods[LOD_COUNT]{};

		D3D12DynamicMesh(const BoundingSphere& boundingSphere) :
			DynamicMesh(boundingSphere) {}

		D3D12DynamicMesh(const D3D12DynamicMesh&) = delete;
		~D3D12DynamicMesh() = default;
	};
}