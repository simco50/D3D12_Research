#include "Common.hlsli"
#include "RayTracingCommon.hlsli"
#include "Lighting.hlsli"
#include "Primitives.hlsli"
#include "DDGICommon.hlsli"
#include "Random.hlsli"

struct PassParameters
{
	float3 RandomVector;
	float RandomAngle;
	float HistoryBlendWeight;
	uint VolumeIndex;
};

ConstantBuffer<PassParameters> cPass : register(b0);

RWTexture2D<float4> uIrradianceMap : register(u0);
RWTexture2D<float2> uDepthMap : register(u0);
RWTexture2D<float4> uVisualizeTexture : register(u0);
RWBuffer<float4> uProbeOffsets : register(u1);
RWBuffer<uint> uProbeStates : register(u0);

Buffer<float4> tRayHitInfo : register(t0);

/**
	- UpdateIrradiance -
	Store Irradiance data in texture atlas
*/

#define MIN_WEIGHT_THRESHOLD 0.0001f

// Precompute border texel copy source/destination for 6x6 probe size
static const uint NUM_COLOR_BORDER_TEXELS = DDGI_PROBE_IRRADIANCE_TEXELS * 4 + 4;
static const uint4 DDGI_COLOR_BORDER_OFFSETS[NUM_COLOR_BORDER_TEXELS] = {
	uint4(6, 6, 0, 0), // TL Corner
	uint4(6, 1, 1, 0), uint4(5, 1, 2, 0), uint4(4, 1, 3, 0), uint4(3, 1, 4, 0), uint4(2, 1, 5, 0), uint4(1, 1, 6, 0), // Top border
	uint4(1, 6, 7, 0), // TR Corner
	uint4(1, 6, 0, 1), uint4(1, 5, 0, 2), uint4(1, 4, 0, 3), uint4(1, 3, 0, 4), uint4(1, 2, 0, 5), uint4(1, 1, 0, 6), // Right border
	uint4(6, 1, 0, 7), // BL Corner
	uint4(6, 6, 7, 1), uint4(6, 5, 7, 2), uint4(6, 4, 7, 3), uint4(6, 3, 7, 4), uint4(6, 2, 7, 5), uint4(6, 1, 7, 6), // Left border
	uint4(1, 1, 7, 7), // BR Corner
	uint4(6, 6, 1, 7), uint4(5, 6, 2, 7), uint4(4, 6, 3, 7), uint4(3, 6, 4, 7), uint4(2, 6, 5, 7), uint4(1, 6, 6, 7) // Bottom border
};

#define IRRADIANCE_RAY_HIT_GS_SIZE DDGI_PROBE_IRRADIANCE_TEXELS * DDGI_PROBE_IRRADIANCE_TEXELS
groupshared float3 gsRadianceCache_Irradiance[IRRADIANCE_RAY_HIT_GS_SIZE];
groupshared float3 gsDirectionCache_Irradiance[IRRADIANCE_RAY_HIT_GS_SIZE];

