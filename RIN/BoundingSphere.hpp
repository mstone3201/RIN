#pragma once

#include <DirectXMath.h>

namespace RIN {
	struct BoundingSphere {
		const DirectX::XMFLOAT3 center;
		const float radius;

		BoundingSphere(float x, float y, float z, float radius) :
			center(x, y, z),
			radius(radius)
		{}
	};
}