#pragma once

#include "StaticMesh.hpp"
#include "FreeListAllocator.hpp"
#include "Config.hpp"

namespace RIN {
	template<class T> class DynamicPool;

	class D3D12StaticMesh : public StaticMesh {
		friend class D3D12Renderer;
		friend class DynamicPool<D3D12StaticMesh>;

		struct LOD {
			FreeListAllocator::Allocation vertexAlloc;
			FreeListAllocator::Allocation indexAlloc;

			LOD(FreeListAllocator::Allocation& vertexAlloc, FreeListAllocator::Allocation& indexAlloc) :
				vertexAlloc(vertexAlloc),
				indexAlloc(indexAlloc)
			{}
		};

		std::optional<LOD> lods[LOD_COUNT]{};

		D3D12StaticMesh(const BoundingSphere& boundingSphere) :
			StaticMesh(boundingSphere)
		{}

		D3D12StaticMesh(const D3D12StaticMesh&) = delete;
		~D3D12StaticMesh() = default;
	};
}