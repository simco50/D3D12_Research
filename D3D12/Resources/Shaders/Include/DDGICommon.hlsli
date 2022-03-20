#include "Common.hlsli"

// Must match with texture size!
#define DDGI_PROBE_IRRADIANCE_TEXELS 6
#define DDGI_PROBE_DEPTH_TEXELS 14
#define DDGI_DYNAMIC_PROBE_OFFSET 1
#define PROBE_GAMMA 5.0f

DDGIVolume GetDDGIVolume(uint index)
{
	StructuredBuffer<DDGIVolume> volumeBuffer = ResourceDescriptorHeap[cView.DDGIVolumesIndex];
	return volumeBuffer[index];
}

float3 GetDDGIProbeIndex3D(DDGIVolume volume, uint index)
{
	return UnFlatten3D(index, volume.ProbeVolumeDimensions);
}

float3 GetDDGIProbePosition(DDGIVolume volume, uint3 index3D)
{
	float3 position = volume.BoundsMin + index3D * volume.ProbeSize;
#if DDGI_DYNAMIC_PROBE_OFFSET
	if(volume.ProbeOffsetIndex != INVALID_HANDLE)
	{
		StructuredBuffer<float4> offsetBuffer = ResourceDescriptorHeap[volume.ProbeOffsetIndex];
		uint index1D = Flatten3D(index3D, volume.ProbeVolumeDimensions);
		position += offsetBuffer[index1D].xyz;
	}
#endif
	return position;
}

uint2 GetDDGIProbeTexel(DDGIVolume volume, uint3 probeIndex3D, uint numProbeInteriorTexels)
{
	uint numProbeTexels = 1 + numProbeInteriorTexels + 1;
	return probeIndex3D.xy * numProbeTexels + uint2(probeIndex3D.z * volume.ProbeVolumeDimensions.x * numProbeTexels, 0) + 1;
}

float2 GetDDGIProbeUV(DDGIVolume volume, uint3 probeIndex3D, float3 direction, uint numProbeInteriorTexels)
{
	uint numProbeTexels = 1 + numProbeInteriorTexels + 1;
	uint textureWidth = numProbeTexels * volume.ProbeVolumeDimensions.x * volume.ProbeVolumeDimensions.z;
	uint textureHeight = numProbeTexels * volume.ProbeVolumeDimensions.y;

	float2 pixel = GetDDGIProbeTexel(volume, probeIndex3D, numProbeInteriorTexels);
	pixel += (EncodeNormalOctahedron(normalize(direction)) * 0.5f + 0.5f) * numProbeInteriorTexels;
	return pixel / float2(textureWidth, textureHeight);
}

float4 SampleDDGIIrradiance(DDGIVolume volume, float3 position, float3 direction)
{
	if(volume.IrradianceIndex == INVALID_HANDLE || volume.DepthIndex == INVALID_HANDLE)
	{
		return 0;
	}

	Texture2D<float4> irradianceTexture = ResourceDescriptorHeap[volume.IrradianceIndex];
	Texture2D<float2> depthTexture = ResourceDescriptorHeap[volume.DepthIndex];

	float volumeWeight = 1.0f;

	// Compute smooth weights of the volume
	float3 relativeCoordindates = (position - volume.BoundsMin) / volume.ProbeSize;
	for(uint i = 0; i < 3; ++i)
	{
		volumeWeight *= lerp(0, 1, saturate(relativeCoordindates[i]));
		if(relativeCoordindates[i] > volume.ProbeVolumeDimensions[i] - 2)
		{
			float x = saturate(relativeCoordindates[i] - volume.ProbeVolumeDimensions[i] + 2);
			volumeWeight *= lerp(1, 0, x);
		}
	}

	if(volumeWeight <= 0.0f)
		return 0.0f;

	uint3 baseProbeCoordinates = floor(relativeCoordindates);
	float3 baseProbePosition = GetDDGIProbePosition(volume, baseProbeCoordinates);
	float3 t = saturate((position - baseProbePosition) / volume.ProbeSize);

	float3 sumIrradiance = 0;
	float sumWeight = 0;

	// Retrieve the irradiance of the probes that form a cage around the location
	for(uint i = 0; i < 8; ++i)
	{
		uint3 indexOffset = uint3(i, i >> 1u, i >> 2u) & 1u;
		uint3 probeCoordinates = clamp(baseProbeCoordinates + indexOffset, 0, volume.ProbeVolumeDimensions - 1);
		float3 probePosition = GetDDGIProbePosition(volume, probeCoordinates);

		float3 relativeProbePosition = position - probePosition + direction * 0.001;
		float3 probeDirection = -normalize(relativeProbePosition);

		float3 interp = lerp(1.0f - t, t, indexOffset);

		float weight = 1;

		// Disregard probes on the other side of the surface we're shading
		weight *= saturate(dot(probeDirection, direction));

		// Visibility check using exponential depth and chebyshev's inequality formula
		// Inspired by variance shadow mapping
		float2 depthUV = GetDDGIProbeUV(volume, probeCoordinates, -probeDirection, DDGI_PROBE_DEPTH_TEXELS);
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

		float2 uv = GetDDGIProbeUV(volume, probeCoordinates, direction, DDGI_PROBE_IRRADIANCE_TEXELS);
		// Remove tone curve and blend in sRGB
		float3 irradiance = pow(irradianceTexture.SampleLevel(sLinearClamp, uv, 0).rgb, PROBE_GAMMA * 0.5f);

		const float crushThreshold = 0.2f;
		if (weight < crushThreshold)
			weight *= weight * weight * (1.0f / Square(crushThreshold));

		weight *= interp.x * interp.y * interp.z;

		sumIrradiance += irradiance * weight;
		sumWeight += weight;
	}

	if(sumWeight == 0)
	{
		return float4(0, 0, 0, volumeWeight);
	}

	sumIrradiance *= (1.0f / sumWeight);
	// Transform back into linear (see note above)
	sumIrradiance *= sumIrradiance;
	sumIrradiance *= 2 * PI;
	return float4(sumIrradiance, volumeWeight);
}

float3 SampleDDGIIrradiance(float3 position, float3 direction)
{
	float4 result = 0;
	for(uint i = 0; i < cView.NumDDGIVolumes && result.a < 0.5f; ++i)
	{
		float4 volSample = SampleDDGIIrradiance(GetDDGIVolume(i), position, direction);
		result.xyz += volSample.rgb * volSample.a;
	}
	return result.rgb;
}
