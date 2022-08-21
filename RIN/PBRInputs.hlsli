#pragma once

#include "SceneObject.hlsli"

struct PBRInput {
	float4 position : SV_POSITION;
	float3 worldPos : POSITION;
	float3x3 tbn : TBN_MATRIX;
	float2 tex : TEXCOORD;
	Material material : MATERIAL;
};