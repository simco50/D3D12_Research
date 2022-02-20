#pragma once

struct VisBufferData
{
	uint PrimitiveID : 7;
	uint MeshletID : 11;
	uint ObjectID : 14;
};

bool RayPlaneIntersection(out float hitT, float3 rayOrigin, float3 rayDirection, float3 planeSurfacePoint, float3 planeNormal)
{
    float denominator = dot(rayDirection, planeNormal);
	if(abs(denominator) > 0.000000001f)
	{
	    float numerator = dot(planeSurfacePoint - rayOrigin, planeNormal);
		hitT = numerator / denominator;
		return hitT >= 0;
	}
	hitT = 0;
	return false;
}

// https://gamedev.stackexchange.com/questions/23743/whats-the-most-efficient-way-to-find-barycentric-coordinates
float3 GetBarycentricsFromPlanePoint(float3 pt, float3 v0, float3 v1, float3 v2)
{
    float3 e0 = v1 - v0;
    float3 e1 = v2 - v0;
    float3 e2 = pt - v0;
    float d00 = dot(e0, e0);
    float d01 = dot(e0, e1);
    float d11 = dot(e1, e1);
    float d20 = dot(e2, e0);
    float d21 = dot(e2, e1);
    float denom = 1.0 / (d00 * d11 - d01 * d01);
    float v = (d11 * d20 - d01 * d21) * denom;
    float w = (d00 * d21 - d01 * d20) * denom;
    float u = 1.0 - v - w;
    return float3(u, v, w);
}

float3 CreateCameraRay(float2 pixel)
{
	float aspect = cView.Projection[1][1] / cView.Projection[0][0];
	float tanHalfFovY = 1.f / cView.Projection[1][1];

	return normalize(
		(pixel.x * cView.ViewInverse[0].xyz * tanHalfFovY * aspect) -
		(pixel.y * cView.ViewInverse[1].xyz * tanHalfFovY) +
		cView.ViewInverse[2].xyz);
}

template<typename T>
T BaryInterpolate(T a, T b, T c, float3 barycentrics)
{
	return a * barycentrics.x + b * barycentrics.y + c * barycentrics.z;
}
