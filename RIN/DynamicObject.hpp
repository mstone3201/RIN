#pragma once

#include <DirectXMath.h>

#include "DynamicMesh.hpp"
#include "Material.hpp"

namespace RIN {
	template<class T> class DynamicPool;

	class DynamicObject {
		friend class D3D12Renderer;
		friend class DynamicPool<DynamicObject>;
	protected:
		alignas(16) DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixIdentity();
		alignas(16) DirectX::XMMATRIX invWorldMatrix = DirectX::XMMatrixIdentity();
		DynamicMesh* mesh;
		Material* material;
		bool _resident = false;

		DynamicObject(DynamicMesh* mesh, Material* material) :
			mesh(mesh),
			material(material)
		{}

		DynamicObject(const DynamicObject&) = delete;
		virtual ~DynamicObject() = default;
	public:
		virtual void setMesh(DynamicMesh* mesh) {
			if(!mesh) return;

			DynamicObject::mesh = mesh;
		}

		virtual void setMaterial(Material* material) {
			if(!material) return;
			
			DynamicObject::material = material;
		}

		virtual void XM_CALLCONV setWorldMatrix(DirectX::FXMMATRIX M) {
			worldMatrix = M;
			invWorldMatrix = DirectX::XMMatrixInverse(nullptr, M);
		}

		DirectX::XMMATRIX getWorldMatrix() const {
			return worldMatrix;
		}

		DirectX::XMMATRIX getInvWorldMatrix() const {
			return invWorldMatrix;
		}

		// This implies that its mesh and material are resident too
		bool resident() const {
			return _resident && mesh->resident() && material->resident();
		}
	};
}