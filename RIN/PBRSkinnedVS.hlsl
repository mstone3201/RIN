#include "PBRInputs.hlsli"
#include "SceneObject.hlsli"
#include "Camera.hlsli"

struct RootConstants {
	uint objectID;
};

ConstantBuffer<RootConstants> rootConstants : register(b0);
ConstantBuffer<Camera> cameraBuffer : register(b1);
StructuredBuffer<SkinnedObject> skinnedObjectBuffer : register(t0);
StructuredBuffer<Bone> boneBuffer : register(t1);

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
	"SRV(t1, visibility = SHADER_VISIBILITY_VERTEX),"\
	"SRV(t0, visibility = SHADER_VISIBILITY_PIXEL),"\
	"SRV(t1, visibility = SHADER_VISIBILITY_PIXEL),"\
	"DescriptorTable("\
		"SRV(t0, numDescriptors = unbounded, space = 1, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE),"\
		"SRV(t0, numDescriptors = unbounded, space = 2, offset = 0, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE),"\
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
	float4 tangent : TANGENT, // float4(tangent.xyz, tex.y)
	uint4 boneIndices : BLENDINDICES,
	float4 boneWeights : BLENDWEIGHTS
) {
	PBRInput output;

	SkinnedObject object = skinnedObjectBuffer[rootConstants.objectID];

	Bone bones[4] = {
		boneBuffer[object.boneIndex + boneIndices.x],
		boneBuffer[object.boneIndex + boneIndices.y],
		boneBuffer[object.boneIndex + boneIndices.z],
		boneBuffer[object.boneIndex + boneIndices.w]
	};

	float4x4 worldMatrix = boneWeights.x * bones[0].worldMatrix +
		boneWeights.y * bones[1].worldMatrix +
		boneWeights.z * bones[2].worldMatrix +
		boneWeights.w * bones[3].worldMatrix;

	float4x4 invWorldMatrix = boneWeights.x * bones[0].invWorldMatrix +
		boneWeights.y * bones[1].invWorldMatrix +
		boneWeights.z * bones[2].invWorldMatrix +
		boneWeights.w * bones[3].invWorldMatrix;

	float4 worldPos = mul(worldMatrix, float4(position, 1.0f));
	output.position = mul(cameraBuffer.viewProjMatrix, worldPos);
	output.clipPos = output.position;
	output.worldPos = worldPos.xyz;
	float3 T = normalize(mul((float3x3)worldMatrix, tangent.xyz));
	// normalMatrix * x = (worldMatrix^-1)^T * x = x * worldMatrix^-1
	float3 N = normalize(mul(normal.xyz, (float3x3)invWorldMatrix));
	output.tbn = float3x3(T, cross(N, T), N); // This is a row-major row vector matrix (x * A)
	output.tex.x = normal.w;
	output.tex.y = tangent.w;
	output.material = object.material;
	output.flags = object.flags;

	return output;
}