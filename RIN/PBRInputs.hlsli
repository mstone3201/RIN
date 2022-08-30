#pragma once

#include "SceneObject.hlsli"

struct PBRInput {
	float4 position : SV_POSITION;
	float4 clipPos : POSITION0;
	float3 worldPos : POSITION1;
	float3x3 tbn : TBN_MATRIX;
	float2 tex : TEXCOORD;
	Material material : MATERIAL;
};