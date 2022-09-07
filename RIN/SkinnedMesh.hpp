#pragma once

#include "BoundingSphere.hpp"

namespace RIN {
	class SkinnedMesh {
	protected:
		const BoundingSphere boundingSphere;
		bool _resident = false;

		SkinnedMesh(const BoundingSphere& boundingSphere) :
			boundingSphere(boundingSphere) {}

		SkinnedMesh(const SkinnedMesh&) = delete;
		virtual ~SkinnedMesh() = 0;
	public:
		// Check this to determine if it is safe to free the
		// buffers used to create the mesh
		bool resident() const {
			return _resident;
		}
	};

	// inline allows the definition to reside here even if it is included multiple times
	inline SkinnedMesh::~SkinnedMesh() {}
}