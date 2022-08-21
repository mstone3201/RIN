#pragma once

#include "BoundingSphere.hpp"

namespace RIN {
	class DynamicMesh {
	protected:
		const BoundingSphere boundingSphere;
		bool _resident = false;

		DynamicMesh(const BoundingSphere& boundingSphere) :
			boundingSphere(boundingSphere)
		{}

		DynamicMesh(const DynamicMesh&) = delete;
		virtual ~DynamicMesh() = 0;
	public:
		// Check this to determine if it is safe to free the
		// buffers used to create the mesh
		bool resident() const {
			return _resident;
		}
	};

	// inline allows the definition to reside here even if it included multiple times
	inline DynamicMesh::~DynamicMesh() {}
}