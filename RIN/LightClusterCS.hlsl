#include "Camera.hlsli"
#include "Light.hlsli"

struct Length {
	uint length;
};

ConstantBuffer<Length> lightBufferLength : register(b0);
ConstantBuffer<Camera> cameraBuffer : register(b1);
StructuredBuffer<Light> lightBuffer : register(t0);
RWStructuredBuffer<LightCluster> lightClusterBuffer : register(u0);

[RootSignature(
	"RootFlags(0),"\
	"RootConstants(num32BitConstants = 1, b0),"\
	"CBV(b1),"\
	"SRV(t0),"\
	"DescriptorTable(UAV(u0))"
)]
[numthreads(16, 8, 8)]
void main(
	uint3 index : SV_DispatchThreadID
) {
	uint offset = getClusterOffset(index);

	float nearZ = getClusterZ(index.z, cameraBuffer.nearZ, cameraBuffer.farZ);
	float farZ = getClusterZ(index.z + 1, cameraBuffer.nearZ, cameraBuffer.farZ);

	/*
	tan(fov / 2) = y / z

	invProjMatrix._m00 = ar * tan(fov / 2)
	invProjMatrix._m11 = tan(fov / 2)

	Keep in mind z is negative and we want the sizes to be positive
	*/
	float2 nearHalfSize = -nearZ * cameraBuffer.invProjMatrix._m00_m11;
	float2 farHalfSize = -farZ * cameraBuffer.invProjMatrix._m00_m11;

	// Bottom left
	float2 ndcMin = (index.xy / float2(FRUSTUM_CLUSTER_WIDTH, FRUSTUM_CLUSTER_HEIGHT) * 2.0f - 1.0f);
	// Top right
	float2 ndcMax = ((index.xy + 1) / float2(FRUSTUM_CLUSTER_WIDTH, FRUSTUM_CLUSTER_HEIGHT) * 2.0f - 1.0f);

	float2 nearMin = ndcMin * nearHalfSize;
	float2 nearMax = ndcMax * nearHalfSize;
	float2 farMin = ndcMin * farHalfSize;
	float2 farMax = ndcMax * farHalfSize;

	float2 viewMin = min(nearMin, farMin);
	float2 viewMax = max(nearMax, farMax);

	uint lightCount = 0;
	for(uint i = 0; lightCount < LIGHT_CLUSTER_LIGHT_COUNT && i < lightBufferLength.length; ++i) {
		Light light = lightBuffer[i];

		if(getLightFlagShowField(light.flags)) {
			float4 lightPos = mul(cameraBuffer.viewMatrix, float4(light.position, 1.0f));

			// Calculate the squared distance to the aabb
			// Distance is 0.0 if the point is inside of the aabb
			float dx = max(max(lightPos.x - viewMax.x, viewMin.x - lightPos.x), 0.0f);
			float dy = max(max(lightPos.y - viewMax.y, viewMin.y - lightPos.y), 0.0f);
			// Keep in mind z is negative, so nearZ is the max and farZ is the min
			float dz = max(max(lightPos.z - nearZ, farZ - lightPos.z), 0.0f);

			if(dx * dx + dy * dy + dz * dz <= light.radius * light.radius)
				lightClusterBuffer[offset].lights[lightCount++] = i;
		}
	}

	lightClusterBuffer[offset].lightCount = lightCount;
}