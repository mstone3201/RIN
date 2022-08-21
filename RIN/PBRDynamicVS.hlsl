#include "PBRInputs.hlsli"
#include "SceneObject.hlsli"
#include "Camera.hlsli"

struct vsOutput {
	float4 position : SV_POSITION;
	float2 tex : TEXCOORD;
	Material material : MATERIAL;
};

struct RootConstants {
	uint objectID;
};

ConstantBuffer<RootConstants> rootConstants : register(b0);
ConstantBuffer<Camera> cameraBuffer : register(b1);
StructuredBuffer<DynamicObject> dynamicObjectBuffer : register(t0);

[RootSignature(
	"RootFlags("\
		"ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |"\
		"DENY_HULL_SHADER_ROOT_ACCESS |"\
		"DENY_DOMAIN_SHADER_ROOT_ACCESS |"\
		"DENY_GEOMETRY_SHADER_ROOT_ACCESS"\
	"),"\
	"RootConstants(num32BitConstants = 1, b0, visibility = SHADER_VISIBILITY_VERTEX),"\
	"RootConstants(num32BitConstants = 4, b0, visibility = SHADER_VISIBILITY_PIXEL),"\
	"CBV(b1),"\
	"SRV(t0, visibility = SHADER_VISIBILITY_VERTEX),"\
	"DescriptorTable("\
		"SRV(t0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE),"\
		"SRV(t0, numDescriptors = unbounded, space = 1, offset = 0, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE),"\
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
	float3 normal : NORMAL,
	float3 tangent : TANGENT,
	float3 binormal : BINORMAL,
	float2 tex : TEXCOORD
) {
	PBRInput output;
	
	DynamicObject object = dynamicObjectBuffer[rootConstants.objectID];

	float4 worldPos = mul(object.worldMatrix, float4(position, 1.0f));
	output.position = mul(cameraBuffer.viewProjMatrix, worldPos);
	output.worldPos = worldPos.xyz;
	float3 T = normalize(mul((float3x3)object.worldMatrix, tangent));
	float3 B = normalize(mul((float3x3)object.worldMatrix, binormal));
	// normalMatrix * x = (worldMatrix^-1)^T * x = x * worldMatrix^-1
	float3 N = normalize(mul(normal, (float3x3)object.invWorldMatrix));
	output.tbn = float3x3(T, B, N); // This is a row-major row vector matrix (x * A)
	output.tex = tex;
	output.material = object.material;

	return output;
}