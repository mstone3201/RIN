// BRDF and falloff used by Unreal Engine 4: https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// Cook-Torrance reflectance model: https://graphics.pixar.com/library/ReflectanceModel/paper.pdf
// Fresnel equation with roughness term: https://seblagarde.wordpress.com/2011/08/17/hello-world/
// Helpful resource: https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
// Helpful resource: https://learnopengl.com/PBR/Theory
// Google Filament documentation: https://google.github.io/filament/Filament.md.html

/*
Notes: This shader implements the BRDFs used by Google Filament
If you are curious about the math, just check out their documentation, they explain things very well.
Their standard BRDF implementation can be seen here: https://google.github.io/filament/Filament.md.html#toc4.6
Their clear coat implementation can be seen here: https://google.github.io/filament/Filament.md.html#toc4.9
Their sheen BRDF implementation can be seen here: https://google.github.io/filament/Filament.md.html#toc4.12
Their IBL implementation can be seen here: https://google.github.io/filament/Filament.md.html#toc5.3
*/

#include "PBRInputs.hlsli"
#include "SceneObject.hlsli"
#include "Camera.hlsli"
#include "Light.hlsli"
#include "Color.hlsli"
#include "BRDF.hlsli"

struct RootConstants {
	uint iblSpecularMIPCount;
};

ConstantBuffer<RootConstants> rootConstants : register(b0);
ConstantBuffer<Camera> cameraBuffer : register(b1);
StructuredBuffer<Light> lightBuffer : register(t0);
StructuredBuffer<LightCluster> lightClusterBuffer : register(t1);
Texture2D<float3> dfgLUT : register(t2);
TextureCube<float3> iblDiffTexture : register(t3);
TextureCube<float3> iblSpecTexture : register(t4);
Texture2D<float4> textures[] : register(t0, space1);
SamplerState textureSampler : register(s0);
SamplerState cubeSampler : register(s1);
SamplerState dfgLUTSampler : register(s2);

// Returns the mapped normal given a normal in tangent space
float3 mapNormal(float3 normal, float3x3 tbn) {
	return normalize(mul(normal, tbn)); // TBN is a row-major row vector matrix (x * A)
}

// Returns the mapped normal given normal.xy in tangent space
float3 mapNormal(float2 normal, float3x3 tbn) {
	// Assumes
	// normal.z > 0 in tangent space
	// |normal.xyz| = 1
	return mapNormal(float3(normal, sqrt(1.0f - normal.x * normal.x - normal.y * normal.y)), tbn);
}

// Returns the cluster offset given the clip space position of a fragment
uint getClusterOffsetClip(float4 clipPos) {
	float2 cluster_ij = (clipPos.xy / clipPos.w * 0.5f + 0.5f) * float2(FRUSTUM_CLUSTER_WIDTH, FRUSTUM_CLUSTER_HEIGHT);
	float viewZ = getViewZClip(clipPos.z, cameraBuffer.projMatrix._m22_m23);
	uint cluster_k = getClusterKConstants(viewZ, cameraBuffer.clusterConstantA, cameraBuffer.clusterConstantB);
	cluster_k = min(cluster_k, FRUSTUM_CLUSTER_DEPTH - 1);
	return getClusterOffset(uint3(cluster_ij, cluster_k));
}

// Returns of the diffuse part of the IBL evaluation
float3 getIBLDiffuse(float3 N) {
	return iblDiffTexture.SampleLevel(cubeSampler, N.xzy, 0);
}

// Returns the specular part of the IBL evaluation with multiple scattering
float3 getIBLSpecular(float3 R, float roughness, float2 dfg, float3 F0) {
	float3 Is = iblSpecTexture.SampleLevel(cubeSampler, R.xzy, roughness * rootConstants.iblSpecularMIPCount).xyz;
	// Rs(Is, dfg, F0) = Is * ((1 - F0) * dfg.x + F0 * dfg.y)
	return Is * lerp(dfg.xxx, dfg.yyy, F0);
}

float3 getIBLSpecular(float3 R, float roughness, float2 dfg, float F0) {
	float3 Is = iblSpecTexture.SampleLevel(cubeSampler, R.xzy, roughness * rootConstants.iblSpecularMIPCount).xyz;
	// Rs(Is, dfg, F0) = Is * ((1 - F0) * dfg.x + F0 * dfg.y)
	return Is * lerp(dfg.x, dfg.y, F0);
}