[numthreads(DDGI_PROBE_IRRADIANCE_TEXELS, DDGI_PROBE_IRRADIANCE_TEXELS, 1)]
void UpdateIrradianceCS(
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupId : SV_GroupID,
	uint groupIndex : SV_GroupIndex)
{
	DDGIVolume volume = GetDDGIVolume(cPass.VolumeIndex);
	uint probeIdx = groupId;
	uint3 probeCoordinates = GetDDGIProbeIndex3D(volume, probeIdx);

	// Early exit could be dangerous here as we have rely on groupshared memory
	// However the entire group will short-circuit if the probe is inactive so should be fine.
	if(!DDGIIsProbeActive(volume, probeCoordinates))
		return;

	uint2 texelLocation = GetDDGIProbeTexel(volume, probeCoordinates, DDGI_PROBE_IRRADIANCE_TEXELS);
	uint2 cornerTexelLocation = texelLocation - 1u;
	texelLocation += groupThreadId.xy;
	Texture2D<float4> tIrradianceMap = ResourceDescriptorHeap[volume.IrradianceIndex];
	float3 prevRadiance = tIrradianceMap[texelLocation].rgb;
	float3 probeDirection = Octahedral::Unpack(((groupThreadId.xy + 0.5f) / (float)DDGI_PROBE_IRRADIANCE_TEXELS) * 2 - 1);
	float3x3 randomRotation = AngleAxis3x3(cPass.RandomAngle, cPass.RandomVector);

	float weightSum = 0;
	float3 sum = 0;

	uint rayIndex = DDGI_NUM_STABLE_RAYS;
	while(rayIndex < volume.NumRaysPerProbe)
	{
		uint rayCount = min(IRRADIANCE_RAY_HIT_GS_SIZE, volume.NumRaysPerProbe - rayIndex);
		if(groupIndex < rayCount)
		{
			gsRadianceCache_Irradiance[groupIndex] = tRayHitInfo[probeIdx * volume.MaxRaysPerProbe + rayIndex + groupIndex].rgb;
			gsDirectionCache_Irradiance[groupIndex] = DDGIGetRayDirection(rayIndex + groupIndex, volume.NumRaysPerProbe, randomRotation);
		}
		GroupMemoryBarrierWithGroupSync();

		for(uint i = 0; i < rayCount; ++i)
		{
			float3 radiance = gsRadianceCache_Irradiance[i].rgb;
			float3 direction = gsDirectionCache_Irradiance[i];
			float weight = saturate(dot(probeDirection, direction));
			sum += weight * radiance;
			weightSum += weight;
		}
		rayIndex += IRRADIANCE_RAY_HIT_GS_SIZE;
	}

    const float epsilon = 1e-9f * float(volume.NumRaysPerProbe);
	sum *= 1.0f / max(2.0f * weightSum, epsilon);

	// Apply tone curve for better encoding
	sum = pow(sum, rcp(DDGI_PROBE_GAMMA));
	const float historyBlendWeight = saturate(1.0f - cPass.HistoryBlendWeight);
	sum = lerp(prevRadiance, sum, historyBlendWeight);

	uIrradianceMap[texelLocation] = float4(sum, 1);

	GroupMemoryBarrierWithGroupSync();

	// Extend the borders of the probes to fix sampling issues at the edges
	for (uint index = groupIndex; index < NUM_COLOR_BORDER_TEXELS; index += DDGI_PROBE_IRRADIANCE_TEXELS * DDGI_PROBE_IRRADIANCE_TEXELS)
	{
		uint2 sourceIndex = cornerTexelLocation + DDGI_COLOR_BORDER_OFFSETS[index].xy;
		uint2 targetIndex = cornerTexelLocation + DDGI_COLOR_BORDER_OFFSETS[index].zw;
		uIrradianceMap[targetIndex] = uIrradianceMap[sourceIndex];
	}
}

// Precompute border texel copy source/destination for 14x14 probe size
static const uint NUM_DEPTH_BORDER_TEXELS = DDGI_PROBE_DEPTH_TEXELS * 4 + 4;
static const uint4 DDGI_DEPTH_BORDER_OFFSETS[NUM_DEPTH_BORDER_TEXELS] = {
	uint4(14, 14, 0, 0), // TL Corner
	uint4(14, 1, 1, 0), uint4(13, 1, 2, 0), uint4(12, 1, 3, 0), uint4(11, 1, 4, 0), uint4(10, 1, 5, 0), uint4(9, 1, 6, 0), uint4(8, 1, 7, 0), // Top border
	uint4(7, 1, 8, 0), uint4(6, 1, 9, 0), uint4(5, 1, 10, 0), uint4(4, 1, 11, 0), uint4(3, 1, 12, 0), uint4(2, 1, 13, 0), uint4(1, 1, 14, 0),
	uint4(1, 14, 15, 0), // TR Corner
	uint4(1, 14, 0, 1), uint4(1, 13, 0, 2), uint4(1, 12, 0, 3), uint4(1, 11, 0, 4), uint4(1, 10, 0, 5), uint4(1, 9, 0, 6), uint4(1, 8, 0, 7), // Right border
	uint4(1, 7, 0, 8), uint4(1, 6, 0, 9), uint4(1, 5, 0, 10), uint4(1, 4, 0, 11), uint4(1, 3, 0, 12), uint4(1, 2, 0, 13), uint4(1, 1, 0, 14),

	uint4(14, 1, 0, 15), // BL Corner
	uint4(14, 14, 15, 1), uint4(14, 13, 15, 2), uint4(14, 12, 15, 3), uint4(14, 11, 15, 4), uint4(14, 10, 15, 5), uint4(14, 9, 15, 6), uint4(14, 8, 15, 7), // Left border
	uint4(14, 7, 15, 8), uint4(14, 6, 15, 9), uint4(14, 5, 15, 10), uint4(14, 4, 15, 11), uint4(14, 3, 15, 12), uint4(14, 2, 15, 13), uint4(14, 1, 15, 14),

	uint4(1, 1, 15, 15), // BR Corner
	uint4(14, 14, 1, 15), uint4(13, 14, 2, 15), uint4(12, 14, 3, 15), uint4(11, 14, 4, 15), uint4(10, 14, 5, 15), uint4(9, 14, 6, 15), uint4(8, 14, 7, 15), // Bottom border
	uint4(7, 14, 8, 15), uint4(6, 14, 9, 15), uint4(5, 14, 10, 15), uint4(4, 14, 11, 15), uint4(3, 14, 12, 15), uint4(2, 14, 13, 15), uint4(1, 14, 14, 15),
};

