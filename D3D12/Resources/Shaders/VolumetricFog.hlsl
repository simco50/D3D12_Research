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
		"StaticSampler(s3, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP)" \

struct ShaderData
{
	float4x4 ViewProjectionInv;
	float4x4 Projection;
	float4x4 PrevViewProjection;
	int3 ClusterDimensions;
	int NumLights;
	float3 InvClusterDimensions;
	float NearZ;
	float3 ViewLocation;
	float FarZ;
	float Jitter;
};

SamplerState sVolumeSampler : register(s3);
ConstantBuffer<ShaderData> cData : register(b0);

Texture3D<float4> tLightScattering : register(t4);
RWTexture3D<float4> uOutLightScattering : register(u0);

float HenyeyGreenstreinPhase(float LoV, float G)
{
	float result = 1.0f - G * G;
	result /= (4.0f * PI * pow(1.0f + G * G - (2.0f * G) * LoV, 1.5f));
	return result;
}

float3 WorldPositionFromFroxel(uint3 index, float offset, out float linearDepth)
{
	float2 texelUV = ((float2)index.xy + 0.5f) * cData.InvClusterDimensions.xy;
	float z = (float)(index.z + offset) * cData.InvClusterDimensions.z;
	linearDepth = cData.FarZ + Square(saturate(z)) * (cData.NearZ - cData.FarZ);
	float ndcZ = LinearDepthToNDC(linearDepth, cData.Projection);
	return WorldFromDepth(texelUV, ndcZ, cData.ViewProjectionInv);
}

float3 WorldPositionFromFroxel(uint3 index, float offset)
{
	float depth;
	return WorldPositionFromFroxel(index, offset, depth);
}

[RootSignature(RootSig)]
[numthreads(8, 8, 4)]
void InjectFogLightingCS(uint3 threadId : SV_DISPATCHTHREADID)
{
	float z;
	float3 worldPosition = WorldPositionFromFroxel(threadId, cData.Jitter, z);

	// Compute reprojected UVW
	float3 reprojWorldPosition = WorldPositionFromFroxel(threadId, 0.5f);
	float4 reprojNDC = mul(float4(reprojWorldPosition, 1), cData.PrevViewProjection);
	reprojNDC.xyz /= reprojNDC.w;
	float3 reprojUV = float3(reprojNDC.x * 0.5f + 0.5f, -reprojNDC.y * 0.5f + 0.5f, reprojNDC.z);
	reprojUV.z = LinearizeDepth(reprojUV.z, cData.NearZ, cData.FarZ);
	reprojUV.z = sqrt((reprojUV.z - cData.FarZ) / (cData.NearZ - cData.FarZ));
	float4 prevScattering = tLightScattering.SampleLevel(sVolumeSampler, reprojUV, 0);

	float3 cellAbsorption = 0.0f;

	// Test exponential height fog
	float fogVolumeMaxHeight = 300.0f;
	float densityAtBase = 0.03f;
	float heightAbsorption = exp(min(0.0, fogVolumeMaxHeight - worldPosition.y)) * densityAtBase;
	float3 lightScattering = heightAbsorption;
	float cellDensity = 0.05 * heightAbsorption;

	float3 V = normalize(cData.ViewLocation.xyz - worldPosition);
	float4 pos = float4(threadId.xy, 0, z);

	float3 totalScattering = 0;

	// Iterate over all the lights and light the froxel
	// todo: Leverage clustered light culling
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
				attenuation *= LightTextureMask(light, shadowIndex, worldPosition);
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

	totalScattering += ApplyAmbientLight(1, 1, tLights[0].GetColor().rgb * 0.005f).x;

	float blendFactor = 0.05f;
	if(any(reprojUV < 0) || any(reprojUV > 1))
	{
		blendFactor = 1.0f;
	}

	float4 newScattering = float4(lightScattering * totalScattering, cellDensity);
	if(blendFactor < 1.0f)
	{
		newScattering = lerp(prevScattering, newScattering, blendFactor);
	}

	uOutLightScattering[threadId] = newScattering;
}

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void AccumulateFogCS(uint3 threadId : SV_DISPATCHTHREADID, uint groupIndex : SV_GROUPINDEX)
{
	float3 accumulatedLight = 0;
	float accumulatedTransmittance = 1;

	float3 previousPosition = WorldPositionFromFroxel(int3(threadId.xy, 0), 0.5f);
	uOutLightScattering[int3(threadId.xy, 0)] = float4(accumulatedLight, accumulatedTransmittance);

	for(int sliceIndex = 1; sliceIndex < cData.ClusterDimensions.z; ++sliceIndex)
	{
		float3 worldPosition = WorldPositionFromFroxel(int3(threadId.xy, sliceIndex), 0.5f);
		float froxelLength = length(worldPosition - previousPosition);
		previousPosition = worldPosition;

		float4 scatteringAndDensity = tLightScattering[int3(threadId.xy, sliceIndex - 1)];
		float transmittance = exp(-scatteringAndDensity.w * froxelLength);

		float3 scatteringOverSlice = (scatteringAndDensity.xyz - scatteringAndDensity.xyz * transmittance) / max(scatteringAndDensity.w, 0.000001f);
		accumulatedLight += scatteringOverSlice * accumulatedTransmittance;
		accumulatedTransmittance *= transmittance;

		uOutLightScattering[int3(threadId.xy, sliceIndex)] = float4(accumulatedLight, accumulatedTransmittance);
	}
}
