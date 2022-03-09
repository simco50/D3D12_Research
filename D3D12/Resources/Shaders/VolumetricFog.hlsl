#include "Common.hlsli"
#include "Lighting.hlsli"

struct PassData
{
	uint3 ClusterDimensions;
	float Jitter;
	float3 InvClusterDimensions;
	float LightClusterSizeFactor;
	float2 LightGridParams;
	uint2 LightClusterDimensions;
};

ConstantBuffer<PassData> cPass : register(b0);

StructuredBuffer<uint> tLightGrid : register(t0);
StructuredBuffer<uint> tLightIndexList : register(t1);
Texture3D<float4> tLightScattering : register(t2);
RWTexture3D<float4> uOutLightScattering : register(u0);

float HenyeyGreenstreinPhase(float LoV, float G)
{
	float result = 1.0f - G * G;
	result /= (4.0f * PI * pow(1.0f + G * G - (2.0f * G) * LoV, 1.5f));
	return result;
}

float3 GetWorldPosition(uint3 index, float offset, out float linearDepth)
{
	float2 texelUV = ((float2)index.xy + 0.5f) * cPass.InvClusterDimensions.xy;
	float z = (float)(index.z + offset) * cPass.InvClusterDimensions.z;
	linearDepth = cView.FarZ + Square(saturate(z)) * (cView.NearZ - cView.FarZ);
	float ndcZ = LinearDepthToNDC(linearDepth, cView.Projection);
	return WorldFromDepth(texelUV, ndcZ, cView.ViewProjectionInverse);
}

float3 GetWorldPosition(uint3 index, float offset)
{
	float depth;
	return GetWorldPosition(index, offset, depth);
}

uint GetLightClusterSliceFromDepth(float depth)
{
	return floor(log(depth) * cPass.LightGridParams.x - cPass.LightGridParams.y);
}

uint GetLightCluster(uint2 fogCellIndex, float depth)
{
	uint slice = GetLightClusterSliceFromDepth(depth);
	uint3 clusterIndex3D = uint3(floor(fogCellIndex * cPass.LightClusterSizeFactor), slice);
	return Flatten3D(clusterIndex3D, uint3(cPass.LightClusterDimensions, 0));
}

struct FogVolume
{
	float3 Location;
	float3 Extents;
	float3 Color;
	float DensityChange;
	float DensityBase;
};

[numthreads(8, 8, 4)]
void InjectFogLightingCS(uint3 threadId : SV_DispatchThreadID)
{
	uint3 cellIndex = threadId;

	float z;
	float3 worldPosition = GetWorldPosition(cellIndex, cPass.Jitter, z);

	// Compute reprojected UVW
	float3 voxelCenterWS = GetWorldPosition(cellIndex, 0.5f);
	float4 reprojNDC = mul(float4(voxelCenterWS, 1), cView.PreviousViewProjection);
	reprojNDC.xyz /= reprojNDC.w;
	float3 reprojUV = float3(reprojNDC.x * 0.5f + 0.5f, -reprojNDC.y * 0.5f + 0.5f, reprojNDC.z);
	reprojUV.z = LinearizeDepth(reprojUV.z);
	reprojUV.z = sqrt((reprojUV.z - cView.FarZ) / (cView.NearZ - cView.FarZ));
	float4 prevScattering = tLightScattering.SampleLevel(sLinearClamp, reprojUV, 0);

	float3 inScattering = 0.0f;
	float3 cellAbsorption = 0.0f;
	float cellDensity = 0.0f;

	const int numFogVolumes = 1;
	FogVolume fogVolumes[numFogVolumes];
	fogVolumes[0].Location = float3(0, 1, 0);
	fogVolumes[0].Extents = float3(100, 100, 100);
	fogVolumes[0].Color = float3(1, 1, 1);
	fogVolumes[0].DensityBase = 0;
	fogVolumes[0].DensityChange = 0.1f;

	uint i;
	for(i = 0; i < numFogVolumes; ++i)
	{
		FogVolume fogVolume = fogVolumes[i];

		float3 posFogLocal = (worldPosition - fogVolume.Location) / fogVolume.Extents;
		float heightNormalized = posFogLocal.y * 0.5f + 0.5f;
		if(min3(posFogLocal.x, posFogLocal.y, posFogLocal.z) < -1 || max3(posFogLocal.x, posFogLocal.y, posFogLocal.z) > 1)
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

	if(dot(inScattering, float3(1, 1, 1)) > 0.0f)
	{
		float3 V = normalize(cView.ViewPosition.xyz - worldPosition);
		float4 pos = float4(threadId.xy, 0, z);

		// Iterate over all the lights and light the froxel
		uint tileIndex = GetLightCluster(threadId.xy, z);
		uint lightOffset = tLightGrid[tileIndex * 2];
		uint lightCount = tLightGrid[tileIndex * 2 + 1];

		for(i = 0; i < lightCount; ++i)
		{
			int lightIndex = tLightIndexList[lightOffset + i];
			Light light = GetLight(lightIndex);
			if(light.IsEnabled && light.IsVolumetric)
			{
				float attenuation = GetAttenuation(light, worldPosition);
				if(attenuation <= 0.0f)
				{
					continue;
				}

				if(light.CastShadows)
				{
					int shadowIndex = GetShadowIndex(light, pos, worldPosition);
					attenuation *= ShadowNoPCF(worldPosition, shadowIndex, light.InvShadowSize);
					attenuation *= LightTextureMask(light, shadowIndex, worldPosition);
				}

				float3 L = normalize(light.Position - worldPosition);
				if(light.IsDirectional)
				{
					L = normalize(light.Direction);
				}
				float VdotL = dot(V, L);
				float4 lightColor = light.GetColor() * light.Intensity;

				totalLighting += attenuation * lightColor.xyz * saturate(HenyeyGreenstreinPhase(VdotL, 0.3f));
			}
		}
	}

	totalLighting += ApplyAmbientLight(1, 1).x;

	float blendFactor = 0.05f;
	if(any(reprojUV < 0.05f) || any(reprojUV > 0.95f))
	{
		blendFactor = 1.0f;
	}

	float4 newScattering = float4(inScattering * totalLighting, cellDensity);
	if(blendFactor < 1.0f)
	{
		newScattering = lerp(prevScattering, newScattering, blendFactor);
	}

	uOutLightScattering[threadId] = newScattering;
}

[numthreads(8, 8, 1)]
void AccumulateFogCS(uint3 threadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
	float3 accumulatedLight = 0;
	float accumulatedTransmittance = 1;
	float3 previousPosition = cView.ViewPosition.xyz;

	for(int sliceIndex = 0; sliceIndex < cPass.ClusterDimensions.z; ++sliceIndex)
	{
		float3 worldPosition = GetWorldPosition(int3(threadId.xy, sliceIndex), 0.5f);
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
