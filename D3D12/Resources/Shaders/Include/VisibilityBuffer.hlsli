#pragma once

struct VisBufferData
{
	uint PrimitiveID : 24;
	uint ObjectID : 8;
};

struct BaryDerivatives
{
	float3 Lambda;
	float3 DDX;
	float3 DDY;
};

BaryDerivatives InitBaryDerivatives(float4 clipPos0, float4 clipPos1, float4 clipPos2, float2 pixelNdc, float2 invWinSize)
{
	BaryDerivatives bary = (BaryDerivatives)0;

	float3 invW = rcp(float3(clipPos0.w, clipPos1.w, clipPos2.w));

	float2 ndc0 = clipPos0.xy * invW.x;
	float2 ndc1 = clipPos1.xy * invW.y;
	float2 ndc2 = clipPos2.xy * invW.z;

	float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
	bary.DDX = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet;
	bary.DDY = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet;

	float2 deltaVec = pixelNdc - ndc0;
	float interpInvW = (invW.x + deltaVec.x * dot(invW, bary.DDX) + deltaVec.y * dot(invW, bary.DDY));
	float interpW = rcp(interpInvW);

	bary.Lambda.x = interpW * (invW[0] + deltaVec.x * bary.DDX.x * invW[0] + deltaVec.y * bary.DDY.x * invW[0]);
	bary.Lambda.y = interpW * (0.0f + deltaVec.x * bary.DDX.y * invW[1] + deltaVec.y * bary.DDY.y * invW[1]);
	bary.Lambda.z = interpW * (0.0f + deltaVec.x * bary.DDX.z * invW[2] + deltaVec.y * bary.DDY.z * invW[2]);

	bary.DDX *= (2.0f * invWinSize.x);
	bary.DDY *= (2.0f * invWinSize.y);

	bary.DDY *= -1.0f;

	return bary;
}

float3 InterpolateWithDeriv(BaryDerivatives deriv, float v0, float v1, float v2)
{
	float3 ret = 0;
	ret.x = dot(deriv.Lambda, float3(v0, v1, v2));
	ret.y = dot(deriv.DDX * float3(v0, v1, v2), float3(1, 1, 1));
	ret.z = dot(deriv.DDY * float3(v0, v1, v2), float3(1, 1, 1));
	return ret;
}

float2 InterpolateWithDeriv(BaryDerivatives deriv, float2 v0, float2 v1, float2 v2)
{
	return mul(deriv.Lambda, float3x2(v0, v1, v2));
}

float2 InterpolateWithDeriv(BaryDerivatives deriv, float2 v0, float2 v1, float2 v2, out float2 outDX, out float2 outDY)
{
	outDX = mul(deriv.DDX, float3x2(v0, v1, v2));
	outDY = mul(deriv.DDY, float3x2(v0, v1, v2));
	return InterpolateWithDeriv(deriv, v0, v1, v2);
}

float3 InterpolateWithDeriv(BaryDerivatives deriv, float3 v0, float3 v1, float3 v2)
{
	return mul(deriv.Lambda, float3x3(v0, v1, v2));
}
