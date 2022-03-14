#include "Common.hlsli"
#include "RayTracingCommon.hlsli"
#include "Lighting.hlsli"
#include "Primitives.hlsli"
#include "DDGICommon.hlsli"
#include "Random.hlsli"

struct RayHitInfo
{
	float3 Direction;
	float Depth;
	float3 Radiance;
	float padd;
};

struct PassParameters
{
	uint RaysPerProbe;
	uint MaxRaysPerProbe;
	float HistoryBlendWeight;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWStructuredBuffer<RayHitInfo> uRayHitInfo : register(u0);
RWTexture2D<float4> uIrradianceMap : register(u0);
RWTexture2D<float2> uDepthMap : register(u0);
RWTexture2D<float4> uVisualizeTexture : register(u0);
RWStructuredBuffer<float4> uProbeOffsets : register(u1);

StructuredBuffer<RayHitInfo> tRayHitInfo : register(t0);
Texture2D<float4> tIrradianceMap : register(t1);
Texture2D<float2> tDepthMap : register(t2);
StructuredBuffer<float4> tProbeOffsets : register(t3);

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
	const float maxDepth = max(cView.DDGIProbeSize.x, max(cView.DDGIProbeSize.y, cView.DDGIProbeSize.z)) * 2;
	float depth = maxDepth;

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

		radiance += totalResult.Diffuse;
		radiance += surface.Emissive;
		radiance += brdfData.Diffuse * SampleIrradiance(hitLocation, N, tIrradianceMap, tDepthMap);

		depth = clamp(q.CommittedRayT(), 0, maxDepth);
	}
	else
	{
		radiance = GetSky(ray.Direction);
	}

	RayHitInfo hit = (RayHitInfo)0;
	hit.Direction = ray.Direction;
	hit.Depth = depth;
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

// From keijiro: 3x3 Rotation matrix with an angle and an arbitrary vector
float3x3 AngleAxis3x3(float angle, float3 axis)
{
    float c, s;
    sincos(angle, s, c);

    float t = 1 - c;
    float x = axis.x;
    float y = axis.y;
    float z = axis.z;

    return float3x3(
        t * x * x + c,      t * x * y - s * z,  t * x * z + s * y,
        t * x * y + s * z,  t * y * y + c,      t * y * z - s * x,
        t * x * z - s * y,  t * y * z + s * x,  t * z * z + c
    );
}

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

	uint seed = SeedThread(cView.FrameIndex);
	float3x3 randomRotation = AngleAxis3x3(Random01(seed) * 2 * PI, normalize(float3(Random01(seed), Random01(seed), Random01(seed))));

	while(rayIndex < cPass.RaysPerProbe)
	{
		float3 direction = mul(SphericalFibonacci(rayIndex, cPass.RaysPerProbe), randomRotation);
		RayHitInfo hit = CastPrimaryRay(probePosition, direction);
		uRayHitInfo[probeIdx * cPass.MaxRaysPerProbe + rayIndex] = hit;
		rayIndex += THREAD_GROUP_SIZE;
	}
}

/**
	- UpdateIrradiance -
	Store Irradiance data in texture atlas
*/

#define MIN_WEIGHT_THRESHOLD 0.0001f

// From Dihara Wijetunga - hybrid-rendering
static const uint4 DDGI_COLOR_BORDER_OFFSETS[36] = {
	uint4(8, 1, 1, 0), uint4(7, 1, 2, 0), uint4(6, 1, 3, 0), uint4(5, 1, 4, 0), uint4(4, 1, 5, 0), uint4(3, 1, 6, 0),
	uint4(2, 1, 7, 0), uint4(1, 1, 8, 0), uint4(8, 8, 1, 9), uint4(7, 8, 2, 9), uint4(6, 8, 3, 9), uint4(5, 8, 4, 9),
	uint4(4, 8, 5, 9), uint4(3, 8, 6, 9), uint4(2, 8, 7, 9), uint4(1, 8, 8, 9), uint4(1, 8, 0, 1), uint4(1, 7, 0, 2),
	uint4(1, 6, 0, 3), uint4(1, 5, 0, 4), uint4(1, 4, 0, 5), uint4(1, 3, 0, 6), uint4(1, 2, 0, 7), uint4(1, 1, 0, 8),
	uint4(8, 8, 9, 1), uint4(8, 7, 9, 2), uint4(8, 6, 9, 3), uint4(8, 5, 9, 4), uint4(8, 4, 9, 5), uint4(8, 3, 9, 6),
	uint4(8, 2, 9, 7), uint4(8, 1, 9, 8), uint4(8, 8, 0, 0), uint4(1, 8, 9, 0), uint4(8, 1, 0, 9), uint4(1, 1, 9, 9)
};

