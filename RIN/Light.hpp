#pragma once

#include <DirectXMath.h>

namespace RIN {
	template<class T> class DynamicPool;

	class Light {
		friend class D3D12Renderer;
		friend class DynamicPool<Light>;
	public:
		DirectX::XMFLOAT3 position{};
		DirectX::XMFLOAT3 color{};
		float radius = 0.0f;
	protected:
		bool _resident = false;

		Light() = default;
		Light(const Light&) = delete;
		virtual ~Light() = default;
	public:
		bool resident() const {
			return _resident;
		}
	};
}