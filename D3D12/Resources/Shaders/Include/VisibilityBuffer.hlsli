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

struct BaryDerivs
{
	float3 Barycentrics;
	float3 DDX_Barycentrics;
	float3 DDY_Barycentrics;
};

BaryDerivs ComputeBarycentrics(float2 pixelClip, VisBufferData visibility, float3 worldPos0, float3 worldPos1, float3 worldPos2)
{
	BaryDerivs bary;

	float3 rayDir = CreateCameraRay(pixelClip);

	float3 neighborRayDirX = QuadReadAcrossX(rayDir);
    float3 neighborRayDirY = QuadReadAcrossY(rayDir);

	float3 edge1 = worldPos1 - worldPos0;
	float3 edge2 = worldPos2 - worldPos0;
	float3 triNormal = cross(edge2.xyz, edge1.xyz);

    float hitT;
    RayPlaneIntersection(hitT, cView.ViewPosition.xyz, rayDir, worldPos0.xyz, triNormal);
    float3 hitPoint = cView.ViewPosition.xyz + rayDir * hitT;
   	bary.Barycentrics = GetBarycentricsFromPlanePoint(hitPoint, worldPos0.xyz, worldPos1.xyz, worldPos2.xyz);

    if (WaveActiveAllEqual((uint)visibility) && WaveActiveCountBits(true) == WaveGetLaneCount())
    {
        bary.DDX_Barycentrics = ddx(bary.Barycentrics);
        bary.DDY_Barycentrics = ddy(bary.Barycentrics);
    }
    else
    {
        float hitTX;
        RayPlaneIntersection(hitTX, cView.ViewPosition.xyz, neighborRayDirX, worldPos0.xyz, triNormal);

        float hitTY;
        RayPlaneIntersection(hitTY, cView.ViewPosition.xyz, neighborRayDirY, worldPos0.xyz, triNormal);

        float3 hitPointX = cView.ViewPosition.xyz + neighborRayDirX * hitTX;
        float3 hitPointY = cView.ViewPosition.xyz + neighborRayDirY * hitTY;

        bary.DDX_Barycentrics = bary.Barycentrics - GetBarycentricsFromPlanePoint(hitPointX, worldPos0.xyz, worldPos1.xyz, worldPos2.xyz);
        bary.DDY_Barycentrics = bary.Barycentrics - GetBarycentricsFromPlanePoint(hitPointY, worldPos0.xyz, worldPos1.xyz, worldPos2.xyz);
    }
	return bary;
}