#define DEPTH_RAY_HIT_GS_SIZE DDGI_PROBE_DEPTH_TEXELS * DDGI_PROBE_DEPTH_TEXELS
groupshared float gsDepthCache_Depth[DEPTH_RAY_HIT_GS_SIZE];
groupshared float3 gsDirectionCache_Depth[DEPTH_RAY_HIT_GS_SIZE];

[numthreads(DDGI_PROBE_DEPTH_TEXELS, DDGI_PROBE_DEPTH_TEXELS, 1)]
void UpdateDepthCS(
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupId : SV_GroupID,
	uint groupIndex : SV_GroupIndex)
{
	DDGIVolume volume = GetDDGIVolume(cPass.VolumeIndex);
	uint probeIdx = groupId;
	uint3 probeCoordinates = GetDDGIProbeIndex3D(volume, probeIdx);

	// Early exit could be dangerous here as we have rely on groupshared memory
	// However the entire group will short-circuit if the probe is inactive so should be fine.
	if(!DDGIIsProbeActive(volume, probeCoordinates))
		return;

	uint2 texelLocation = GetDDGIProbeTexel(volume, probeCoordinates, DDGI_PROBE_DEPTH_TEXELS);
	uint2 cornerTexelLocation = texelLocation - 1u;
	texelLocation += groupThreadId.xy;
	Texture2D<float2> tDepthMap = ResourceDescriptorHeap[volume.DepthIndex];
	float2 prevDepth = tDepthMap[texelLocation];
	float3 probeDirection = Octahedral::Unpack(((groupThreadId.xy + 0.5f) / (float)DDGI_PROBE_DEPTH_TEXELS) * 2 - 1);
	float3x3 randomRotation = AngleAxis3x3(cPass.RandomAngle, cPass.RandomVector);

	float weightSum = 0;
	float2 sum = 0;

	uint rayIndex = DDGI_NUM_STABLE_RAYS;
	while(rayIndex < volume.NumRaysPerProbe)
	{
		uint rayCount = min(DEPTH_RAY_HIT_GS_SIZE, volume.NumRaysPerProbe - rayIndex);
		if(groupIndex < rayCount)
		{
			gsDepthCache_Depth[groupIndex] = tRayHitInfo[probeIdx * volume.MaxRaysPerProbe + rayIndex + groupIndex].a;
			gsDirectionCache_Depth[groupIndex] = DDGIGetRayDirection(rayIndex + groupIndex, volume.NumRaysPerProbe, randomRotation);
		}
		GroupMemoryBarrierWithGroupSync();

		for(uint i = 0; i < rayCount; ++i)
		{
			float depth = gsDepthCache_Depth[i];
			float3 direction = gsDirectionCache_Depth[i];
			float weight = saturate(dot(probeDirection, direction));
			weight = pow(weight, 64);
			if(weight > MIN_WEIGHT_THRESHOLD)
			{
				weightSum += weight;
				sum += float2(abs(depth), Square(depth)) * weight;
			}
		}
		rayIndex += DEPTH_RAY_HIT_GS_SIZE;
	}

	if(weightSum > MIN_WEIGHT_THRESHOLD)
	{
		sum /= weightSum;
	}

	const float historyBlendWeight = saturate(1.0f - cPass.HistoryBlendWeight);
	sum = lerp(prevDepth, sum, historyBlendWeight);

	uDepthMap[texelLocation] = sum;

	GroupMemoryBarrierWithGroupSync();

	// Extend the borders of the probes to fix sampling issues at the edges
	for (uint index = groupIndex; index < NUM_DEPTH_BORDER_TEXELS; index += DDGI_PROBE_DEPTH_TEXELS * DDGI_PROBE_DEPTH_TEXELS)
	{
		uint2 sourceIndex = cornerTexelLocation + DDGI_DEPTH_BORDER_OFFSETS[index].xy;
		uint2 targetIndex = cornerTexelLocation + DDGI_DEPTH_BORDER_OFFSETS[index].zw;
		uDepthMap[targetIndex] = uDepthMap[sourceIndex];
	}
}

