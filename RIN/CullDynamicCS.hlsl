#include "SceneObject.hlsli"
#include "Camera.hlsli"

struct Size {
	uint2 size;
};

struct DynamicCommand {
	uint objectID;
	struct {
		uint indexCountPerInstance;
		uint instanceCount;
		uint startIndexLocation;
		int baseVertexLocation;
		uint startInstanceLocation;
	} drawIndexed;
};

ConstantBuffer<Size> depthHierarchySize : register(b0);
StructuredBuffer<DynamicObject> dynamicObjectBuffer : register(t0);
AppendStructuredBuffer<DynamicCommand> dynamicCommandBuffer : register(u0);
ConstantBuffer<Camera> cameraBuffer : register(b1);
Texture2D<float> depthHierarchy : register(t1);
SamplerState depthHierarchySampler : register(s0);

[RootSignature(
	"RootFlags(0),"\
	"RootConstants(num32BitConstants = 2, b0),"\
	"SRV(t0),"\
	"DescriptorTable(UAV(u0)),"\
	"CBV(b1),"\
	"DescriptorTable(SRV(t1)),"\
	"StaticSampler("\
		"s0,"\
		"filter = FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT,"\
		"addressU = TEXTURE_ADDRESS_CLAMP,"\
		"addressV = TEXTURE_ADDRESS_CLAMP,"\
		"addressW = TEXTURE_ADDRESS_CLAMP"\
	")"
)]
[numthreads(128, 1, 1)]
void main(
	uint3 index : SV_DispatchThreadID
) {
	/*
	TODO: Use wave intrinsics
	*/
	DynamicObject object = dynamicObjectBuffer[index.x];
	if(object.flags) {
		float3 a = float3(object.worldMatrix[0].xyz);
		float a2 = dot(a, a);
		float3 b = float3(object.worldMatrix[1].xyz);
		float b2 = dot(b, b);
		float3 c = float3(object.worldMatrix[2].xyz);
		float c2 = dot(c, c);

		float radius = object.boundingSphere.radius * sqrt(max(a2, max(b2, c2)));

		// Transform to view space
		float4 worldCenter = mul(object.worldMatrix, float4(object.boundingSphere.center, 1.0f));
		float3 viewCenter = mul(cameraBuffer.viewMatrix, worldCenter).xyz;

		// Frustum culling
		bool visible = inFrustum(
			viewCenter,
			radius,
			cameraBuffer.frustumXX,
			cameraBuffer.frustumXZ,
			cameraBuffer.frustumYY,
			cameraBuffer.frustumYZ,
			cameraBuffer.nearZ,
			cameraBuffer.farZ
		);

		if(visible) {
			// Occlusion culling
			float4 aabb;
			if(getProjectedSphereAABB(viewCenter, radius, cameraBuffer.nearZ, cameraBuffer.projMatrix._m00_m11, aabb)) {
				float4 uvAABB = saturate(aabb * float4(0.5f, -0.5f, 0.5f, -0.5f) + 0.5f);

				float2 aabbSize = float2(uvAABB.z - uvAABB.x, uvAABB.w - uvAABB.y) * float2(depthHierarchySize.size);

				float sampledDepth = depthHierarchy.SampleLevel(
					depthHierarchySampler,
					(uvAABB.xy + uvAABB.zw) * 0.5f,
					ceil(log2(max(aabbSize.x, aabbSize.y)))
				);

				float depth = -cameraBuffer.projMatrix._m22 - cameraBuffer.projMatrix._m23 / (viewCenter.z + radius);

				visible = depth <= sampledDepth + 0.00001f;
			}

			if(visible) {
				// LOD selection
				float dist = length(viewCenter) - radius;

				uint lod = dist < 5.0f ? 0 : (dist < 10.0f ? 1 : 2);

				DynamicCommand command;
				command.objectID = index.x;
				command.drawIndexed.indexCountPerInstance = object.lods[lod].indexCount;
				command.drawIndexed.instanceCount = 1;
				command.drawIndexed.startIndexLocation = object.lods[lod].startIndex;
				command.drawIndexed.baseVertexLocation = object.lods[lod].vertexOffset;
				command.drawIndexed.startInstanceLocation = 0;

				dynamicCommandBuffer.Append(command);
			}
		}
	}
}