float3 evaluateStandard(PBRInput input) {
	float3 baseColor = textures[input.material.baseColorID].Sample(textureSampler, input.tex).xyz;
	float2 normal = textures[input.material.normalID].Sample(textureSampler, input.tex).xy;
	float2 roughnessAO = textures[input.material.roughnessAOID].Sample(textureSampler, input.tex).xy;
	float metallic = textures[input.material.metallicID].Sample(textureSampler, input.tex).x;

	float roughness = roughnessAO.x;
	float ao = roughnessAO.y;

	// Remapped and clamped roughness
	float a = getAlpha(roughness);

	// Normal mapping
	float3 N = mapNormal(normal * 2.0f - 1.0f, input.tbn);

	float3 diffColor = baseColor * (1.0f - metallic);
	float3 specColor = lerp(DIELECTRIC_F0, baseColor, metallic);

	// Vector pointing to the camera
	float3 V = normalize(cameraBuffer.position - input.worldPos);

	float NV = abs(dot(N, V));

	float2 dfg = dfgLUT.SampleLevel(dfgLUTSampler, float2(NV, roughness), 0).xy;
	float3 multiscatter = 1.0f + specColor * (1.0f / dfg.y - 1.0f);

	// Find the light cluster this fragment is inside of
	uint lightClusterOffset = getClusterOffsetClip(input.clipPos);

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

		// Specular contribution - See BRDF.hlsli
		// Rs = D * G * Fx / (4 * NL * NV) = D * V * Fx
		float3 Fx = specF(VH, specColor); // F0 = specColor
		float3 Rs = specD(NH, a) * specV(NL, NV, a) * Fx;

		// Multiple scattering compensation
		Rs *= multiscatter;

		// Diffuse contribution - Lambertian
		float3 Rd = diffColor * INV_PI;
		float3 d = (1.0f - Fx);

		// Total reflectance - Cook-Torrance
		// R = s * Rs + d * Rd
		// s = Fx, d = 1 - Fx
		// Rs is alread multiplied by Fx, so just multiply Rd by d
		float3 R = Rs + d * Rd;

		// Light intensity - point light
		float3 I = light.color * getPointLightFalloff(light.radius, distance);

		// Total contribution from this light
		Ir += I * NL * R;
	}

	// Image-based lighting contribution
	float3 Fx = specF(NV, specColor); // F0 = specColor

	// Diffuse contribution
	float3 Rd = diffColor * getIBLDiffuse(N);
	float3 d = 1.0f - Fx;

	// Specular contribution
	float3 R = 2.0f * NV * N - V; // reflect(-V, N)
	float3 Rs = getIBLSpecular(R, roughness, dfg, specColor);

	// Total contribution from image-based lighting
	return Ir + (Rs + d * Rd) * ao;
}

float3 evaluateClearCoat(PBRInput input) {
	float3 baseColor = textures[input.material.baseColorID].Sample(textureSampler, input.tex).xyz;
	float2 normal = textures[input.material.normalID].Sample(textureSampler, input.tex).xy;
	float2 roughnessAO = textures[input.material.roughnessAOID].Sample(textureSampler, input.tex).xy;
	float metallic = textures[input.material.metallicID].Sample(textureSampler, input.tex).x;
	float4 clearCoatParams = textures[input.material.specialID].Sample(textureSampler, input.tex);

	float roughness = roughnessAO.x;
	float ao = roughnessAO.y;

	float clearCoatRoughness = clearCoatParams.z;
	float clearCoatMask = clearCoatParams.w;

	// Remapped and clamped roughness
	float a = getAlpha(roughness);
	float ac = getAlpha(clearCoatRoughness);

	// Normal mapping
	float3 N = mapNormal(normal * 2.0f - 1.0f, input.tbn);
	float3 Nc = mapNormal(clearCoatParams.xy * 2.0f - 1.0f, input.tbn);

	float3 diffColor = baseColor * (1.0f - metallic);

	// Base layer F0 modification https://google.github.io/filament/Filament.md.html#toc4.9.4
	float3 sqrtF0 = sqrt(lerp(DIELECTRIC_F0, baseColor, metallic));
	float3 baseF0Num = 1.0f - 5.0f * sqrtF0;
	float3 baseF0Denom = 5.0f - sqrtF0;
	float3 specColor = baseF0Num * baseF0Num / (baseF0Denom * baseF0Denom);

	// Vector pointing to the camera
	float3 V = normalize(cameraBuffer.position - input.worldPos);

	float NV = abs(dot(N, V));
	float NVc = abs(dot(Nc, V));

	float2 dfg = dfgLUT.SampleLevel(dfgLUTSampler, float2(NV, roughness), 0).xy;
	float2 dfgc = dfgLUT.SampleLevel(dfgLUTSampler, float2(NVc, clearCoatRoughness), 0).xy;
	float3 multiscatter = 1.0f + specColor * (1.0f / dfg.y - 1.0f);
	float multiscatterc = 1.0f + DIELECTRIC_F0 * (1.0f / dfgc.y - 1.0f);

	// Find the light cluster this fragment is inside of
	uint lightClusterOffset = getClusterOffsetClip(input.clipPos);

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
		float NHc = saturate(dot(Nc, H));
		float NLc = saturate(dot(Nc, L));

		// Specular contribution - See BRDF.hlsli
		// Rs = D * G * Fx / (4 * NL * NV) = D * V * Fx
		float3 Fx = specF(VH, specColor); // F0 = specColor
		float3 Rs = specD(NH, a) * specV(NL, NV, a) * Fx;

		// Multiple scattering compensation
		Rs *= multiscatter;

		// Diffuse contribution - Lambertian
		float3 Rd = diffColor * INV_PI;
		float3 d = (1.0f - Fx);

		// Total reflectance - Cook-Torrance
		// R = s * Rs + d * Rd
		// s = Fx, d = 1 - Fx
		// Rs is alread multiplied by Fx, so just multiply Rd by d
		float3 R = Rs + d * Rd;

		// Clear coat contribution
		float Fxc = specF(VH, DIELECTRIC_F0) * clearCoatMask; // F0 = 0.04
		// Filament uses a much simpler Kelemen visibility function.
		// Here I just use the same visibility function as the standard model
		float Rsc = specD(NHc, ac) * specV(NLc, NVc, ac) * Fxc;
		float cc = 1.0f - Fxc;

		// Multiple scattering compensation
		Rsc *= multiscatterc;

		// Light intensity - point light
		float3 I = light.color * getPointLightFalloff(light.radius, distance);

		// Total contribution from this light
		Ir += I * (cc * NL * R + NLc * Rsc);
	}

	// Image-based lighting contribution
	float3 Fx = specF(NV, specColor); // F0 = specColor

	// Diffuse contribution
	float3 Rd = diffColor * getIBLDiffuse(N);
	float3 d = 1.0f - Fx;

	// Specular contribution
	float3 R = 2.0f * NV * N - V; // reflect(-V, N)
	float3 Rs = getIBLSpecular(R, roughness, dfg, specColor);

	// Clear coat contribution
	float Fxc = specF(NVc, DIELECTRIC_F0) * clearCoatMask;
	float3 Rc = 2.0f * NVc * Nc - V; // reflect(-V, Nc)
	float3 Rsc = getIBLSpecular(Rc, clearCoatRoughness, dfgc, DIELECTRIC_F0);
	float cc = 1.0f - Fxc;

	// Total contribution from image-based lighting
	return Ir + cc * (Rs + d * Rd) * ao + Rsc;
}

