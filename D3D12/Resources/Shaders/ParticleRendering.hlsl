#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Raytracing/DDGICommon.hlsli"
#include "Primitives.hlsli"
#include "Packing.hlsli"
#include "Noise.hlsli"

struct ParticleData
{
	float3 Position;
	float LifeTime;
	float3 Velocity;
	float Size;
};

StructuredBuffer<ParticleData> tParticleData : register(t0);
StructuredBuffer<uint> tAliveList : register(t1);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float3 PositionWS : POSITION;
	float3 Normal : NORMAL;
};

InterpolantsVSToPS VSMain(uint instanceId : SV_InstanceID, uint vertexId : SV_VertexID)
{
	InterpolantsVSToPS output;

	uint particleIndex = tAliveList[instanceId];
	ParticleData particle = tParticleData[particleIndex];

	float3 p = particle.Size * SPHERE[vertexId].xyz;

	output.PositionWS.xyz = p + particle.Position;
	output.Position = mul(float4(output.PositionWS, 1.0f), cView.ViewProjection);
	output.Normal = p;

	return output;
}

struct PSOut
{
 	float4 Color : SV_Target0;
	float2 Normal : SV_Target1;
	float Roughness : SV_Target2;
};

LightResult DoLight(float3 specularColor, float R, float3 diffuseColor, float3 N, float3 V, float3 worldPos, float2 pixel, float linearDepth, float dither)
{
	LightResult totalResult = (LightResult)0;

	for(uint i = 0; i < cView.LightCount; ++i)
	{
		Light light = GetLight(i);
		LightResult result = DoLight(light, specularColor, diffuseColor, R, N, V, worldPos, linearDepth, dither);
		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	return totalResult;
}

PSOut PSMain(InterpolantsVSToPS input)
{
	float3 radiance = 0;
	float3 V = normalize(cView.ViewLocation - input.PositionWS);
	radiance += SampleDDGIIrradiance(input.PositionWS, input.Normal, -V) / PI;

	float dither = InterleavedGradientNoise(input.Position.xy);
	LightResult result = DoLight(1.0f, 0.3f, 0.2f, input.Normal, V, input.PositionWS, input.Position.xy, input.Position.w, dither);
	//radiance += result.Diffuse + result.Specular;

	PSOut output;
	output.Color = float4(radiance, 1.0f);
	output.Normal = EncodeNormalOctahedron(input.Normal);
	output.Roughness = 0.3f;
	return output;
}
