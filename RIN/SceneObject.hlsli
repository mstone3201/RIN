#pragma once

#define LOD_COUNT 3

#define MATERIAL_TYPE_PBR_STANDARD 0
#define MATERIAL_TYPE_PBR_EMISSIVE 1
#define MATERIAL_TYPE_PBR_CLEAR_COAT 2
#define MATERIAL_TYPE_PBR_SHEEN 3

struct BoundingSphere {
	float3 center;
	float radius;
};

struct LOD {
	uint startIndex;
	uint indexCount;
	uint vertexOffset;
};

struct Material {
	uint baseColorID;
	uint normalID;
	uint roughnessAOID;
	uint metallicID;
	uint heightID;
	uint specialID;
};

struct StaticObject {
	BoundingSphere boundingSphere;
	LOD lods[LOD_COUNT];
	Material material;
	uint flags;
};

struct DynamicObject {
	float4x4 worldMatrix;
	float4x4 invWorldMatrix;
	BoundingSphere boundingSphere;
	LOD lods[LOD_COUNT];
	Material material;
	uint flags;
};

struct SkinnedObject {
	BoundingSphere boundingSphere;
	LOD lods[LOD_COUNT];
	Material material;
	uint boneIndex;
	uint flags;
	float3 padding;
};

uint getObjectFlagShowField(uint flags) {
	return flags & 1;
}

uint getObjectFlagMaterialTypeField(uint flags) {
	return flags >> 1;
}

struct Bone {
	float4x4 worldMatrix;
	float4x4 invWorldMatrix;
};