/*
Note: This sheen implementation does not include subsurface scattering,
for details on that see https://google.github.io/filament/Filament.md.html#toc4.12.2
*/
float3 evaluateSheen(PBRInput input) {
	float3 baseColor = textures[input.material.baseColorID].Sample(textureSampler, input.tex).xyz;
	float2 normal = textures[input.material.normalID].Sample(textureSampler, input.tex).xy;
	float2 roughnessAO = textures[input.material.roughnessAOID].Sample(textureSampler, input.tex).xy;
	float3 sheenColor = textures[input.material.specialID].Sample(textureSampler, input.tex).xyz;

	float roughness = roughnessAO.x;
	float ao = roughnessAO.y;

	// Remapped and clamped roughness
	float a = getAlpha(roughness);

	// Normal mapping
	float3 N = mapNormal(normal * 2.0f - 1.0f, input.tbn);

	// Vector pointing to the camera
	float3 V = normalize(cameraBuffer.position - input.worldPos);

	float NV = abs(dot(N, V));

	// Find the light cluster this fragment is inside of
	uint lightClusterOffset = getClusterOffsetClip(input.clipPos);

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

		// Specular contribution - See BRDF.hlsli
		float3 Rs = specDSheen(NH, a) * specVSheen(NL, NV) * sheenColor;

		// Diffuse contribution - Lambertian
		float3 Rd = baseColor * INV_PI;

		// Total reflectance
		float3 R = Rs + Rd;

		// Light intensity - point light
		float3 I = light.color * getPointLightFalloff(light.radius, distance);

		// Total contribution from this light
		Ir += I * NL * R;
	}

	// Image-based lighting contribution

	// Diffuse contribution
	float3 Rd = baseColor * getIBLDiffuse(N);

	// Specular contribution
	float3 R = 2.0f * NV * N - V; // reflect(-V, N)
	float3 Is = iblSpecTexture.SampleLevel(cubeSampler, R.xzy, roughness * rootConstants.iblSpecularMIPCount).xyz;
	float dg = dfgLUT.SampleLevel(dfgLUTSampler, float2(NV, roughness), 0).z;
	float3 Rs = Is * dg * sheenColor;

	// Total contribution from image-based lighting
	return Ir + (Rs + Rd) * ao;
}

float4 main(PBRInput input) : SV_TARGET {

	float3 Ir = 0.0f;

	uint materialType = getObjectFlagMaterialTypeField(input.flags);

	if(materialType == MATERIAL_TYPE_PBR_STANDARD) {
		Ir = evaluateStandard(input);
	} else if(materialType == MATERIAL_TYPE_PBR_EMISSIVE) {
		float3 emissive = textures[input.material.specialID].Sample(textureSampler, input.tex).xyz;
		Ir = evaluateStandard(input) + emissive;
	} else if(materialType == MATERIAL_TYPE_PBR_CLEAR_COAT) {
		Ir = evaluateClearCoat(input);
	} else if(materialType == MATERIAL_TYPE_PBR_SHEEN) {
		Ir = evaluateSheen(input);
	}

	// Final color
	float3 color = aces_tonemap(Ir);
	color = linear_to_srgb(color);
	return float4(color, 1.0f);
}