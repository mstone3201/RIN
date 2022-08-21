#include "Color.hlsli"

TextureCube<float3> skybox : register(t0);
SamplerState skyboxSampler : register(s0);

float4 main(
	float4 position : SV_POSITION,
	float3 tex : TEXCOORD
) : SV_TARGET {
	float3 color = skybox.SampleLevel(skyboxSampler, tex, 0);

	color = aces_tonemap(color);
	color = linear_to_srgb(color);
	return float4(color, 1.0f);
}