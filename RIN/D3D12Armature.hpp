#pragma once

#include "Armature.hpp"
#include "FreeListAllocator.hpp"

namespace RIN {
	template<class T> class DynamicPool;

	class D3D12Armature : public Armature {
		friend class D3D12Renderer;
		friend class DynamicPool<D3D12Armature>;

		const FreeListAllocator::Allocation boneAlloc;

		D3D12Armature(Bone* bones, FreeListAllocator::Allocation& boneAlloc) :
			Armature(bones),
			boneAlloc(boneAlloc)
		{}

		D3D12Armature(const D3D12Armature&) = delete;
		~D3D12Armature() = default;
	};
}