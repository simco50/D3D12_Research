#include "Common.hlsli"
#include "RayTracingCommon.hlsli"
#include "Lighting.hlsli"
#include "Primitives.hlsli"
#include "DDGICommon.hlsli"
#include "Random.hlsli"
#include "TonemappingCommon.hlsli"

struct RayHitInfo
{
	float3 Direction;
	float Depth;
	float3 Radiance;
	float padd;
};

struct PassParameters
{
	float3 RandomVector;
	float RandomAngle;
	float HistoryBlendWeight;
	uint VolumeIndex;
};

ConstantBuffer<PassParameters> cPass : register(b0);

RWStructuredBuffer<RayHitInfo> uRayHitInfo : register(u0);
RWTexture2D<float4> uIrradianceMap : register(u0);
RWTexture2D<float2> uDepthMap : register(u0);
RWTexture2D<float4> uVisualizeTexture : register(u0);
RWStructuredBuffer<float4> uProbeOffsets : register(u1);

StructuredBuffer<RayHitInfo> tRayHitInfo : register(t0);

float CastShadowRay(float3 origin, float3 direction)
{
	float len = length(direction);
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction / len;
	ray.TMin = RAY_BIAS;
	ray.TMax = len;

	const int rayFlags =
		RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |
		RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
		RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

	RaytracingAccelerationStructure TLAS = ResourceDescriptorHeap[cView.TLASIndex];

	RayQuery<rayFlags> q;

	q.TraceRayInline(
		TLAS, 	// AccelerationStructure
		0,		// RayFlags
		0xFF, 	// InstanceMask
		ray		// Ray
	);

	while(q.Proceed())
	{
		switch(q.CandidateType())
		{
			case CANDIDATE_NON_OPAQUE_TRIANGLE:
			{
				MeshInstance instance = GetMeshInstance(q.CandidateInstanceID());
				VertexAttribute vertex = GetVertexAttributes(instance, q.CandidateTriangleBarycentrics(), q.CandidatePrimitiveIndex(), q.CandidateObjectToWorld4x3());
				MaterialData material = GetMaterial(instance.Material);
				MaterialProperties surface = GetMaterialProperties(material, vertex.UV, 0);
				if(surface.Opacity > material.AlphaCutoff)
				{
					q.CommitNonOpaqueTriangleHit();
				}
			}
			break;
		}
	}
	return q.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}

// From G3DMath
// Generates a nearly uniform point distribution on the unit sphere of size N
static const float PHI = sqrt(5) * 0.5 + 0.5;
float3 SphericalFibonacci(float i, float n)
{
	#define madfrac(A, B) ((A) * (B)-floor((A) * (B)))
	float phi = 2.0 * PI * madfrac(i, PHI - 1);
	float cos_theta = 1.0 - (2.0 * i + 1.0) * (1.0 / n);
	float sin_theta = sqrt(saturate(1.0 - cos_theta * cos_theta));
	return float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
	#undef madfrac
}

/**
	- TraceRays -
	Cast N uniformly distributed rays from each probe.
*/
#define RAY_TRACE_GROUP_SIZE 32

