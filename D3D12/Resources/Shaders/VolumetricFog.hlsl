#include "Common.hlsli"
#include "CommonBindings.hlsli"
#include "Lighting.hlsli"

#define RootSig \
		"CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
		"CBV(b2, visibility=SHADER_VISIBILITY_ALL), " \
		"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
		"DescriptorTable(SRV(t4, numDescriptors = 10), visibility=SHADER_VISIBILITY_ALL), " \
		GLOBAL_BINDLESS_TABLE ", " \
		"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_POINT, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP), " \
		"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_POINT, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP), " \
		"StaticSampler(s2, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, comparisonFunc=COMPARISON_GREATER), " \

struct ShaderData
{
	int3 ClusterDimensions;
	int NoiseTexture;
	float3 InvClusterDimensions;
	int NumLights;
	float4x4 ViewProjectionInv;
	float4x4 ProjectionInv;
	float4x4 ViewInv;
	float NearZ;
	float FarZ;
};

ConstantBuffer<ShaderData> cData : register(b0);

Texture3D<float4> tLightScattering : register(t4);
RWTexture3D<float4> uOutLightScattering : register(u0);

float HGPhase(float VdotL, float g)
{
    float denom = 1.0 - sqrt(g);
    float div = (1.0 + sqrt(g)) + ((2.0 * g) * (-VdotL));
    return denom / ((12.56637096405029296875 * div) * sqrt(div));
}

[RootSignature(RootSig)]
[numthreads(8, 8, 4)]
void InjectFogLightingCS(uint3 threadId : SV_DISPATCHTHREADID)
{

	float nearPlane = 0.1f;
	float farPlane = 100.0f;

	// Compute the sample point inside the froxel. Jittered
	float2 texelUV = (threadId.xy + 0.5f) * cData.InvClusterDimensions.xy;
	float sliceMinZ = threadId.z * cData.InvClusterDimensions.z;
	float minLinearZ = nearPlane + sliceMinZ * (farPlane - nearPlane);
	float sliceMaxZ = (threadId.z + 1) * cData.InvClusterDimensions.z;
	float maxLinearZ = nearPlane + sliceMaxZ * (farPlane - nearPlane);
	float sliceThickness = sliceMaxZ - sliceMinZ;

	float3 worldPosition = WorldFromDepth(texelUV, (sliceMinZ + sliceMaxZ) * 0.5f, cData.ViewProjectionInv);

	//worldPosition = float3(texelUV * 200 - 100, 0);

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

	float3 lightScattering = 0;

	float3 V = normalize(cData.ViewInv[3].xyz - worldPosition);

	// Iterate over all the lights and light the froxel
	for(int i = 0; i < cData.NumLights; ++i)
	{
		Light light = tLights[i];
		float attentuation = GetAttenuation(light, worldPosition);
		if(attentuation <= 0.0f)
		{
			continue;
		}

		float4 pos = float4(texelUV, 0, (minLinearZ + maxLinearZ) * 0.5f);
		if(light.ShadowIndex >= 0)
		{
			int shadowIndex = GetShadowIndex(light, pos, worldPosition);
			attentuation *= ShadowNoPCF(worldPosition, shadowIndex, light.InvShadowSize);
		}

		float3 L = normalize(worldPosition - light.Position);
		if(light.Type == LIGHT_DIRECTIONAL)
		{
			L = -normalize(light.Direction);
		}
		float VdotL = dot(V, L);
		float4 lightColor = light.GetColor() * light.Intensity;

		lightScattering += attentuation * lightColor.xyz * HenyeyGreenstrein(VdotL);
	}

	uOutLightScattering[threadId] = float4(lightScattering, cellDensity);
}

float Max4(float4 v)
{
	return max(v.x, max(v.y, max(v.z, v.w)));
}

groupshared uint gsMaxDepth;

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void AccumulateFogCS(uint3 threadId : SV_DISPATCHTHREADID, uint groupIndex : SV_GROUPINDEX)
{
	float2 texCoord = threadId.xy * cData.InvClusterDimensions.xy;
	float depth = tDepth.SampleLevel(sDiffuseSampler, texCoord, 0).r;

	if(groupIndex == 0)
	{
		gsMaxDepth = 0xffffffff;
	}

    GroupMemoryBarrierWithGroupSync();

	InterlockedMin(gsMaxDepth, asuint(depth));

    GroupMemoryBarrierWithGroupSync();

	float maxDepth = asfloat(gsMaxDepth);

	uint lastSlice = (maxDepth) * cData.ClusterDimensions.z;

	float3 lightScattering = 0;
	float cellDensity = 0;
	float transmittance = 1;

	uOutLightScattering[int3(threadId.xy, 0)] = float4(lightScattering, transmittance);

	for(int sliceIndex = 1; sliceIndex <= lastSlice; ++sliceIndex)
	{
		float4 scatteringDensity = tLightScattering[int3(threadId.xy, sliceIndex - 1)];
		lightScattering += scatteringDensity.xyz * transmittance;
		cellDensity += scatteringDensity.w;
		transmittance = saturate(exp(-cellDensity));
		uOutLightScattering[int3(threadId.xy, sliceIndex)] = float4(lightScattering, transmittance);
	}
}
