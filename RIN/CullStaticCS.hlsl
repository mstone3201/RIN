#include "SceneObject.hlsli"
#include "Camera.hlsli"

struct Size {
	uint2 size;
};

struct StaticCommand {
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
StructuredBuffer<StaticObject> staticObjectBuffer : register(t0);
AppendStructuredBuffer<StaticCommand> staticCommandBuffer : register(u0);
ConstantBuffer<Camera> cameraBuffer : register(b1);
Texture2D<float> depthHierarchy : register(t1);
SamplerState depthHierarchySampler : register(s0);

[RootSignature(
	"RootFlags(0),"\
	"RootConstants(num32BitConstants=2, b0),"\
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
	StaticObject object = staticObjectBuffer[index.x];
	if(object.flags) {
		float radius = object.boundingSphere.radius;

		// Transform to view space
		float3 viewCenter = mul(cameraBuffer.viewMatrix, float4(object.boundingSphere.center, 1.0f)).xyz;

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
				// Clip the aabb to the screen, the part offscreen is not
				// visible anyway, so just consider the visible part
				// Transform to UV space
				float4 uvAABB = saturate(aabb * float4(0.5f, -0.5f, 0.5f, -0.5f) + 0.5f);

				float2 aabbSize = float2(uvAABB.z - uvAABB.x, uvAABB.w - uvAABB.y) * float2(depthHierarchySize.size);

				float sampledDepth = depthHierarchy.SampleLevel(
					depthHierarchySampler,
					(uvAABB.xy + uvAABB.zw) * 0.5f,
					ceil(log2(max(aabbSize.x, aabbSize.y)))
				);
				
				/*
				a = projMatrix._m00
				b = projMatrix._m11
				c = projMatrix._m22
				d = projMatrix._m23

				mul(projMatrix, <x, y, z, 1>) = <ax, by, cz + d, -z>

				depth = (cz + d) / -z = -c - d/z
				*/
				float depth = -cameraBuffer.projMatrix._m22 - cameraBuffer.projMatrix._m23 / (viewCenter.z + radius);

				visible = depth <= sampledDepth + 0.00001f;
			}

			if(visible) {
				// LOD selection
				float dist = length(viewCenter) - radius;

				uint lod = dist < 5.0f ? 0 : (dist < 10.0f ? 1 : 2);

				StaticCommand command;
				command.objectID = index.x;
				command.drawIndexed.indexCountPerInstance = object.lods[lod].indexCount;
				command.drawIndexed.instanceCount = 1;
				command.drawIndexed.startIndexLocation = object.lods[lod].startIndex;
				command.drawIndexed.baseVertexLocation = object.lods[lod].vertexOffset;
				command.drawIndexed.startInstanceLocation = 0;

				staticCommandBuffer.Append(command);
			}
		}
	}
}