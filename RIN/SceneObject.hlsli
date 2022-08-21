#pragma once

#define LOD_COUNT 3

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