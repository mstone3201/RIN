#pragma once

#define PI 3.1415926535897932384626433832795
#define INV_PI 0.31830988618379067153776752674503

// Specular D
float D(float NH, float roughness) {
	float a = roughness * roughness;
	float a2 = a * a;
	float div = NH * NH * (a2 - 1.0f) + 1.0f;
	return a2 / max(PI * div * div, 1e-8f);
}

float G1(float NX, float k) {
	return 1.0f / (NX * (1.0f - k) + k);
}

// Specular G - cancel out NL * NV from the numerator
float G(float NL, float NV, float roughness) {
	float rplus = roughness + 1.0f;
	float k = rplus * rplus / 8.0f;
	return G1(NL, k) * G1(NV, k);
}

// Specular F
float3 F(float VH, float3 F0) {
	return F0 + ldexp(1.0f - F0, (-5.55473f * VH - 6.98316f) * VH);
}

// Fresnel with fudged roughness term
float3 F_roughness(float NV, float3 F0, float roughness) {
	return F0 + ldexp(saturate(1.0f - roughness - F0), (-5.55473f * NV - 6.98316f) * NV);
}