#include "Common.hlsli"
#include "SkyCommon.hlsli"
#include "Primitives.hlsli"
#include "External/Atmosphere.hlsli"

RWTexture2D<float4> uSky : register(u0);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float3 UV : TEXCOORD;
};

InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	InterpolantsVSToPS output;
	float3 positionVS = mul(CUBE[vertexId].xyz, (float3x3)cView.View);
	output.Position = mul(float4(positionVS, 1.0f), cView.Projection);
	output.Position.z = 0.0001f;
	output.UV = CUBE[vertexId].xyz;
	return output;
}

float4 PSMain(in InterpolantsVSToPS input) : SV_Target
{
	float3 uv = normalize(input.UV);
	return float4(GetSky(uv), 1);
}

[numthreads(16, 16, 1)]
void ComputeSkyCS(uint3 threadId : SV_DispatchThreadID)
{
	float2 uv = threadId.xy * cView.TargetDimensionsInv;
	uv.x *= 2 * PI;
	uv.y *= PI;

	float3 dir = normalize(float3(sin(uv.x) * sin(uv.y), cos(uv.y), cos(uv.x) * sin(uv.y)));
	float3 rayStart = cView.ViewLocation;
	float rayLength = 1000000.0f;
	if(0)
	{
		float2 planetIntersection = PlanetIntersection(rayStart, dir);
		if(planetIntersection.x > 0)
		{
			rayLength = min(rayLength, planetIntersection.x);
		}
	}
	Light sun = GetLight(0);
	float3 lightDir = -sun.Direction;
	float3 lightColor = sun.GetColor();

	float3 transmittance;
	float3 sky = IntegrateScattering(rayStart, dir, rayLength, lightDir, lightColor, transmittance);

	uSky[threadId.xy] = float4(sky, 1.0f);
}
