#include "PBRInputs.hlsli"
#include "SceneObject.hlsli"
#include "Camera.hlsli"

struct RootConstants {
	uint objectID;
};

ConstantBuffer<RootConstants> rootConstants : register(b0);
ConstantBuffer<Camera> cameraBuffer : register(b1);
StructuredBuffer<StaticObject> staticObjectBuffer : register(t0);

[RootSignature(
	"RootFlags("\
		"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |"\
		"DENY_HULL_SHADER_ROOT_ACCESS |"\
		"DENY_DOMAIN_SHADER_ROOT_ACCESS |"\
		"DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
	"),"\
	"RootConstants(num32BitConstants = 1, b0, visibility = SHADER_VISIBILITY_VERTEX),"\
	"RootConstants(num32BitConstants = 1, b0, visibility = SHADER_VISIBILITY_PIXEL),"\
	"CBV(b1),"\
	"SRV(t0, visibility = SHADER_VISIBILITY_VERTEX),"\
	"SRV(t0, visibility = SHADER_VISIBILITY_PIXEL),"\
	"SRV(t1, visibility = SHADER_VISIBILITY_PIXEL),"\
	"DescriptorTable("\
		"SRV(t2),"\
		"SRV(t3, offset = 2, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE),"\
		"SRV(t4, offset = 3, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE),"\
		"SRV(t0, offset = 4, numDescriptors = unbounded, space = 1, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE),"\
		"visibility = SHADER_VISIBILITY_PIXEL"
	"),"\
	"StaticSampler(s0, visibility = SHADER_VISIBILITY_PIXEL),"\
	"StaticSampler("\
		"s1,"\
		"filter = FILTER_MIN_MAG_MIP_LINEAR,"\
		"visibility = SHADER_VISIBILITY_PIXEL"\
	"),"\
	"StaticSampler("\
		"s2,"\
		"filter = FILTER_MIN_MAG_LINEAR_MIP_POINT,"\
		"addressU = TEXTURE_ADDRESS_CLAMP,"\
		"addressV = TEXTURE_ADDRESS_CLAMP,"\
		"addressW = TEXTURE_ADDRESS_CLAMP,"\
		"visibility = SHADER_VISIBILITY_PIXEL"\
	")"
)]
PBRInput main(
	float3 position : POSITION,
	float4 normal : NORMAL, // float4(normal.xyz, tex.x)
	float4 tangent : TANGENT // float4(tangent.xyz, tex.y)
) {
	StaticObject object = staticObjectBuffer[rootConstants.objectID];

	PBRInput output;
	output.position = mul(cameraBuffer.viewProjMatrix, float4(position, 1.0f));
	output.clipPos = output.position;
	output.worldPos = position;
	output.tbn = float3x3(tangent.xyz, cross(normal.xyz, tangent.xyz), normal.xyz); // This is a row-major row vector matrix (x * A)
	output.tex.x = normal.w;
	output.tex.y = tangent.w;
	output.material = object.material;
	output.flags = object.flags;

	return output;
}