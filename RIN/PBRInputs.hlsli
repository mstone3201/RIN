#pragma once

#include "SceneObject.hlsli"

struct PBRInput {
	float4 position : SV_POSITION;
	float4 clipPos : POSITION0;
	float3 worldPos : POSITION1;
	float3x3 tbn : TBN_MATRIX; // Row-major row vector matrix (x * A)
	float2 tex : TEXCOORD;
	Material material : MATERIAL;
	uint flags : OBJECT_FLAGS;
};