/**
	- Update Probe States -
*/

[numthreads(32, 1, 1)]
void UpdateProbeStatesCS(uint threadID : SV_DispatchThreadID)
{
	DDGIVolume volume = GetDDGIVolume(cPass.VolumeIndex);
	uint probeIdx = threadID.x;
	uint numProbes = volume.ProbeVolumeDimensions.x * volume.ProbeVolumeDimensions.y * volume.ProbeVolumeDimensions.z;
	if(probeIdx >= numProbes)
		return;

	float3 prevOffset = uProbeOffsets[probeIdx].xyz;

	// Use the stable rays to determine if the probe should be (re)activated.
	// The rays are temporally stable so that they don't make the probe states flicker.
	uint numStableRays = min(volume.NumRaysPerProbe, DDGI_NUM_STABLE_RAYS);

	uint maxNumBackfaces = numStableRays * 0.25f;
	uint numBackfaces = 0;

	// Find 3 kinds of rays: The nearest and farthest frontface hit, and the nearest backface hit.
	int nearestBackfaceIndex = -1;
	float nearestBackfaceHitDistance = FLT_MAX;
	int nearestFrontfaceIndex = -1;
	float nearestFrontfaceHitDistance = FLT_MAX;
	int farthestFrontFaceIndex = -1;
	float farthestFrontfaceHitDistance = 0;

	for(uint rayIndex = 0; rayIndex < numStableRays; ++rayIndex)
	{
		float depth = tRayHitInfo[probeIdx * volume.MaxRaysPerProbe + rayIndex].a;
		if(depth < 0)
		{
			numBackfaces++;
			depth /= DDGI_BACKFACE_DEPTH_MULTIPLIER;
			if(depth < nearestBackfaceHitDistance)
			{
				nearestBackfaceHitDistance = depth;
				nearestBackfaceIndex = rayIndex;
			}
		}
		else
		{
			if(depth < nearestFrontfaceHitDistance)
			{
				nearestFrontfaceHitDistance = depth;
				nearestFrontfaceIndex = rayIndex;
			}
			else if(depth > farthestFrontfaceHitDistance)
			{
				farthestFrontfaceHitDistance = depth;
				farthestFrontFaceIndex = rayIndex;
			}
		}
	}

	float3 newOffset = FLT_MAX;
	const float minFrontFaceDistanceThreshold = 0.2f;
	const float minNumBackfacesThreshold = 0.25f;
	const float offsetScale = 0.5f;

	// The idea is to move through a backface surface where possible.
	// Otherwise move away from close front faces and always attempt to return to the base position

	if (nearestBackfaceIndex != -1 && (float)numBackfaces / numStableRays > minNumBackfacesThreshold)
    {
        float3 nearestBackfaceDirection = nearestBackfaceHitDistance * DDGIGetRayDirection(nearestBackfaceIndex, numStableRays);
        newOffset = prevOffset + nearestBackfaceDirection * (offsetScale + 1.f);
    }
    else if (nearestFrontfaceHitDistance < minFrontFaceDistanceThreshold)
    {
        float3 nearestFrontfaceDirection = DDGIGetRayDirection(nearestFrontfaceIndex, numStableRays);
        float3 farthestFrontfaceDirection = DDGIGetRayDirection(farthestFrontFaceIndex, numStableRays);

        // We don't want to offset closer to the farthest front face if that also means coming closer to the nearest front face.
        if (dot(nearestFrontfaceDirection, farthestFrontfaceDirection) <= 0.f)
        {
            newOffset = prevOffset + farthestFrontfaceDirection * min(farthestFrontfaceHitDistance, 1.f) * offsetScale;
        }
    }
    else if (nearestFrontfaceHitDistance > minFrontFaceDistanceThreshold + offsetScale)
    {
        float pushBackAmount = min(nearestFrontfaceHitDistance - minFrontFaceDistanceThreshold, length(prevOffset));
        newOffset = prevOffset - pushBackAmount * normalize(prevOffset);
    }

	// Apply offset when valid
    float3 offsetNormalized = newOffset / volume.ProbeSize;
    if (dot(offsetNormalized, offsetNormalized) < Square(0.5f))
    {
        prevOffset = newOffset;
    }

	uProbeOffsets[probeIdx] = float4(prevOffset, 0);

	// Add some extra margin to max depth for when probes move around
	const float3 maxProbeDepth = volume.ProbeSize * 3.0f;
	bool isActive = numBackfaces < maxNumBackfaces && any(nearestFrontfaceHitDistance <= maxProbeDepth);
	uProbeStates[probeIdx] = isActive ? 0 : 1;
}

