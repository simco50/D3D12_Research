#include "Common.hlsli"
#include "RayTracingCommon.hlsli"
#include "Lighting.hlsli"
#include "Primitives.hlsli"
#include "DDGICommon.hlsli"

struct RayHitInfo
{
	float3 Direction;
	float Distance;
	float3 Radiance;
	float padd;
};

struct PassParameters
{
	float4x4 RandomTransform;
	uint RaysPerProbe;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWStructuredBuffer<RayHitInfo> uRayHitInfo : register(u0);
RWTexture2D<float4> uIrradianceMap : register(u0);
RWTexture2D<float4> uVisualizeTexture : register(u0);

StructuredBuffer<RayHitInfo> tRayHitInfo : register(t0);
Texture2D<float4> tIrradianceMap : register(t1);

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

LightResult EvaluateLight(Light light, float3 worldPos, float3 V, float3 N, BrdfData brdfData)
{
	LightResult result = (LightResult)0;
	float attenuation = GetAttenuation(light, worldPos);
	if(attenuation <= 0.0f)
	{
		return result;
	}

	float3 L = light.Position - worldPos;
	if(light.IsDirectional)
	{
		L = RAY_MAX_T * -light.Direction;
	}

	attenuation *= CastShadowRay(worldPos, L);
	if(attenuation <= 0.0f)
	{
		return result;
	}

	result = DefaultLitBxDF(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, N, V, normalize(L), attenuation);
	float4 color = light.GetColor();
	result.Diffuse *= color.rgb * light.Intensity;
	result.Specular *= color.rgb * light.Intensity;
	return result;
}

RayHitInfo CastPrimaryRay(float3 origin, float3 direction)
{
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction;
	ray.TMin = 0;
	ray.TMax = RAY_MAX_T;

	const int rayFlags = RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

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

	float3 radiance = 0;
	float distance = 0;

	if(q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
	{
		MeshInstance instance = GetMeshInstance(q.CommittedInstanceID());
		VertexAttribute vertex = GetVertexAttributes(instance, q.CommittedTriangleBarycentrics(), q.CommittedPrimitiveIndex(), q.CommittedObjectToWorld4x3());
		MaterialData material = GetMaterial(instance.Material);
		MaterialProperties surface = GetMaterialProperties(material, vertex.UV, 2);
		BrdfData brdfData = GetBrdfData(surface);

		float3 hitLocation = q.WorldRayOrigin() + q.WorldRayDirection() * q.CommittedRayT();
		float3 V = normalize(-q.WorldRayDirection());
		float3 N = vertex.Normal;

		LightResult totalResult = (LightResult)0;
		for(int i = 0; i < cView.LightCount; ++i)
		{
			Light light = GetLight(i);
			LightResult result = EvaluateLight(light, hitLocation, V, N, brdfData);
			totalResult.Diffuse += result.Diffuse;
			totalResult.Specular += result.Specular;
		}

		radiance = totalResult.Diffuse;

		// #todo: Multi bounce - Borked
		//radiance += SampleIrradiance(hitLocation, N, tIrradianceMap);

		distance = q.CommittedRayT();
	}
	else
	{
		radiance = GetSky(ray.Direction);
		distance = -1;
	}

	RayHitInfo hit = (RayHitInfo)0;
	hit.Direction = ray.Direction;
	hit.Distance = distance;
	hit.Radiance = radiance;
	return hit;
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
	return float3(cos(phi) * sin_theta, cos_theta, sin(phi) * sin_theta);
	#undef madfrac
}

/**
	- TraceRays -
	Cast N uniformly distributed rays from each probe.
*/
#define THREAD_GROUP_SIZE 32

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void TraceRaysCS(
	uint threadId : SV_DispatchThreadID,
	uint groupIndex : SV_GroupID,
	uint groupThreadId : SV_GroupThreadID)
{
	uint probeIdx = groupIndex;
	uint3 probeIdx3D = GetProbeIndex3D(probeIdx);
	float3 probePosition = GetProbePosition(probeIdx3D);
	uint rayIndex = groupThreadId;
	while(rayIndex < cPass.RaysPerProbe)
	{
		float3 direction = mul(SphericalFibonacci(rayIndex, cPass.RaysPerProbe), (float3x3)cPass.RandomTransform);
		RayHitInfo hit = CastPrimaryRay(probePosition, direction);
		uRayHitInfo[probeIdx * MAX_RAYS_PER_PROBE + rayIndex] = hit;
		rayIndex += THREAD_GROUP_SIZE;
	}
}

/**
	- UpdateIrradiance -
	Store Irradiance data in texture atlas
*/

static const uint4 DDGI_COLOR_BORDER_OFFSETS[36] = {
	uint4(8, 1, 1, 0),
	uint4(7, 1, 2, 0),
	uint4(6, 1, 3, 0),
	uint4(5, 1, 4, 0),
	uint4(4, 1, 5, 0),
	uint4(3, 1, 6, 0),
	uint4(2, 1, 7, 0),
	uint4(1, 1, 8, 0),
	uint4(8, 8, 1, 9),
	uint4(7, 8, 2, 9),
	uint4(6, 8, 3, 9),
	uint4(5, 8, 4, 9),
	uint4(4, 8, 5, 9),
	uint4(3, 8, 6, 9),
	uint4(2, 8, 7, 9),
	uint4(1, 8, 8, 9),
	uint4(1, 8, 0, 1),
	uint4(1, 7, 0, 2),
	uint4(1, 6, 0, 3),
	uint4(1, 5, 0, 4),
	uint4(1, 4, 0, 5),
	uint4(1, 3, 0, 6),
	uint4(1, 2, 0, 7),
	uint4(1, 1, 0, 8),
	uint4(8, 8, 9, 1),
	uint4(8, 7, 9, 2),
	uint4(8, 6, 9, 3),
	uint4(8, 5, 9, 4),
	uint4(8, 4, 9, 5),
	uint4(8, 3, 9, 6),
	uint4(8, 2, 9, 7),
	uint4(8, 1, 9, 8),
	uint4(8, 8, 0, 0),
	uint4(1, 8, 9, 0),
	uint4(8, 1, 0, 9),
	uint4(1, 1, 9, 9)
};

[numthreads(PROBE_TEXEL_RESOLUTION, PROBE_TEXEL_RESOLUTION, 1)]
void UpdateIrradianceCS(
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupId : SV_GroupID,
	uint groupIndex : SV_GroupIndex)
{
	uint probeIdx = groupId;
	uint3 probeCoordinates = GetProbeIndex3D(probeIdx);
	uint2 texelLocation = GetProbeTexel(probeCoordinates);
	uint2 cornerTexelLocation = texelLocation - 1u;
	texelLocation += groupThreadId.xy;
	float3 prevRadiance = tIrradianceMap[texelLocation].rgb;

	float3 probeDirection = DecodeNormalOctahedron(((groupThreadId.xy + 0.5f) / (float)PROBE_TEXEL_RESOLUTION) * 2 - 1);

	float weightSum = 0;
	float3 radianceSum = 0;

	for(uint i = 0; i < cPass.RaysPerProbe; ++i)
	{
		RayHitInfo rayData = tRayHitInfo[probeIdx * MAX_RAYS_PER_PROBE + i];
		float weight = saturate(dot(probeDirection, rayData.Direction));
		if(weight > 0.0001)
		{
			radianceSum += weight * rayData.Radiance;
			weightSum += weight;
		}
	}

	float3 radiance = radianceSum;

	if(weightSum > 0.0001)
	{
		radiance /= weightSum;
	}

	if(cView.FrameIndex > 2)
	{
		const float historyBlendWeight = 0.02;
		radiance = lerp(prevRadiance, radiance, historyBlendWeight);
	}
	uIrradianceMap[texelLocation] = float4(radiance, 1);

	DeviceMemoryBarrierWithGroupSync();

	// Extend the borders of the probes to fix sampling issues at the edges
	for (uint index = groupIndex; index < 36; index += PROBE_TEXEL_RESOLUTION * PROBE_TEXEL_RESOLUTION)
	{
		uint2 sourceIndex = cornerTexelLocation + DDGI_COLOR_BORDER_OFFSETS[index].xy;
		uint2 targetIndex = cornerTexelLocation + DDGI_COLOR_BORDER_OFFSETS[index].zw;
		uIrradianceMap[targetIndex] = uIrradianceMap[sourceIndex];
	}
}

/**
	- VisualizeIrradiance -
	Visualization Shader rendering spheres in the scene.
*/

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
	const float scale = 0.1f;

	uint probeIdx = instanceId;
	uint3 probeIdx3D = GetProbeIndex3D(probeIdx);
	float3 probePosition = GetProbePosition(probeIdx3D);
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
	uint3 probeIdx3D = GetProbeIndex3D(input.ProbeIndex);
	float3 probePosition = GetProbePosition(probeIdx3D);
	float2 uv = GetProbeUV(probeIdx3D, input.Normal);
	float3 radiance = tIrradianceMap.SampleLevel(sLinearClamp, uv, 0).rgb;
	return float4(radiance, 1);
}
