// BRDF used by Unreal Engine 4: https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// Cook-Torrance reflectance model: https://graphics.pixar.com/library/ReflectanceModel/paper.pdf
// fresnel equation with roughness term: https://seblagarde.wordpress.com/2011/08/17/hello-world/
// helpful resource: https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
// helpful resource: https://learnopengl.com/PBR/Theory

#include "PBRInputs.hlsli"
#include "SceneObject.hlsli"
#include "Camera.hlsli"
#include "Light.hlsli"
#include "Color.hlsli"
#include "Util.hlsli"

struct RootConstants {
	uint diffuseIBLID;
	uint specularIBLID;
	uint specularMIPCount;
	uint brdfLUTID;
};

ConstantBuffer<RootConstants> rootConstants : register(b0);
ConstantBuffer<Camera> cameraBuffer : register(b1);
StructuredBuffer<Light> lightBuffer : register(t0);
StructuredBuffer<LightCluster> lightClusterBuffer : register(t1);
Texture2D<float4> textures[] : register(t0, space1);
TextureCube<float4> cubeTextures[] : register(t0, space2);
SamplerState textureSampler : register(s0);
SamplerState cubeSampler : register(s1);
SamplerState brdfLUTSampler : register(s2);

float4 main(PBRInput input) : SV_TARGET {
	float3 baseColor = textures[input.material.baseColorID].Sample(textureSampler, input.tex).xyz;
	float2 normal = textures[input.material.normalID].Sample(textureSampler, input.tex).xy;
	float2 roughnessAO = textures[input.material.roughnessAOID].Sample(textureSampler, input.tex).xy;
	float metallic = textures[input.material.metallicID].Sample(textureSampler, input.tex).x;

	float roughness = roughnessAO.x;
	float ao = roughnessAO.y;

	// Normal vector
	normal = normal * 2.0f - 1.0f;
	float3 N = normalize(mul(float3(normal, sqrt(1.0f - normal.x * normal.x - normal.y * normal.y)), input.tbn)); // TBN is a row-major row vector matrix (x * A)

	float3 diffColor = baseColor * (1.0f - metallic);
	float3 specColor = lerp(0.04f, baseColor, metallic); // Substance uses (0.08 * specularLevel) where 0.5 is the default for dielectric materials

	// Vector pointing to the camera
	float3 V = normalize(cameraBuffer.position - input.worldPos);

	float NV = saturate(dot(N, V));

	// Find the light cluster this fragment is inside of
	float2 cluster_ij = (input.clipPos.xy / input.clipPos.w * 0.5f + 0.5f) * float2(FRUSTUM_CLUSTER_WIDTH, FRUSTUM_CLUSTER_HEIGHT);
	float viewZ = getViewZClip(input.clipPos.z, cameraBuffer.projMatrix._m22_m23);
	uint cluster_k = getClusterKConstants(viewZ, cameraBuffer.clusterConstantA, cameraBuffer.clusterConstantB);
	cluster_k = min(cluster_k, FRUSTUM_CLUSTER_DEPTH - 1);
	uint lightClusterOffset = getClusterOffset(uint3(cluster_ij, cluster_k));

	// Accumulate point light contribution
	float3 Ir = 0.0f;
	for(uint i = 0; i < lightClusterBuffer[lightClusterOffset].lightCount; i++) {
		Light light = lightBuffer[lightClusterBuffer[lightClusterOffset].lights[i]];

		// Light vector pointing from pixel to light
		float3 L = light.position - input.worldPos;
		float distance = length(L);
		L /= distance;
		// Halfway vector between V and L
		float3 H = normalize(V + L);

		float NH = saturate(dot(N, H));
		float NL = saturate(dot(N, L));
		float VH = saturate(dot(V, H));

		// Specular contribution
		float3 Fx = F(VH, specColor); // F0 = specColor
		float3 Rs = D(NH, roughness) * G(NL, NV, roughness) * Fx / 4.0f; // Cancel out NL * NV from the denominator

		// Diffuse contribution
		float3 Rd = diffColor * INV_PI;
		float3 d = 1.0f - Fx;

		// Total reflectance
		float3 R = Rs + d * Rd; // s = Fx, which is already in the numerator of Rs so it is omitted

		// Light intensity - point light
		float window = distance / light.radius;
		float window2 = window * window;
		float numsqrt = saturate(1.0f - window2 * window2);
		float falloff = numsqrt * numsqrt / (distance * distance + 1.0f);
		float3 I = light.color * falloff;

		// Total contribution from this light
		Ir += I * NL * R;
	}

	// Image-based lighting contribution
	float3 Fx = F_roughness(NV, specColor, roughness); // F0 = specColor

	// Diffuse contribution
	float3 Id = cubeTextures[rootConstants.diffuseIBLID].SampleLevel(cubeSampler, N.xzy, 0).xyz;
	float3 Rd = diffColor * Id;
	float3 d = 1.0f - Fx;

	// Specular contribution
	float3 R = 2.0f * NV * N - V; // reflect(-V, N)
	float3 Is = cubeTextures[rootConstants.specularIBLID].SampleLevel(cubeSampler, R.xzy, roughness * rootConstants.specularMIPCount).xyz;
	float2 brdf = textures[rootConstants.brdfLUTID].SampleLevel(brdfLUTSampler, float2(NV, roughness), 0).xy;
	float3 Rs = Is * (Fx * brdf.x + brdf.y);

	// Total contribution from image-based lighting
	Ir += (Rs + d * Rd) * ao;

	if(getObjectFlagMaterialTypeField(input.flags) == MATERIAL_TYPE_PBR_EMISSIVE)
		// Emissive contribution
		Ir += textures[input.material.specialID].Sample(textureSampler, input.tex).xyz;
	
	// Final color
	float3 color = aces_tonemap(Ir);
	color = linear_to_srgb(color);
	return float4(color, 1.0f);
}