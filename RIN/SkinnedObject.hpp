#pragma once

#include "SkinnedMesh.hpp"
#include "Armature.hpp"
#include "Material.hpp"

namespace RIN {
	template<class T> class DynamicPool;

	class SkinnedObject {
		friend class D3D12Renderer;
		friend class DynamicPool<SkinnedObject>;
	protected:
		SkinnedMesh* mesh;
		Armature* armature;
		Material* material;
		bool _resident = false;

		SkinnedObject(SkinnedMesh* mesh, Armature* armature, Material* material) :
			mesh(mesh),
			armature(armature),
			material(material)
		{}

		SkinnedObject(const SkinnedObject&) = delete;
		virtual ~SkinnedObject() = default;
	public:
		virtual void setMesh(SkinnedMesh* mesh) {
			if(!mesh) return;

			SkinnedObject::mesh = mesh;
		}

		virtual void setArmature(Armature* armature) {
			if(!armature) return;

			SkinnedObject::armature = armature;
		}

		virtual void setMaterial(Material* material) {
			if(!material) return;

			SkinnedObject::material = material;
		}

		// This implies that its mesh, armature, and material are resident too
		bool resident() const {
			return _resident && mesh->resident() && armature->resident() && material->resident();
		}
	};
}