[numthreads(RAY_TRACE_GROUP_SIZE, 1, 1)]
void TraceRaysCS(
	uint threadId : SV_DispatchThreadID,
	uint groupIndex : SV_GroupID,
	uint groupThreadId : SV_GroupThreadID)
{
	DDGIVolume volume = GetDDGIVolume(cPass.VolumeIndex);

	uint probeIdx = groupIndex;
	uint3 probeIdx3D = GetDDGIProbeIndex3D(volume, probeIdx);
	float3 probePosition = GetDDGIProbePosition(volume, probeIdx3D);
	uint rayIndex = groupThreadId;

	float3x3 randomRotation = AngleAxis3x3(cPass.RandomAngle, cPass.RandomVector);
	const float maxDepth = max(volume.ProbeSize.x, max(volume.ProbeSize.y, volume.ProbeSize.z)) * 2;

	RaytracingAccelerationStructure TLAS = ResourceDescriptorHeap[cView.TLASIndex];

	uint numRays = volume.NumRaysPerProbe;
	while(rayIndex < numRays)
	{
		float3 direction = mul(SphericalFibonacci(rayIndex, numRays), randomRotation);

		RayDesc ray;
		ray.Origin = probePosition;
		ray.Direction = direction;
		ray.TMin = 0;
		ray.TMax = RAY_MAX_T;

		RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
		q.TraceRayInline(
			TLAS, 	// AccelerationStructure
			0,		// RayFlags
			0xFF, 	// InstanceMask
			ray		// Ray
		);
		while(q.Proceed())
		{
			switch(q.CandidateType())
			{
				case CANDIDATE_NON_OPAQUE_TRIANGLE:
				{
					MeshInstance instance = GetMeshInstance(q.CandidateInstanceID());
					VertexAttribute vertex = GetVertexAttributes(instance, q.CandidateTriangleBarycentrics(), q.CandidatePrimitiveIndex(), q.CandidateObjectToWorld4x3());
					MaterialData material = GetMaterial(instance.Material);
					MaterialProperties surface = GetMaterialProperties(material, vertex.UV, 0);
					if(surface.Opacity > material.AlphaCutoff)
					{
						q.CommitNonOpaqueTriangleHit();
					}
				}
				break;
			}
		}

		float3 radiance = 0;
		float depth = maxDepth;

		if(q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			MeshInstance instance = GetMeshInstance(q.CommittedInstanceID());
			VertexAttribute vertex = GetVertexAttributes(instance, q.CommittedTriangleBarycentrics(), q.CommittedPrimitiveIndex(), q.CommittedObjectToWorld4x3());
			MaterialData material = GetMaterial(instance.Material);
			const uint textureMipLevel = 5;
			MaterialProperties surface = GetMaterialProperties(material, vertex.UV, textureMipLevel);
			BrdfData brdfData = GetBrdfData(surface);

			float3 hitLocation = q.WorldRayOrigin() + q.WorldRayDirection() * q.CommittedRayT();
			float3 N = vertex.Normal;

			for(uint lightIndex = 0; lightIndex < cView.LightCount; ++lightIndex)
			{
				Light light = GetLight(lightIndex);
				float attenuation = GetAttenuation(light, hitLocation);
				if(attenuation <= 0.0f)
					continue;

				float3 L = light.Position - hitLocation;
				if(light.IsDirectional)
				{
					L = RAY_MAX_T * -light.Direction;
				}
				attenuation *= CastShadowRay(hitLocation, L);
				if(attenuation <= 0.0f)
					continue;

				float3 diffuse = (attenuation * saturate(dot(N, L))) * Diffuse_Lambert(brdfData.Diffuse);
				radiance += diffuse * light.GetColor().rgb * light.Intensity;
			}

			radiance += surface.Emissive;
			radiance += Diffuse_Lambert(min(brdfData.Diffuse, 0.9f)) * SampleDDGIIrradiance(hitLocation, N, ray.Direction);
			depth = min(q.CommittedRayT(), depth);

			// If backfacing, make negative so probes get pushed through the backface when offset.
			if(!q.CommittedTriangleFrontFace())
			{
				depth *= -0.2f;
			}
		}
		else
		{
			radiance = GetSky(ray.Direction);
		}

		RayHitInfo hit = (RayHitInfo)0;
		hit.Direction = ray.Direction;
		hit.Depth = depth;
		hit.Radiance = radiance;
		uRayHitInfo[probeIdx * volume.MaxRaysPerProbe + rayIndex] = hit;
		rayIndex += RAY_TRACE_GROUP_SIZE;
	}
}

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
groupshared RayHitInfo gsIrradianceRayHitCache[IRRADIANCE_RAY_HIT_GS_SIZE];

