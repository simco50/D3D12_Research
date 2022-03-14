#include "Common.hlsli"

// Must match with texture size!
#define DDGI_PROBE_IRRADIANCE_TEXELS 8
#define DDGI_PROBE_DEPTH_TEXELS 16
#define DDGI_DYNAMIC_PROBE_OFFSET 1

float3 GetProbeIndex3D(uint index)
{
	return UnFlatten3D(index, cView.DDGIProbeVolumeDimensions);
}

float3 GetProbePosition(uint3 index3D)
{
	float3 position = cView.SceneBoundsMin + index3D * cView.DDGIProbeSize;
#if DDGI_DYNAMIC_PROBE_OFFSET
	if(cView.DDGIProbeOffsetIndex != INVALID_HANDLE)
	{
		StructuredBuffer<float4> offsetBuffer = ResourceDescriptorHeap[cView.DDGIProbeOffsetIndex];
		uint index1D = Flatten3D(index3D, cView.DDGIProbeVolumeDimensions);
		position += offsetBuffer[index1D].xyz;
	}
#endif
	return position;
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

float3 SampleIrradiance(float3 position, float3 direction, Texture2D<float4> irradianceTexture, Texture2D<float2> depthTexture)
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
		float3 probePosition = GetProbePosition(probeCoordinates);

		float3 relativeProbePosition = position - probePosition + direction * 0.001;
		float3 probeDirection = -normalize(relativeProbePosition);

		float3 interp = lerp(1.0f - t, t, indexOffset);

		float weight = 1;

		// Disregard probes on the other side of the surface we're shading
		weight *= saturate(dot(probeDirection, direction));

		// Visibility check using exponential depth and chebyshev's inequality formula
		// Inspired by variance shadow mapping
		float2 depthUV = GetProbeUV(probeCoordinates, -probeDirection, DDGI_PROBE_DEPTH_TEXELS);
		float probeDistance = length(relativeProbePosition);
		// https://developer.download.nvidia.com/SDK/10/direct3d/Source/VarianceShadowMapping/Doc/VarianceShadowMapping.pdf
		float2 moments = depthTexture.SampleLevel(sLinearClamp, depthUV, 0).xy;
		float variance = abs(Square(moments.x) - moments.y);
		float mD = moments.x - probeDistance;
		float mD2 = max(Square(mD), 0);
		float p = variance / (variance + mD2);
		// Sharpen the factor
		p = max(pow(p, 3), 0.0);
		weight *= max(p, probeDistance <= moments.x);

		float2 uv = GetProbeUV(probeCoordinates, direction, DDGI_PROBE_IRRADIANCE_TEXELS);
		float3 irradiance = irradianceTexture.SampleLevel(sLinearClamp, uv, 0).rgb;

		const float crush_threshold = 0.2f;
		if (weight < crush_threshold)
			weight *= weight * weight * (1.0f / Square(crush_threshold));

		weight *= interp.x * interp.y * interp.z;

		sumIrradiance += irradiance * weight;
		sumWeight += weight;
	}

	if(sumWeight > 0)
		return sumIrradiance / sumWeight;
	return 0;
}

float3 SampleIrradiance(float3 position, float3 direction)
{
	if(cView.DDGIIrradianceIndex != INVALID_HANDLE)
	{
		Texture2D<float4> irradianceMap = ResourceDescriptorHeap[cView.DDGIIrradianceIndex];
		Texture2D<float2> depthMap = ResourceDescriptorHeap[cView.DDGIDepthIndex];
		return SampleIrradiance(position, direction, irradianceMap, depthMap);
	}
	return 0;
}
