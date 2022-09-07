#pragma once

#include <DirectXMath.h>

#include "Config.hpp"

namespace RIN {
	/*
	Representation of the camera on the GPU
	Aligned to float4
	*/
	struct alignas(16) D3D12CameraData {
		DirectX::XMFLOAT4X4A viewMatrix; // Column-major column vector matrix
		DirectX::XMFLOAT4X4A projMatrix; // Column-major column vector matrix
		DirectX::XMFLOAT4X4A invProjMatrix; // Column-major column vector matrix
		DirectX::XMFLOAT4X4A viewProjMatrix; // Column-major column vector matrix
		DirectX::XMFLOAT3A position;
		float frustumXX; // View space right frustum plane x normal (pointing in)
		float frustumXZ; // View space right frustum plane z normal (pointing in)
		float frustumYY; // View space top frustum plane y normal (pointing in)
		float frustumYZ; // View space top frustum plane z normal (pointing in)
		float nearZ; // Negative since the camera faces -z
		float farZ; // Negative since the camera faces -z
		float clusterConstantA; // FRUSTUM_CLUSTER_DEPTH / log2(farZ / nearZ)
		float clusterConstantB; // log2(nearZ) * clusterConstantA
	};

	struct D3D12BoundingSphereData {
		DirectX::XMFLOAT3 center;
		float radius;
	};

	struct D3D12LODData {
		uint32_t startIndex;
		uint32_t indexCount;
		uint32_t vertexOffset;
	};

	struct D3D12MaterialData {
		uint32_t baseColorID;
		uint32_t normalID;
		uint32_t roughnessAOID;
		uint32_t metallicID;
		uint32_t heightID;
		uint32_t specialID;
	};

	union D3D12ObjectFlagData {
		uint32_t data;
		struct {
			uint32_t show : 1;
			uint32_t materialType : 31;
		};
	};

	static_assert(sizeof(D3D12ObjectFlagData) == sizeof(uint32_t));

	/*
	Representation of a static object on the GPU
	Aligned to float4
	*/
	struct alignas(16) D3D12StaticObjectData {
		D3D12BoundingSphereData boundingSphere;
		D3D12LODData lods[LOD_COUNT];
		D3D12MaterialData material;
		D3D12ObjectFlagData flags;
	};

	/*
	Representation of a dynamic object on the GPU
	Aligned to float4
	*/
	struct alignas(16) D3D12DynamicObjectData {
		DirectX::XMFLOAT4X4A worldMatrix; // Column-major column vector matrix
		DirectX::XMFLOAT4X4A invWorldMatrix; // Column-major column vector matrix
		D3D12BoundingSphereData boundingSphere;
		D3D12LODData lods[LOD_COUNT];
		D3D12MaterialData material;
		D3D12ObjectFlagData flags;
	};

	/*
	Representation of a skinned object on the GPU
	Aligned to float4
	*/
	struct alignas(16) D3D12SkinnedObjectData {
		D3D12BoundingSphereData boundingSphere;
		D3D12LODData lods[LOD_COUNT];
		D3D12MaterialData material;
		uint32_t boneIndex;
		D3D12ObjectFlagData flags;
	};

	/*
	Representation of a bone on the GPU
	Aligned to float4
	*/
	struct alignas(16) D3D12BoneData {
		DirectX::XMFLOAT4X4A worldMatrix; // Column-major column vector matrix;
		DirectX::XMFLOAT4X4A invWorldMatrix; // Column-major column vector matrix;
	};

	union D3D12LightFlagData {
		uint32_t data;
		struct {
			uint32_t show : 1;
		};
	};

	static_assert(sizeof(D3D12LightFlagData) == sizeof(uint32_t));

	/*
	Representation of a light on the GPU
	Aligned to float4
	*/
	struct alignas(16) D3D12LightData {
		DirectX::XMFLOAT3 position;
		float radius;
		DirectX::XMFLOAT3 color;
		D3D12LightFlagData flags;
	};
}