/**
	- VisualizeIrradiance -
	Visualization Shader rendering spheres in the scene.
*/

struct VisualizeParameters
{
	uint VolumeIndex;
};

ConstantBuffer<VisualizeParameters> cVisualize : register(b0);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float3 Normal : NORMAL;
	uint ProbeIndex : PROBEINDEX;
};

InterpolantsVSToPS VisualizeIrradianceVS(
	uint vertexId : SV_VertexID,
	uint instanceId : SV_InstanceID)
{
	DDGIVolume volume = GetDDGIVolume(cVisualize.VolumeIndex);
	const float scale = 0.1f;

	uint probeIdx = instanceId;
	uint3 probeIdx3D = GetDDGIProbeIndex3D(volume, probeIdx);
	float3 probePosition = GetDDGIProbePosition(volume, probeIdx3D);
	float3 pos = SPHERE[vertexId].xyz;
	float3 worldPos = scale * pos + probePosition;

	InterpolantsVSToPS output;
	output.Position = mul(float4(worldPos, 1), cView.ViewProjection);
	output.Normal = pos;
	output.ProbeIndex = probeIdx;
	return output;
}

#define VISUALIZE_MODE_IRRADIANCE 0
#define VISUALIZE_MODE_DEPTH 1
#define VISUALIZE_MODE_UNIQUE_COLOR 2

#define VISUALIZE_MODE VISUALIZE_MODE_IRRADIANCE

float4 VisualizeIrradiancePS(InterpolantsVSToPS input) : SV_Target0
{
	DDGIVolume volume = GetDDGIVolume(cVisualize.VolumeIndex);
	uint3 probeIdx3D = GetDDGIProbeIndex3D(volume, input.ProbeIndex);
	float3 probePosition = GetDDGIProbePosition(volume, probeIdx3D);
#if VISUALIZE_MODE == VISUALIZE_MODE_IRRADIANCE
	float2 uv = GetDDGIProbeUV(volume, probeIdx3D, input.Normal, DDGI_PROBE_IRRADIANCE_TEXELS);
	Texture2D<float4> tIrradianceMap = ResourceDescriptorHeap[volume.IrradianceIndex];
	float3 radiance = tIrradianceMap.SampleLevel(sLinearClamp, uv, 0).rgb;
	radiance = pow(radiance, DDGI_PROBE_GAMMA * 0.5);
	float3 color = radiance / PI;
#elif VISUALIZE_MODE == VISUALIZE_MODE_DEPTH
	float2 uv = GetDDGIProbeUV(volume, probeIdx3D, input.Normal, DDGI_PROBE_DEPTH_TEXELS);
	Texture2D<float> tDepthMap = ResourceDescriptorHeap[volume.DepthIndex];
	float depth = tDepthMap.SampleLevel(sLinearClamp, uv, 0);
	float3 color = depth.xxx / (MaxComponent(volume.ProbeSize) * 3);
#elif VISUALIZE_MODE == VISUALIZE_MODE_UNIQUE_COLOR
	uint seed = SeedThread(input.ProbeIndex);
	float3 color = float3(Random01(seed), Random01(seed), Random01(seed));
#endif

	float3 probeDirection = normalize(cView.ViewLocation - probePosition);
	if(dot(probeDirection, input.Normal) < 0.4f && !DDGIIsProbeActive(volume, probeIdx3D))
	{
		color = float3(1, 0, 1);
	}

	return float4(color, 1);
}
