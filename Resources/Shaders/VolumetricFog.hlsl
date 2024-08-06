#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Volumetrics.hlsli"
#include "RayTracing/DDGICommon.hlsli"
#include "Noise.hlsli"

struct InjectParams
{
	uint3 ClusterDimensions;
	float Jitter;
	float3 InvClusterDimensions;
	float LightClusterSizeFactor;
	float2 LightGridParams;
	uint2 LightClusterDimensions;
	float MinBlendFactor;
	uint NumFogVolumes;
};

struct AccumulateParams
{
	uint3 ClusterDimensions;
	float3 InvClusterDimensions;
};

ConstantBuffer<InjectParams> cInjectParams : register(b0);
ConstantBuffer<InjectParams> cAccumulateParams : register(b0);

StructuredBuffer<FogVolume> tFogVolumes : register(t0);
Buffer<uint> tLightGrid : register(t1);
Texture3D<float4> tLightScattering : register(t2);

RWTexture3D<float4> uOutLightScattering : register(u0);

float3 GetWorldPosition(uint3 index, float offset, float3 clusterDimensionsInv, out float linearDepth)
{
	float2 texelUV = ((float2)index.xy + 0.5f) * clusterDimensionsInv.xy;
	float z = (float)(index.z + offset) * clusterDimensionsInv.z;
	linearDepth = cView.FarZ + Square(saturate(z)) * (cView.NearZ - cView.FarZ);
	float ndcZ = LinearDepthToNDC(linearDepth, cView.ViewToClip);
	return WorldPositionFromDepth(texelUV, ndcZ, cView.ClipToWorld);
}

float3 GetWorldPosition(uint3 index, float offset, float3 clusterDimensionsInv)
{
	float depth;
	return GetWorldPosition(index, offset, clusterDimensionsInv, depth);
}

uint GetLightClusterSliceFromDepth(float depth)
{
	return floor(log(depth) * cInjectParams.LightGridParams.x - cInjectParams.LightGridParams.y);
}

uint GetLightCluster(uint2 fogCellIndex, float depth)
{
	uint slice = GetLightClusterSliceFromDepth(depth);
	uint3 clusterIndex3D = uint3(floor(fogCellIndex * cInjectParams.LightClusterSizeFactor), slice);
	return Flatten3D(clusterIndex3D, cInjectParams.LightClusterDimensions);
}

[numthreads(8, 8, 4)]
void InjectFogLightingCS(uint3 threadId : SV_DispatchThreadID)
{
	uint3 cellIndex = threadId;

	float z;
	float3 worldPosition = GetWorldPosition(cellIndex, cInjectParams.Jitter, cInjectParams.InvClusterDimensions, z);

	// Compute reprojected UVW
	float3 voxelCenterWS = GetWorldPosition(cellIndex, 0.5f, cInjectParams.InvClusterDimensions);
	float4 reprojNDC = mul(float4(voxelCenterWS, 1), cView.WorldToClipPrev);
	reprojNDC.xyz /= reprojNDC.w;
	float3 reprojUV = float3(reprojNDC.x * 0.5f + 0.5f, -reprojNDC.y * 0.5f + 0.5f, reprojNDC.z);
	reprojUV.z = LinearizeDepth(reprojUV.z);
	reprojUV.z = sqrt((reprojUV.z - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 prevScattering = tLightScattering.SampleLevel(sLinearClamp, reprojUV, 0);

	float3 inScattering = 0.0f;
	float3 cellAbsorption = 0.0f;
	float cellDensity = 0.0f;

	uint i;
	for(i = 0; i < cInjectParams.NumFogVolumes; ++i)
	{
		FogVolume fogVolume = tFogVolumes[i];

		float3 posFogLocal = (worldPosition - fogVolume.Location) / fogVolume.Extents;
		float heightNormalized = posFogLocal.y * 0.5f + 0.5f;
		if(Min(posFogLocal.x, posFogLocal.y, posFogLocal.z) < -1 || Max(posFogLocal.x, posFogLocal.y, posFogLocal.z) > 1)
		{
			continue;
		}

		float density = min(1.0f, fogVolume.DensityBase + Square(1.0f - heightNormalized) * fogVolume.DensityChange);

		if(density < 0.0f)
		{
			continue;
		}

		cellAbsorption += density * fogVolume.Color;
		cellDensity += density;
	}

	inScattering = cellAbsorption;
	cellDensity = cellDensity;

	float3 totalLighting = 0;
	float dither = InterleavedGradientNoise(threadId.xy);

	float3 V = normalize(cView.ViewLocation - worldPosition);
	if(dot(inScattering, float3(1, 1, 1)) > 0.0f)
	{
		// Iterate over all the lights and light the froxel
		uint tileIndex = GetLightCluster(threadId.xy, z);
		uint lightGridOffset = tileIndex * CLUSTERED_LIGHTING_NUM_BUCKETS;

		for(uint bucketIndex = 0; bucketIndex < CLUSTERED_LIGHTING_NUM_BUCKETS; ++bucketIndex)
		{
			uint bucket = tLightGrid[lightGridOffset + bucketIndex];
			while(bucket)
			{
				uint bitIndex = firstbitlow(bucket);
				bucket ^= 1u << bitIndex;

				uint lightIndex = bitIndex + bucketIndex * 32;
				Light light = GetLight(lightIndex);

				if(light.IsEnabled && light.IsVolumetric)
				{
					float3 L;
					float attenuation = GetAttenuation(light, worldPosition, L);
					if(attenuation <= 0.0f)
						continue;

					if(light.CastShadows)
					{
						int shadowIndex = GetShadowMapIndex(light, worldPosition, z, dither);
						attenuation *= ShadowNoPCF(worldPosition, light.MatrixIndex + shadowIndex, light.ShadowMapIndex + shadowIndex, light.InvShadowSize);
					}
					if(attenuation <= 0.0f)
						continue;

					float VdotL = dot(V, L);
					totalLighting += attenuation * light.GetColor() * saturate(HenyeyGreenstreinPhase(VdotL, 0.3f));
				}
			}
		}
	}

	float blendFactor = 0.05f;
	if(any(reprojUV < 0.0f) || any(reprojUV > 1.0f))
		blendFactor = 0.25f;

	blendFactor = max(cInjectParams.MinBlendFactor, blendFactor);

	float4 newScattering = float4(inScattering * totalLighting, cellDensity);
	newScattering = lerp(prevScattering, newScattering, blendFactor);

	uOutLightScattering[threadId] = newScattering;
}

[numthreads(8, 8, 1)]
void AccumulateFogCS(uint3 threadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
	float3 accumulatedLight = 0;
	float accumulatedTransmittance = 1;
	float3 previousPosition = cView.ViewLocation;

	for(int sliceIndex = 0; sliceIndex < cAccumulateParams.ClusterDimensions.z; ++sliceIndex)
	{
		float3 worldPosition = GetWorldPosition(int3(threadId.xy, sliceIndex), 0.5f, cAccumulateParams.InvClusterDimensions);
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
