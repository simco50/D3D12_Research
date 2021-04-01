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
	float4x4 ViewProjectionInv;
	float4x4 Projection;
	int3 ClusterDimensions;
	int NumLights;
	float3 InvClusterDimensions;
	float NearZ;
	float3 ViewLocation;
	float FarZ;
	float Jitter;
};

ConstantBuffer<ShaderData> cData : register(b0);

Texture3D<float4> tLightScattering : register(t4);
RWTexture3D<float4> uOutLightScattering : register(u0);

float HenyeyGreenstreinPhase(float LoV, float G)
{
	float result = 1.0f - G * G;
	result /= (4.0f * PI * pow(1.0f + G * G - (2.0f * G) * LoV, 1.5f));
	return result;
}

[RootSignature(RootSig)]
[numthreads(8, 8, 4)]
void InjectFogLightingCS(uint3 threadId : SV_DISPATCHTHREADID)
{
	// Compute the sample point inside the froxel. Jittered
	float2 texelUV = (threadId.xy + 0.5f) * cData.InvClusterDimensions.xy;
	float minZ = (float)threadId.z * cData.InvClusterDimensions.z;
	float maxZ = (float)(threadId.z + 1) * cData.InvClusterDimensions.z;
	float minLinearZ = cData.FarZ + Square(saturate(minZ)) * (cData.NearZ - cData.FarZ);
	float maxLinearZ = cData.FarZ + Square(saturate(maxZ)) * (cData.NearZ - cData.FarZ);

	float z = lerp(minLinearZ, maxLinearZ, cData.Jitter);

	float ndcZ = LinearDepthToNDC(z, cData.Projection);
	float3 worldPosition = WorldFromDepth(texelUV, ndcZ, cData.ViewProjectionInv);

	// Calculate Density based on the fog volumes
	float cellThickness = maxLinearZ - minLinearZ;
	float3 cellAbsorption = 0.0f;

	float fogVolumeMaxHeight = 30.0f;
	float densityAtBase = 0.4f;
	float heightAbsorption = cellThickness * exp(min(0.0, fogVolumeMaxHeight - worldPosition.y)) * densityAtBase;

	float3 lightScattering = heightAbsorption;
	float cellDensity = 0.05 * heightAbsorption;

	float3 V = normalize(cData.ViewLocation.xyz - worldPosition);
	float4 pos = float4(texelUV, 0, z);

	float3 totalScattering = 0;

	// Iterate over all the lights and light the froxel
	for(int i = 0; i < cData.NumLights; ++i)
	{
		Light light = tLights[i];
		if(light.VolumetricLighting > 0)
		{
			float attenuation = GetAttenuation(light, worldPosition);
			if(attenuation <= 0.0f)
			{
				continue;
			}

			if(light.ShadowIndex >= 0)
			{
				int shadowIndex = GetShadowIndex(light, pos, worldPosition);
				attenuation *= ShadowNoPCF(worldPosition, shadowIndex, light.InvShadowSize);
				if(light.LightTexture >= 0)
				{
					float4 lightPos = mul(float4(worldPosition, 1), cShadowData.LightViewProjections[shadowIndex]);
					lightPos.xyz /= lightPos.w;
					lightPos.xy = (lightPos.xy + 1) / 2;
					float mask = tTexture2DTable[light.LightTexture].SampleLevel(sClampSampler, lightPos.xy, 0).r;
					attenuation *= mask;
				}
			}

			float3 L = normalize(light.Position - worldPosition);
			if(light.Type == LIGHT_DIRECTIONAL)
			{
				L = -normalize(light.Direction);
			}
			float VdotL = dot(V, L);
			float4 lightColor = light.GetColor() * light.Intensity;

			totalScattering += attenuation * lightColor.xyz * HenyeyGreenstreinPhase(-VdotL, 0.5);
		}
	}

	//totalScattering += ApplyAmbientLight(1, 1, tLights[0].GetColor().rgb * 0.02f).x;

	float4 prevScattering = tLightScattering[threadId];
	float4 newScattering = float4(lightScattering * totalScattering, cellDensity);

	uOutLightScattering[threadId] = lerp(prevScattering, newScattering, 0.05);
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

#if 0
	float maxDepth = asfloat(gsMaxDepth);
	float linearDepth = LinearizeDepth(maxDepth, cData.NearZ, cData.FarZ);
	float volumeDepth = sqrt((linearDepth - cData.FarZ) / (cData.NearZ - cData.FarZ));
	uint lastSlice = ceil((volumeDepth) * cData.ClusterDimensions.z);
#else
	uint lastSlice = cData.ClusterDimensions.z;
#endif

	float3 accumulatedLight = 0;
	float accumulatedTransmittance = 1;

	uOutLightScattering[int3(threadId.xy, 0)] = float4(accumulatedLight, accumulatedTransmittance);

	for(int sliceIndex = 1; sliceIndex <= lastSlice; ++sliceIndex)
	{
		float4 scatteringAndDensity = tLightScattering[int3(threadId.xy, sliceIndex - 1)];
		float transmittance = saturate(exp(-scatteringAndDensity.w));

		float3 scatteringOverSlice = (scatteringAndDensity.xyz - scatteringAndDensity.xyz * transmittance) / max(scatteringAndDensity.w, 0.000001f);
		accumulatedLight += scatteringOverSlice * accumulatedTransmittance;
		accumulatedTransmittance *= transmittance;

		uOutLightScattering[int3(threadId.xy, sliceIndex)] = float4(accumulatedLight, accumulatedTransmittance);
	}
}
