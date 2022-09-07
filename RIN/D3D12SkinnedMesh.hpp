#pragma once

#include "SkinnedMesh.hpp"
#include "FreeListAllocator.hpp"
#include "Config.hpp"

namespace RIN {
	template<class T> class DynamicPool;

	class D3D12SkinnedMesh : public SkinnedMesh {
		friend class D3D12Renderer;
		friend class DynamicPool<D3D12SkinnedMesh>;

		struct LOD {
			FreeListAllocator::Allocation vertexAlloc;
			FreeListAllocator::Allocation indexAlloc;

			LOD(FreeListAllocator::Allocation& vertexAlloc, FreeListAllocator::Allocation& indexAlloc) :
				vertexAlloc(vertexAlloc),
				indexAlloc(indexAlloc) {}
		};

		std::optional<LOD> lods[LOD_COUNT];

		D3D12SkinnedMesh(const BoundingSphere& boundingSphere) :
			SkinnedMesh(boundingSphere)
		{}

		D3D12SkinnedMesh(const D3D12SkinnedMesh&) = delete;
		~D3D12SkinnedMesh() = default;
	};
}