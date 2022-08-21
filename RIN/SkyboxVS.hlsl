#include "Camera.hlsli"

struct vsOutput {
	float4 position : SV_POSITION;
	float3 tex : TEXCOORD;
};

ConstantBuffer<Camera> cameraBuffer : register(b0);

[RootSignature(
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |"\
		"DENY_HULL_SHADER_ROOT_ACCESS |"\
		"DENY_DOMAIN_SHADER_ROOT_ACCESS |"\
		"DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
	"),"\
	"CBV(b0, visibility = SHADER_VISIBILITY_VERTEX),"\
	"DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL),"\
	"StaticSampler("\
		"s0,"\
		"filter = FILTER_MIN_MAG_LINEAR_MIP_POINT,"\
		"visibility = SHADER_VISIBILITY_PIXEL"\
	")"
)]
vsOutput main(float3 position : POSITION) {
	vsOutput output;

	output.position = float4(mul((float3x3)cameraBuffer.viewMatrix, position), 1.0f); // Ignore view matrix translation
	output.position = mul(cameraBuffer.projMatrix, output.position).xyww; // Make it so z is always 1.0f after the perspective divide
	output.tex = position.xzy; // In word space z points up, in texture cube coords y points up

	return output;
}