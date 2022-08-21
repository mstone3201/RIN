#pragma once

#include "StaticMesh.hpp"
#include "Material.hpp"

namespace RIN {
	template<class T> class DynamicPool;

	class StaticObject {
		friend class D3D12Renderer;
		friend class DynamicPool<StaticObject>;
	protected:
		StaticMesh* mesh;
		Material* material;
		bool _resident = false;

		StaticObject(StaticMesh* mesh, Material* material) :
			mesh(mesh),
			material(material)
		{}

		StaticObject(const StaticObject&) = delete;
		virtual ~StaticObject() = default;
	public:
		virtual void setMesh(StaticMesh* mesh) {
			if(!mesh) return;

			StaticObject::mesh = mesh;
		}

		virtual void setMaterial(Material* material) {
			if(!material) return;

			StaticObject::material = material;
		}

		// This implies that its mesh and material are resident too
		bool resident() const {
			return _resident && mesh->resident() && material->resident();
		}
	};
}