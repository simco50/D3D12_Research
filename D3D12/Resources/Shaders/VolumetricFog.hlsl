#include "Common.hlsli"
#include "CommonBindings.hlsli"

#define RootSig \
		"CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
		"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
		"DescriptorTable(SRV(t5, numDescriptors = 10), visibility=SHADER_VISIBILITY_ALL), " \
		GLOBAL_BINDLESS_TABLE ", " \
		"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP), " \

struct ShaderData
{
	int3 ClusterDimensions;
	int NoiseTexture;
	float3 InvClusterDimensions;
	int NumLights;
	float4x4 ViewProjectionInv;
	float4x4 ProjectionInv;
	float4x4 ViewInv;
};

ConstantBuffer<ShaderData> cData : register(b0);

RWTexture3D<float3> uOutputTexture : register(u0);

[RootSignature(RootSig)]
[numthreads(8, 8, 4)]
void InjectFogLightingCS(uint3 threadId : SV_DISPATCHTHREADID)
{
	float nearPlane = 0.1f;
	float farPlane = 100.0f;

	// Compute the sample point inside the froxel. Jittered
	float2 texelUV = (threadId.xy + 0.5f) * cData.InvClusterDimensions.xy;
	float sliceMinZ = threadId.z * cData.InvClusterDimensions.z;
	float minLinearZ = sliceMinZ * (farPlane - nearPlane);
	float sliceMaxZ = (threadId.z + 1) * cData.InvClusterDimensions.z;
	float maxLinearZ = sliceMaxZ * (farPlane - nearPlane);
	float sliceThickness = sliceMaxZ - sliceMinZ;

	float3 worldPosition = WorldFromDepth(texelUV, (minLinearZ + maxLinearZ) * 0.5f, cData.ViewProjectionInv);

	worldPosition = float3(texelUV * 200 - 100, 0);

	// Calculate Density based on the fog volumes
	float cellDensity = 0.0f;
	float3 cellAbsorption = 0.0f;
	float densityVariation = 0.0f;

	float3 p = floor(worldPosition * 1.0f);
	float3 f = frac(worldPosition * 1.0f);
	f = (f * f) * (3.0f - (f * 2.0f));
	float2 uv = (p.xy + (float2(37.0f, 17.0f) * p.z)) + f.xy;
	float2 rg = tTexture2DTable[cData.NoiseTexture].SampleLevel(sDiffuseSampler, (uv + 0.5f) / 256.0f, 0).yx;
	densityVariation += (lerp(rg.x, rg.y, f.z) * 16.0f);
	p = floor(worldPosition * 2.0f);
	f = frac(worldPosition * 2.0f);
	f = (f * f) * (3.0f - (f * 2.0f));
	uv = (p.xy + (float2(37.0f, 17.0f) * p.z)) + f.xy;
	rg = tTexture2DTable[cData.NoiseTexture].SampleLevel(sDiffuseSampler, (uv + 0.5f) / 256.0f, 0).yx;
	densityVariation += (lerp(rg.x, rg.y, f.z) * 8.0f);
	p = floor(worldPosition * 4.0f);
	f = frac(worldPosition * 4.0f);
	f = (f * f) * (3.0f - (f * 2.0f));
	uv = (p.xy + (float2(37.0f, 17.0f) * p.z)) + f.xy;
	rg = tTexture2DTable[cData.NoiseTexture].SampleLevel(sDiffuseSampler, (uv + 0.5f) / 256.0f, 0).yx;
	densityVariation += (lerp(rg.x, rg.y, f.z) * 4.0f);
	p = floor(worldPosition * 8.0f);
	f = frac(worldPosition * 8.0f);
	f = (f * f) * (3.0f - (f * 2.0f));
	uv = (p.xy + (float2(37.0f, 17.0f) * p.z)) + f.xy;
	rg = tTexture2DTable[cData.NoiseTexture].SampleLevel(sDiffuseSampler, (uv + 0.5f) / 256.0f, 0).yx;
	densityVariation += (lerp(rg.x, rg.y, f.z) * 2.0f);
	
	densityVariation /= 30.0f;

	// Iterate over all the lights and light the froxel
	/*for(int i = 0; i < cData.NumLights; ++i)
	{

	}*/

	uOutputTexture[threadId] = float3(densityVariation, 0, 0);
}