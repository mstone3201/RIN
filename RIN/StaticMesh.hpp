#pragma once

#include "BoundingSphere.hpp"

namespace RIN {
	class StaticMesh {
	protected:
		const BoundingSphere boundingSphere;
		bool _resident = false;

		StaticMesh(const BoundingSphere& boundingSphere) :
			boundingSphere(boundingSphere)
		{}

		StaticMesh(const StaticMesh&) = delete;
		virtual ~StaticMesh() = 0;
	public:
		// Check this to determine if it is safe to free the
		// buffers used to create the mesh
		bool resident() const {
			return _resident;
		}
	};

	// inline allows the definition to reside here even if it is included multiple times
	inline StaticMesh::~StaticMesh() {}
}