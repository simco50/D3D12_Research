#include "Common.hlsli"

// Must match with texture size!
#define DDGI_PROBE_IRRADIANCE_TEXELS 6
#define DDGI_PROBE_DEPTH_TEXELS 14

#define DDGI_DYNAMIC_PROBE_OFFSET 1
#define DDGI_USE_PROBE_STATES 1
// Maximum amount of rays to cast per probe that are temporally stable used to determine probe state.
#define DDGI_NUM_STABLE_RAYS 32

#define DDGI_PROBE_GAMMA 5.0f
#define DDGI_BACKFACE_DEPTH_MULTIPLIER -0.2f

// Ray Tracing Gems 2: Essential Ray Generation Shaders
float3 SphericalFibonacci(float i, float n)
{
	const float PHI = sqrt(5) * 0.5f + 0.5f;
	float fraction = (i * (PHI - 1)) - floor(i * (PHI - 1));
	float phi = 2.0f * PI * fraction;
	float cosTheta = 1.0f - (2.0f * i + 1.0f) * (1.0f / n);
	float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
	return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

DDGIVolume GetDDGIVolume(uint index)
{
	StructuredBuffer<DDGIVolume> volumeBuffer = ResourceDescriptorHeap[cView.DDGIVolumesIndex];
	return volumeBuffer[index];
}

bool DDGIIsProbeActive(DDGIVolume volume, uint3 index3D)
{
#if DDGI_USE_PROBE_STATES
	uint index1D = Flatten3D(index3D, volume.ProbeVolumeDimensions);
	Buffer<uint> stateBuffer = ResourceDescriptorHeap[volume.ProbeStatesIndex];
	return stateBuffer[index1D] == 0;
#else
	return true;
#endif
}

float3 DDGIGetRayDirection(uint rayIndex, uint numRays, float3x3 randomRotation = IDENTITY_MATRIX_3)
{
	if(rayIndex < DDGI_NUM_STABLE_RAYS)
	{
		return SphericalFibonacci(rayIndex, min(DDGI_NUM_STABLE_RAYS, numRays));
	}
	return mul(SphericalFibonacci(rayIndex - DDGI_NUM_STABLE_RAYS, numRays - DDGI_NUM_STABLE_RAYS), randomRotation);
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
		Buffer<float4> offsetBuffer = ResourceDescriptorHeap[volume.ProbeOffsetIndex];
		uint index1D = Flatten3D(index3D, volume.ProbeVolumeDimensions);
		position += offsetBuffer[index1D].xyz;
	}
#endif
	return position;
}

uint2 GetDDGIProbeTexel(DDGIVolume volume, uint3 probeIndex3D, uint numProbeInteriorTexels)
{
	uint numProbeTexels = 1 + numProbeInteriorTexels + 1;
	return uint2(probeIndex3D.x + probeIndex3D.y * volume.ProbeVolumeDimensions.x, probeIndex3D.z) * numProbeTexels + 1;
}

float2 GetDDGIProbeUV(DDGIVolume volume, uint3 probeIndex3D, float3 direction, uint numProbeInteriorTexels)
{
	uint numProbeTexels = 1 + numProbeInteriorTexels + 1;
	uint textureWidth = numProbeTexels * volume.ProbeVolumeDimensions.x * volume.ProbeVolumeDimensions.y;
	uint textureHeight = numProbeTexels * volume.ProbeVolumeDimensions.z;

	float2 pixel = GetDDGIProbeTexel(volume, probeIndex3D, numProbeInteriorTexels);
	pixel += (EncodeNormalOctahedron(normalize(direction)) * 0.5f + 0.5f) * numProbeInteriorTexels;
	return pixel / float2(textureWidth, textureHeight);
}

float3 DDGIComputeBias(DDGIVolume volume, float3 normal, float3 viewDirection, float b = 0.2f)
{
	const float normalBiasMultiplier = 0.2f;
	const float viewBiasMultiplier = 0.8f;
	const float axialDistanceMultiplier = 0.75f;
	return (normal * normalBiasMultiplier + viewDirection * viewBiasMultiplier) *
		axialDistanceMultiplier * Min3(volume.ProbeSize) * b;
}

float4 SampleDDGIIrradiance(DDGIVolume volume, float3 position, float3 direction, float3 cameraDirection)
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

	position += DDGIComputeBias(volume, direction, -cameraDirection, 0.2f);

	uint3 baseProbeCoordinates = floor(relativeCoordindates);
	float3 baseProbePosition = GetDDGIProbePosition(volume, baseProbeCoordinates);
	float3 alpha = saturate((position - baseProbePosition) / volume.ProbeSize);

	float3 sumIrradiance = 0;
	float sumWeight = 0;

	// Retrieve the irradiance of the probes that form a cage around the location
	for(uint i = 0; i < 8; ++i)
	{
		uint3 indexOffset = uint3(i, i >> 1u, i >> 2u) & 1u;

		uint3 probeCoordinates = clamp(baseProbeCoordinates + indexOffset, 0, volume.ProbeVolumeDimensions - 1);
		if(!DDGIIsProbeActive(volume, probeCoordinates))
		{
			continue;
		}

		float3 probePosition = GetDDGIProbePosition(volume, probeCoordinates);

		float3 relativeProbePosition = position - probePosition;
		float3 probeDirection = -normalize(relativeProbePosition);

		float3 trilinear = max(0.001f, lerp(1.0f - alpha, alpha, indexOffset));
        float trilinearWeight = (trilinear.x * trilinear.y * trilinear.z);

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
		float chebyshev = 1.0f;
		if(probeDistance > moments.x)
		{
			float mD = moments.x - probeDistance;
			chebyshev = variance / (variance + Square(mD));
			// Sharpen the factor
			chebyshev = max(chebyshev * chebyshev * chebyshev, 0.0);
		}
		weight *= max(chebyshev, 0.05f);
		weight = max(0.000001f, weight);

		const float crushThreshold = 0.2f;
		if (weight < crushThreshold)
		{
			weight *= weight * weight * (1.0f / Square(crushThreshold));
		}
		weight *= trilinearWeight;

		float2 uv = GetDDGIProbeUV(volume, probeCoordinates, direction, DDGI_PROBE_IRRADIANCE_TEXELS);
		// Remove tone curve and blend in sRGB
		float3 irradiance = irradianceTexture.SampleLevel(sLinearClamp, uv, 0).rgb;
		irradiance = pow(irradiance, DDGI_PROBE_GAMMA * 0.5f);

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

float3 SampleDDGIIrradiance(float3 position, float3 direction, float3 cameraDirection)
{
	float4 result = 0;
	for(uint i = 0; i < cView.NumDDGIVolumes && result.a < 0.5f; ++i)
	{
		float4 volSample = SampleDDGIIrradiance(GetDDGIVolume(i), position, direction, cameraDirection);
		result.xyz += volSample.rgb * volSample.a;
	}
	return result.rgb;
}
