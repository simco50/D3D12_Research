#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Primitives.hlsli"
#include "External/Atmosphere.hlsli"

struct PassParams
{
	float2 DimensionsInv;
	RWTexture2DArrayH<float4> Sky;
};
DEFINE_CONSTANTS(PassParams, 0);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float3 UV : TEXCOORD;
};

InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	InterpolantsVSToPS output;
	float3 positionVS = mul(CUBE[vertexId].xyz, (float3x3)cView.WorldToView);
	output.Position = mul(float4(positionVS, 1.0f), cView.ViewToClip);
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
	static const float3x3 CUBEMAP_ROTATIONS[] =
	{
		float3x3(0,0,-1, 0,-1,0, -1,0,0),   // right
		float3x3(0,0,1, 0,-1,0, 1,0,0),     // left
		float3x3(1,0,0, 0,0,-1, 0,1,0),     // top
		float3x3(1,0,0, 0,0,1, 0,-1,0),     // bottom
		float3x3(1,0,0, 0,-1,0, 0,0,-1),    // back
		float3x3(-1,0,0, 0,-1,0, 0,0,1),    // front
	};

	float2 uv = TexelToUV(threadId.xy, cPassParams.DimensionsInv);
	float3 dir = normalize(mul(CUBEMAP_ROTATIONS[threadId.z], float3(uv * 2 - 1, -1)));

	float3 rayStart = cView.ViewLocation;
	float rayLength = 1000000.0f;

	Light sun = GetLight(0);
	float3 lightDir = -sun.Direction;
	float3 lightColor = sun.GetColor() / sun.Intensity;

	float3 transmittance;
	float3 sky = IntegrateScattering(rayStart, dir, rayLength, lightDir, lightColor, transmittance);

	cPassParams.Sky.Store(threadId, float4(sky, 1.0f));
}