[numthreads(DDGI_PROBE_IRRADIANCE_TEXELS, DDGI_PROBE_IRRADIANCE_TEXELS, 1)]
void UpdateIrradianceCS(
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupId : SV_GroupID,
	uint groupIndex : SV_GroupIndex)
{
	uint probeIdx = groupId;
	uint3 probeCoordinates = GetProbeIndex3D(probeIdx);
	uint2 texelLocation = GetProbeTexel(probeCoordinates, DDGI_PROBE_IRRADIANCE_TEXELS);
	uint2 cornerTexelLocation = texelLocation - 1u;
	texelLocation += groupThreadId.xy;
	float3 prevRadiance = tIrradianceMap[texelLocation].rgb;
	float3 probeDirection = DecodeNormalOctahedron(((groupThreadId.xy + 0.5f) / (float)DDGI_PROBE_IRRADIANCE_TEXELS) * 2 - 1);

	float weightSum = 0;
	float3 sum = 0;

	for(uint i = 0; i < cPass.RaysPerProbe; ++i)
	{
		RayHitInfo rayData = tRayHitInfo[probeIdx * cPass.MaxRaysPerProbe + i];
		float weight = saturate(dot(probeDirection, rayData.Direction));
		if(weight > MIN_WEIGHT_THRESHOLD)
		{
			sum += weight * rayData.Radiance;
			weightSum += weight;
		}
	}
	if(weightSum > MIN_WEIGHT_THRESHOLD)
	{
		sum /= weightSum;
	}

	const float historyBlendWeight = saturate(1.0f - cPass.HistoryBlendWeight);
	sum = lerp(prevRadiance, sum, historyBlendWeight);

	uIrradianceMap[texelLocation] = float4(sum, 1);

	DeviceMemoryBarrierWithGroupSync();

	// Extend the borders of the probes to fix sampling issues at the edges
	for (uint index = groupIndex; index < 36; index += DDGI_PROBE_IRRADIANCE_TEXELS * DDGI_PROBE_IRRADIANCE_TEXELS)
	{
		uint2 sourceIndex = cornerTexelLocation + DDGI_COLOR_BORDER_OFFSETS[index].xy;
		uint2 targetIndex = cornerTexelLocation + DDGI_COLOR_BORDER_OFFSETS[index].zw;
		uIrradianceMap[targetIndex] = uIrradianceMap[sourceIndex];
	}
}

