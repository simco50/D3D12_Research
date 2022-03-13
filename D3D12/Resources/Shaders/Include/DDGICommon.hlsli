#include "Common.hlsli"

#define MAX_RAYS_PER_PROBE 128
#define PROBE_TEXEL_RESOLUTION 8
#define PROBE_TEXEL_RESOLUTION_FULL (PROBE_TEXEL_RESOLUTION + 2)

float3 GetProbeIndex3D(uint index)
{
	return UnFlatten3D(index, cView.ProbeVolumeDimensions);
}

float3 GetProbePosition(uint3 index3D)
{
	return cView.SceneBoundsMin + index3D * cView.ProbeSize;
}

uint2 GetProbeTexel(uint3 probeIndex3D)
{
	return probeIndex3D.xz * PROBE_TEXEL_RESOLUTION_FULL + uint2(probeIndex3D.y * cView.ProbeVolumeDimensions.x * PROBE_TEXEL_RESOLUTION_FULL, 0) + 1;
}

float2 GetProbeUV(uint3 probeIndex3D, float3 direction)
{
	uint textureWidth = PROBE_TEXEL_RESOLUTION_FULL * cView.ProbeVolumeDimensions.x * cView.ProbeVolumeDimensions.y;
	uint textureHeight = PROBE_TEXEL_RESOLUTION_FULL * cView.ProbeVolumeDimensions.z;

	float2 pixel = GetProbeTexel(probeIndex3D);
	pixel += (EncodeNormalOctahedron(normalize(direction)) * 0.5f + 0.5f) * PROBE_TEXEL_RESOLUTION;
	return pixel / float2(textureWidth, textureHeight);
}

float3 SampleIrradiance(float3 position, float3 direction, Texture2D<float4> irradianceTexture)
{
	uint3 baseProbeCoordinates = floor((position - cView.SceneBoundsMin) / cView.ProbeSize);

	float3 baseProbePosition = GetProbePosition(baseProbeCoordinates);
	float3 t = saturate((position - baseProbePosition) / cView.ProbeSize);

	float3 sumIrradiance = 0;
	float sumWeight = 0;

	// Retrieve the irradiance of the probes that form a cage around the location
	for(uint i = 0; i < 8; ++i)
	{
		uint3 indexOffset = uint3(i, i >> 1u, i >> 2u) & 1u;
		uint3 probeCoordinates = clamp(baseProbeCoordinates + indexOffset, 0, cView.ProbeVolumeDimensions - 1);

		float3 interp = lerp(1.0f - t, t, indexOffset);

		float2 uv = GetProbeUV(probeCoordinates, direction);
		float3 irradiance = irradianceTexture.SampleLevel(sLinearClamp, uv, 0).rgb;
		float weight = interp.x * interp.y * interp.z;

		sumIrradiance += irradiance * weight;
		sumWeight += weight;
	}

	return sumIrradiance / sumWeight;
}
