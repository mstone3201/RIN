#pragma once

struct Camera {
	float4x4 viewMatrix;
	float4x4 projMatrix;
	float4x4 viewProjMatrix;
	float3 position;
	float padding0;
	float frustumXX;
	float frustumXZ;
	float frustumYY;
	float frustumYZ;
	float nearZ; // Negative
	float farZ; // Negative
	float2 padding1;
};

// Returns false if the view space sphere is completely outside of the frustum
bool inFrustum(float3 viewCenter, float radius, float frustumXX, float frustumXZ, float frustumYY, float frustumYZ, float nearZ, float farZ) {
	// http://www8.cs.umu.se/kurser/5DV051/HT12/lab/plane_extraction.pdf
	// Plane normals are facing inward
	// Use frustum symmetry to cull two planes at the same time with abs()
	float3 trViewCenter = float3(abs(viewCenter.x), abs(viewCenter.y), viewCenter.z);
	// Left and right planes
	bool visible = dot(float3(frustumXX, 0.0f, frustumXZ), trViewCenter) >= -radius;
	// Top and bottom planes
	visible = visible && dot(float3(0.0f, frustumYY, frustumYZ), trViewCenter) >= -radius;
	// Near plane
	visible = visible && viewCenter.z - radius <= nearZ;
	// Far plane
	visible = visible && viewCenter.z + radius >= farZ;

	return visible;
}

// Gets the AABB of a view space sphere projected into NDC space
// Returns false if the sphere is behind or clips the near plane
bool getProjectedSphereAABB(float3 C, float r, float nearZ, float2 ab, out float4 aabb) {
	// Michael Mara and Morgan McGuire, 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere, Journal of Computer Graphics Techniques (JCGT), vol. 2, no. 2, 70-83, 2013
	// https://jcgt.org/published/0002/02/05/

	// Also, check out this sample by Arseny Kapoulkine who arrives at the same simplification
	// https://github.com/zeux/niagara/blob/master/src/shaders/drawcull.comp.glsl

	// This is all in view space

	// Discard if sphere is behind near plane or clips it
	if(C.z + r >= nearZ) return false;

	// axis <1, 0, 0>
	float2 cX = float2(C.x, C.z);
	// min/max.x will be divided by min/max.y later, so the magnitude of the vectors does not matter here
	float2 vX = float2(sqrt(dot(cX, cX) - r * r), r); // <t, r> = <|c|cos, |c|sin>
	float2 minX = mul(float2x2(vX.x, vX.y, -vX.y, vX.x), cX);
	float2 maxX = mul(float2x2(vX.x, -vX.y, vX.y, vX.x), cX);

	// axis <0, 1, 0>
	float2 cY = float2(C.y, C.z);
	float2 vY = float2(sqrt(dot(cY, cY) - r * r), r);
	float2 minY = mul(float2x2(vY.x, vY.y, -vY.y, vY.x), cY);
	float2 maxY = mul(float2x2(vY.x, -vY.y, vY.y, vY.x), cY);

	// Project
	/*
	Column Vector Right-Handed Projection Matrix:
	| 1 / (ar * tan(fov / 2)) |              0.0 |                       0.0 |                       0.0 |
	|                     0.0 | 1 / tan(fov / 2) |                       0.0 |                       0.0 |
	|                     0.0 |              0.0 |        far / (near - far) | near * far / (near - far) |
	|                     0.0 |              0.0 |                      -1.0 |                       0.0 |

	a = projMatrix._m00
	b = projMatrix._m11
	c = projMatrix._m22
	d = projMatrix._m23

	mul(projMatrix, <x, y, z, 1>) = <ax, by, cz + d, -z>

	Divide each component by -z for perspective division (clip space -> NDC space)
	*/

	// viewLeft = <minX.x, 0, minX.y>
	float leftX = ab.x * minX.x / -minX.y;

	// viewRight = <maxX.x, 0, maxX.y>
	float rightX = ab.x * maxX.x / -maxX.y;

	// viewBottom = <0, minY.x, minY.y>
	float bottomY = ab.y * minY.x / -minY.y;

	// viewTop = <0, maxY.x, maxY.y>
	float topY = ab.y * maxY.x / -maxY.y;

	aabb = float4(leftX, bottomY, rightX, topY);

	return true;
}