// From Dihara Wijetunga - hybrid-rendering
static const uint4 DDGI_DEPTH_BORDER_OFFSETS[68] = {
	uint4(16, 1, 1, 0), 	uint4(15, 1, 2, 0), 	uint4(14, 1, 3, 0), 	uint4(13, 1, 4, 0), 	uint4(12, 1, 5, 0),
	uint4(11, 1, 6, 0), 	uint4(10, 1, 7, 0), 	uint4(9, 1, 8, 0), 		uint4(8, 1, 9, 0), 		uint4(7, 1, 10, 0),
	uint4(6, 1, 11, 0), 	uint4(5, 1, 12, 0), 	uint4(4, 1, 13, 0), 	uint4(3, 1, 14, 0), 	uint4(2, 1, 15, 0),
	uint4(1, 1, 16, 0), 	uint4(16, 16, 1, 17), 	uint4(15, 16, 2, 17), 	uint4(14, 16, 3, 17), 	uint4(13, 16, 4, 17),
	uint4(12, 16, 5, 17), 	uint4(11, 16, 6, 17), 	uint4(10, 16, 7, 17), 	uint4(9, 16, 8, 17), 	uint4(8, 16, 9, 17),
	uint4(7, 16, 10, 17), 	uint4(6, 16, 11, 17), 	uint4(5, 16, 12, 17), 	uint4(4, 16, 13, 17), 	uint4(3, 16, 14, 17),
	uint4(2, 16, 15, 17), 	uint4(1, 16, 16, 17), 	uint4(1, 16, 0, 1), 	uint4(1, 15, 0, 2), 	uint4(1, 14, 0, 3),
	uint4(1, 13, 0, 4), 	uint4(1, 12, 0, 5), 	uint4(1, 11, 0, 6), 	uint4(1, 10, 0, 7),		uint4(1, 9, 0, 8),
	uint4(1, 8, 0, 9), 		uint4(1, 7, 0, 10), 	uint4(1, 6, 0, 11), 	uint4(1, 5, 0, 12), 	uint4(1, 4, 0, 13),
	uint4(1, 3, 0, 14), 	uint4(1, 2, 0, 15), 	uint4(1, 1, 0, 16), 	uint4(16, 16, 17, 1), 	uint4(16, 15, 17, 2),
	uint4(16, 14, 17, 3),	uint4(16, 13, 17, 4), 	uint4(16, 12, 17, 5), 	uint4(16, 11, 17, 6), 	uint4(16, 10, 17, 7),
	uint4(16, 9, 17, 8), 	uint4(16, 8, 17, 9), 	uint4(16, 7, 17, 10), 	uint4(16, 6, 17, 11),	uint4(16, 5, 17, 12),
	uint4(16, 4, 17, 13), 	uint4(16, 3, 17, 14), 	uint4(16, 2, 17, 15), 	uint4(16, 1, 17, 16), 	uint4(16, 16, 0, 0),
	uint4(1, 16, 17, 0), 	uint4(16, 1, 0, 17), 	uint4(1, 1, 17, 17)
};

[numthreads(DDGI_PROBE_DEPTH_TEXELS, DDGI_PROBE_DEPTH_TEXELS, 1)]
void UpdateDepthCS(
	uint3 groupThreadId : SV_GroupThreadID,
	uint groupId : SV_GroupID,
	uint groupIndex : SV_GroupIndex)
{
	uint probeIdx = groupId;
	uint3 probeCoordinates = GetProbeIndex3D(probeIdx);
	uint2 texelLocation = GetProbeTexel(probeCoordinates, DDGI_PROBE_DEPTH_TEXELS);
	uint2 cornerTexelLocation = texelLocation - 1u;
	texelLocation += groupThreadId.xy;
	float2 prevDepth = tDepthMap[texelLocation];
	float3 probeDirection = DecodeNormalOctahedron(((groupThreadId.xy + 0.5f) / (float)DDGI_PROBE_DEPTH_TEXELS) * 2 - 1);

#if DDGI_DYNAMIC_PROBE_OFFSET
	float3 prevProbeOffset = uProbeOffsets[probeIdx].xyz;
	float3 probeOffset = 0;
	const float probeOffsetDistance = max(cView.DDGIProbeSize.x, max(cView.DDGIProbeSize.y, cView.DDGIProbeSize.z)) * 0.3f;
#endif


	float weightSum = 0;
	float2 sum = 0;
	for(uint i = 0; i < cPass.RaysPerProbe; ++i)
	{
		RayHitInfo rayData = tRayHitInfo[probeIdx * cPass.MaxRaysPerProbe + i];
		float weight = saturate(dot(probeDirection, rayData.Direction));
		weight = pow(weight, 64);
		float depth = rayData.Depth;
		if(weight > MIN_WEIGHT_THRESHOLD)
		{
			weightSum += weight;
			sum += float2(depth, Square(depth)) * weight;
		}

#if DDGI_DYNAMIC_PROBE_OFFSET
		probeOffset -= rayData.Direction * saturate(probeOffsetDistance - depth);
#endif
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
		uProbeOffsets[probeIdx] = float4(lerp(prevProbeOffset, probeOffset, 0.005f), 0);
	}
#endif

	// Extend the borders of the probes to fix sampling issues at the edges
	for (uint index = groupIndex; index < 68; index += DDGI_PROBE_DEPTH_TEXELS * DDGI_PROBE_DEPTH_TEXELS)
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
	float2 uv = GetProbeUV(probeIdx3D, input.Normal, DDGI_PROBE_IRRADIANCE_TEXELS);
	float3 radiance = tIrradianceMap.SampleLevel(sLinearClamp, uv, 0).rgb;
	return float4(radiance, 1);
}
