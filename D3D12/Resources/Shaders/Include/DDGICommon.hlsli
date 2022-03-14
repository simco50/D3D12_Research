#include "Common.hlsli"

// Must match with texture size!
#define DDGI_PROBE_IRRADIANCE_TEXELS 8

float3 GetProbeIndex3D(uint index)
{
	return UnFlatten3D(index, cView.DDGIProbeVolumeDimensions);
}

float3 GetProbePosition(uint3 index3D)
{
	return cView.SceneBoundsMin + index3D * cView.DDGIProbeSize;
}

uint2 GetProbeTexel(uint3 probeIndex3D, uint numProbeInteriorTexels)
{
	uint numProbeTexels = 1 + numProbeInteriorTexels + 1;
	return probeIndex3D.xy * numProbeTexels + uint2(probeIndex3D.z * cView.DDGIProbeVolumeDimensions.x * numProbeTexels, 0) + 1;
}

float2 GetProbeUV(uint3 probeIndex3D, float3 direction, uint numProbeInteriorTexels)
{
	uint numProbeTexels = 1 + numProbeInteriorTexels + 1;
	uint textureWidth = numProbeTexels * cView.DDGIProbeVolumeDimensions.x * cView.DDGIProbeVolumeDimensions.z;
	uint textureHeight = numProbeTexels * cView.DDGIProbeVolumeDimensions.y;

	float2 pixel = GetProbeTexel(probeIndex3D, numProbeInteriorTexels);
	pixel += (EncodeNormalOctahedron(normalize(direction)) * 0.5f + 0.5f) * numProbeInteriorTexels;
	return pixel / float2(textureWidth, textureHeight);
}

float3 SampleIrradiance(float3 position, float3 direction, Texture2D<float4> irradianceTexture)
{
	uint3 baseProbeCoordinates = floor((position - cView.SceneBoundsMin) / cView.DDGIProbeSize);

	float3 baseProbePosition = GetProbePosition(baseProbeCoordinates);
	float3 t = saturate((position - baseProbePosition) / cView.DDGIProbeSize);

	float3 sumIrradiance = 0;
	float sumWeight = 0;

	// Retrieve the irradiance of the probes that form a cage around the location
	for(uint i = 0; i < 8; ++i)
	{
		uint3 indexOffset = uint3(i, i >> 1u, i >> 2u) & 1u;
		uint3 probeCoordinates = clamp(baseProbeCoordinates + indexOffset, 0, cView.DDGIProbeVolumeDimensions - 1);

		float3 interp = lerp(1.0f - t, t, indexOffset);

		float2 uv = GetProbeUV(probeCoordinates, direction, DDGI_PROBE_IRRADIANCE_TEXELS);
		float3 irradiance = irradianceTexture.SampleLevel(sLinearClamp, uv, 0).rgb;
		float weight = interp.x * interp.y * interp.z;

		sumIrradiance += irradiance * weight;
		sumWeight += weight;
	}

	return sumIrradiance / sumWeight;
}

float3 SampleIrradiance(float3 position, float3 direction)
{
	Texture2D<float4> tex = ResourceDescriptorHeap[cView.DDGIIrradianceIndex];
	return SampleIrradiance(position, direction, tex);
}
