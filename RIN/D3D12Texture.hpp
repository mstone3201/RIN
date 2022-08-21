#pragma once

#include <d3d12.h>

#include "Texture.hpp"
#include "FreeListAllocator.hpp"

namespace RIN {
	template<class T> class DynamicPool;

	class D3D12Texture : public Texture {
		friend class D3D12Renderer;
		friend class DynamicPool<D3D12Texture>;

		const FreeListAllocator::Allocation textureAlloc;
		ID3D12Resource* resource;
		bool dead = false;

		D3D12Texture(TEXTURE_TYPE type, TEXTURE_FORMAT format, FreeListAllocator::Allocation& textureAlloc, ID3D12Resource* resource) :
			Texture(type, format),
			textureAlloc(textureAlloc),
			resource(resource)
		{}

		D3D12Texture(const D3D12Texture&) = delete;
		~D3D12Texture() = default;
	};
}