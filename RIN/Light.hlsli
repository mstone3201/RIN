#pragma once

#define FRUSTUM_CLUSTER_WIDTH 16
#define FRUSTUM_CLUSTER_HEIGHT 8
#define FRUSTUM_CLUSTER_DEPTH 24
#define LIGHT_CLUSTER_LIGHT_COUNT 63

struct Light {
	float3 position;
	float radius;
	float3 color;
	uint flags;
};

struct LightCluster {
	uint lightCount;
	uint lights[LIGHT_CLUSTER_LIGHT_COUNT];
};

uint getLightFlagShowField(uint flags) {
	return flags & 1;
}

// Ola Olsson, Markus Billeter, and Ulf Assarsson, Clustered Deferred and Forward Shading, High Performance Graphics (2012)
// https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf

// Also, check out this post by Angel Ortiz where they describe their implementation of the above paper
// http://www.aortiz.me/2018/12/21/CG.html

// The reason why we use the depth subdivision equation from DOOM 2016 instead of the one in Ola Olsson's paper
// is that it allows us to specify exactly how many depth subdivisions we want, which lets us ignore the need to
// reallocate the cluster structure if the frustum changes
// Unfortunately, this means that the clusters may be too large or too small depending on the shape of the frustum
// DOOM 2016 uses 16 x 8 x 24 which seems to work decently for me as well under most conditions
// One additional thing to mention is that this implementation forgoes a lot of the optimizations suggested by Ola Osson,
// such as using a BVH to traverse the global light list during light assignment. Perhaps that might be something to
// implement in the future, but for now this is sufficient
// Another thing to point out is that the number of lights per cluster is a constant. It might be nice to allow the cluster
// size to vary in the future, but would require an extra layer of indirection. This implementation definitely has issues
// with high density light areas, and this change would certainly be an improvement.

/*
Clusters are defined going from left to right, bottom to top, front to back
Usually we might expect the clusters to go from top to bottom, similar to screen space
coordinates, however, since we are typically in NDC space, this makes things easier
*/
// Returns a flattened cluster offset
uint getClusterOffset(uint3 index) {
	return index.x + index.y * FRUSTUM_CLUSTER_WIDTH + index.z * FRUSTUM_CLUSTER_WIDTH * FRUSTUM_CLUSTER_HEIGHT;
}

// Returns the view space z
float getClusterZ(uint k, float nearZ, float farZ) {
	// In their blogpost, Angel Ortiz describes the depth subdivision equation used by DOOM 2016
	// viewZ = nearZ * pow(farZ / nearZ, k / FRUSTUM_CLUSTER_DEPTH)
	// Keep in mind our view space z values are negative

	return nearZ * pow(farZ / nearZ, float(k) / FRUSTUM_CLUSTER_DEPTH);
}

// Returns the depth slice
uint getClusterK(float viewZ, float nearZ, float farZ) {
	// In their blogpost, Angel Ortiz describes the depth subdivision equation used by DOOM 2016
	// k = log2(-viewZ) * FRUSTUM_CLUSTER_DEPTH / log2(-farZ / -nearZ) - FRUSTUM_CLUSTER_DEPTH * log2(-nearZ) / log2(-farZ / -nearZ)
	// Keep in mind our view space z values are negative

	float logNear = log2(-nearZ);
	// log2(-farZ / -nearZ) = log2(-farZ) - log2(-nearZ)
	float a = FRUSTUM_CLUSTER_DEPTH / (log2(-farZ) - logNear);
	return log2(-viewZ) * a - logNear * a;
}

// Returns the depth slice using precalculated constants
uint getClusterKConstants(float viewZ, float a, float b) {
	return log2(-viewZ) * a - b;
}

// Returns the falloff of a point light with radius r at distance d
float getPointLightFalloff(float r, float d) {
	float window = d / r;
	float window2 = window * window;
	float numsqrt = saturate(1.0f - window2 * window2);
	return numsqrt * numsqrt / (d * d + 1.0f);
}