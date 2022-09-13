#pragma once

#define PI 3.1415926535897932384626433832795f
#define INV_PI 0.31830988618379067153776752674503f
#define DIELECTRIC_F0 0.04f // Substance uses (0.08 * specularLevel) where 0.5 is the default for dielectric materials

// Section 4.4.1 https://google.github.io/filament/Filament.md.html#toc4.4.1
// Specular D - GGX
// D(NH, a) = a^2 / (pi * (NH^2 * (a^2 - 1) + 1)^2)
float specD(float NH, float a) {
	float a2 = a * a;
	float div = NH * NH * (a2 - 1.0f) + 1.0f;
	return a2 / (PI * div * div);
}

// Section 4.4.2 https://google.github.io/filament/Filament.md.html#toc4.4.2
// Specualar V - Height correlated Smith GGX
// V = G / (4 * NL * NV)
// V(NL, NV, a) = 0.5 / (NL * sqrt(NV^2 * (1 - a^2) + a^2) + NV * sqrt(NL^2 * (1 - a^2) + a^2))
float specV(float NL, float NV, float a) {
	float a2 = a * a;
	float GGX1 = NL * sqrt(NV * NV * (1.0f - a2) + a2);
	float GGX2 = NV * sqrt(NL * NL * (1.0f - a2) + a2);
	return 0.5f / (GGX1 + GGX2);
}

// Section 4.4.3 https://google.github.io/filament/Filament.md.html#toc4.4.3
// Specular F - Schlick
// F(VH, F0, F90 = 1) = F0 + (F90 - F0) * (1 - VH)^5
float3 specF(float VH, float3 F0) {
	return F0 + (1.0f - F0) * pow(1.0f - VH, 5.0f);
}

float specF(float VH, float F0) {
	return F0 + (1.0f - F0) * pow(1.0f - VH, 5.0f);
}

// Secion 4.12.1 https://google.github.io/filament/Filament.md.html#toc4.12.1
// Sheen Specular D - Charlie
// D(t, a) = (2 + 1 / a) * pow(sin(t), 1 / a) / 2 * pi
// We can get sin(t) from NH when |N||H| = 1 => NH^2 = cos(t) * cos(t)
// We have the property cos^2(t) + sin^2(t) = 1
// Therefore, sin(t) = sqrt(1 - NH^2), which gives us
// D(NH, a) = (2 + 1 / a) * pow(1 - NH^2, 1 / (2 * a)) / 2 * pi
float specDSheen(float NH, float a) {
	float invA = 1.0f / a;
	return (2.0f + invA) * pow(1.0f - NH * NH, 0.5f * invA) * 0.5f * INV_PI;
}

// Secion 4.12.1 https://google.github.io/filament/Filament.md.html#toc4.12.1
// Sheen Specular V - Neubelt
// V(NL, NV) = 1 / (4 * (NL + NV - NL * NV))
float specVSheen(float NL, float NV) {
	return 1.0f / (4.0f * (NL + NV - NL * NV));
}

// Remapped and clamped roughness
float getAlpha(float roughness) {
	return max(roughness * roughness, 0.05f); // 0.05 seems to eliminate specular aliasing
}