#pragma once

struct VisBufferData
{
	uint PrimitiveID : 7;
	uint MeshletID : 16;
	uint ObjectID : 9;
};

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

BaryDerivs ComputeBarycentrics(float2 pixelCS, float4 vertexCS0, float4 vertexCS1, float4 vertexCS2)
{
	BaryDerivs result;

	float3 pos0 = vertexCS0.xyz / vertexCS0.w;
	float3 pos1 = vertexCS1.xyz / vertexCS1.w;
	float3 pos2 = vertexCS2.xyz / vertexCS2.w;

	float3 RcpW = rcp(float3(vertexCS0.w, vertexCS1.w, vertexCS2.w));

	float3 pos120X = float3(pos1.x, pos2.x, pos0.x);
	float3 pos120Y = float3(pos1.y, pos2.y, pos0.y);
	float3 pos201X = float3(pos2.x, pos0.x, pos1.x);
	float3 pos201Y = float3(pos2.y, pos0.y, pos1.y);

	float3 C_dx = pos201Y - pos120Y;
	float3 C_dy = pos120X - pos201X;

	float3 C = C_dx * (pixelCS.x - pos120X) + C_dy * (pixelCS.y - pos120Y);
	float3 G = C * RcpW;

	float H = dot(C, RcpW);
	float rcpH = rcp(H);

	result.Barycentrics = G * rcpH;

	float3 G_dx = C_dx * RcpW;
	float3 G_dy = C_dy * RcpW;

	float H_dx = dot(C_dx, RcpW);
	float H_dy = dot(C_dy, RcpW);

	result.DDX_Barycentrics = (G_dx * H - G * H_dx) * (rcpH * rcpH) * ( 2.0f * cView.ViewportDimensionsInv.x);
	result.DDY_Barycentrics = (G_dy * H - G * H_dy) * (rcpH * rcpH) * (-2.0f * cView.ViewportDimensionsInv.y);

	return result;
}