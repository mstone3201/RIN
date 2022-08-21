#pragma once

// Substance sRGB equation: https://substance3d.adobe.com/tutorials/courses/the-pbr-guide-part-1
// ACES tonemapping: https://64.github.io/tonemapping/
// fitted ACES tonemapping: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/

float3 srgb_to_linear(float3 srgb) {
	return pow(srgb, 2.2f);
}

float3 srgb_to_linear_fancy(float3 srgb) {
	return srgb <= 0.04045f ? srgb / 12.92f : pow((srgb + 0.055f) / 1.055f, 2.2f);
}

float3 linear_to_srgb(float3 lin) {
	return pow(lin, 0.4545f);
}

float3 aces_tonemap(float3 hdr) {
	float3 aces = hdr * 0.6f;
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((aces * (a * aces + b)) / (aces * (c * aces + d) + e));
}