[numthreads(DDGI_PROBE_IRRADIANCE_TEXELS, DDGI_PROBE_IRRADIANCE_TEXELS, 1)]
void UpdateIrradianceCS(
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupId : SV_GroupID,
	uint groupIndex : SV_GroupIndex)
{
	DDGIVolume volume = GetDDGIVolume(cPass.VolumeIndex);
	uint probeIdx = groupId;
	uint3 probeCoordinates = GetDDGIProbeIndex3D(volume, probeIdx);
	uint2 texelLocation = GetDDGIProbeTexel(volume, probeCoordinates, DDGI_PROBE_IRRADIANCE_TEXELS);
	uint2 cornerTexelLocation = texelLocation - 1u;
	texelLocation += groupThreadId.xy;
	Texture2D<float4> tIrradianceMap = ResourceDescriptorHeap[volume.IrradianceIndex];
	float3 prevRadiance = tIrradianceMap[texelLocation].rgb;
	float3 probeDirection = DecodeNormalOctahedron(((groupThreadId.xy + 0.5f) / (float)DDGI_PROBE_IRRADIANCE_TEXELS) * 2 - 1);

	float weightSum = 0;
	float3 sum = 0;

	uint remainingRays = volume.NumRaysPerProbe;
	while(remainingRays > 0)
	{
		uint rayCount = min(remainingRays, IRRADIANCE_RAY_HIT_GS_SIZE);
		remainingRays -= rayCount;

		if(groupIndex < rayCount)
		{
			gsIrradianceRayHitCache[groupIndex] = tRayHitInfo[probeIdx * volume.MaxRaysPerProbe + remainingRays + groupIndex];
		}
		GroupMemoryBarrierWithGroupSync();

		for(uint i = 0; i < rayCount; ++i)
		{
			RayHitInfo rayData = gsIrradianceRayHitCache[i];
			float weight = saturate(dot(probeDirection, rayData.Direction));
			sum += weight * rayData.Radiance;
			weightSum += weight;
		}
	}

    const float epsilon = 1e-9f * float(volume.NumRaysPerProbe);
	sum *= 1.0f / max(2.0f * weightSum, epsilon);

	// Apply tone curve for better encoding
	sum = pow(sum, rcp(PROBE_GAMMA));
	const float historyBlendWeight = saturate(1.0f - cPass.HistoryBlendWeight);
	sum = lerp(prevRadiance, sum, historyBlendWeight);

	uIrradianceMap[texelLocation] = float4(sum, 1);

	DeviceMemoryBarrierWithGroupSync();

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
groupshared RayHitInfo gsDepthRayHitCache[DEPTH_RAY_HIT_GS_SIZE];

[numthreads(DDGI_PROBE_DEPTH_TEXELS, DDGI_PROBE_DEPTH_TEXELS, 1)]
void UpdateDepthCS(
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupId : SV_GroupID,
	uint groupIndex : SV_GroupIndex)
{
	DDGIVolume volume = GetDDGIVolume(cPass.VolumeIndex);
	uint probeIdx = groupId;
	uint3 probeCoordinates = GetDDGIProbeIndex3D(volume, probeIdx);
	uint2 texelLocation = GetDDGIProbeTexel(volume, probeCoordinates, DDGI_PROBE_DEPTH_TEXELS);
	uint2 cornerTexelLocation = texelLocation - 1u;
	texelLocation += groupThreadId.xy;
	Texture2D<float2> tDepthMap = ResourceDescriptorHeap[volume.DepthIndex];
	float2 prevDepth = tDepthMap[texelLocation];
	float3 probeDirection = DecodeNormalOctahedron(((groupThreadId.xy + 0.5f) / (float)DDGI_PROBE_DEPTH_TEXELS) * 2 - 1);

#if DDGI_DYNAMIC_PROBE_OFFSET
	float3 prevProbeOffset = uProbeOffsets[probeIdx].xyz;
	float3 probeOffset = 0;
	const float probeOffsetDistance = max(volume.ProbeSize.x, max(volume.ProbeSize.y, volume.ProbeSize.z)) * 0.5f;
#endif

	float weightSum = 0;
	float2 sum = 0;

	uint remainingRays = volume.NumRaysPerProbe;
	while(remainingRays > 0)
	{
		uint rayCount = min(remainingRays, DEPTH_RAY_HIT_GS_SIZE);
		remainingRays -= rayCount;

		if(groupIndex < rayCount)
		{
			gsDepthRayHitCache[groupIndex] = tRayHitInfo[probeIdx * volume.MaxRaysPerProbe + remainingRays + groupIndex];
		}
		GroupMemoryBarrierWithGroupSync();

		for(uint i = 0; i < rayCount; ++i)
		{
			RayHitInfo rayData = gsDepthRayHitCache[i];
			float weight = saturate(dot(probeDirection, rayData.Direction));
			weight = pow(weight, 64);
			float depth = rayData.Depth;
			if(weight > MIN_WEIGHT_THRESHOLD)
			{
				weightSum += weight;
				sum += float2(abs(depth), Square(depth)) * weight;
			}

	#if DDGI_DYNAMIC_PROBE_OFFSET
			probeOffset -= rayData.Direction * saturate(probeOffsetDistance - depth);
	#endif
		}
	}

	if(weightSum > MIN_WEIGHT_THRESHOLD)
	{
		sum /= weightSum;
	}

	const float historyBlendWeight = saturate(1.0f - cPass.HistoryBlendWeight);
	sum = lerp(prevDepth, sum, historyBlendWeight);

	uDepthMap[texelLocation] = sum;

	DeviceMemoryBarrierWithGroupSync();

#if DDGI_DYNAMIC_PROBE_OFFSET
	if(groupIndex == 0)
	{
		const float maxOffset = probeOffsetDistance * 0.2f;
		probeOffset = clamp(probeOffset, -maxOffset, maxOffset);
		uProbeOffsets[probeIdx] = float4(lerp(prevProbeOffset, probeOffset, 0.01f), 0);
	}
#endif

	// Extend the borders of the probes to fix sampling issues at the edges
	for (uint index = groupIndex; index < NUM_DEPTH_BORDER_TEXELS; index += DDGI_PROBE_DEPTH_TEXELS * DDGI_PROBE_DEPTH_TEXELS)
	{
		uint2 sourceIndex = cornerTexelLocation + DDGI_DEPTH_BORDER_OFFSETS[index].xy;
		uint2 targetIndex = cornerTexelLocation + DDGI_DEPTH_BORDER_OFFSETS[index].zw;
		uDepthMap[targetIndex] = uDepthMap[sourceIndex];
	}
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

float4 VisualizeIrradiancePS(InterpolantsVSToPS input) : SV_Target0
{
	DDGIVolume volume = GetDDGIVolume(cVisualize.VolumeIndex);
	uint3 probeIdx3D = GetDDGIProbeIndex3D(volume, input.ProbeIndex);
	float3 probePosition = GetDDGIProbePosition(volume, probeIdx3D);
	float2 uv = GetDDGIProbeUV(volume, probeIdx3D, input.Normal, DDGI_PROBE_IRRADIANCE_TEXELS);
	Texture2D<float4> tIrradianceMap = ResourceDescriptorHeap[volume.IrradianceIndex];
	float3 radiance = tIrradianceMap.SampleLevel(sLinearClamp, uv, 0).rgb;
	radiance = pow(radiance, PROBE_GAMMA * 0.5);
	radiance = 2 * Square(radiance);
	return float4(radiance, 1);
}
