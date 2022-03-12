#include "Common.hlsli"
#include "RayTracingCommon.hlsli"
#include "Lighting.hlsli"
#include "Primitives.hlsli"

#define MAX_RAYS_PER_PROBE 32
#define THREAD_GROUP_SIZE 32

struct RayHitInfo
{
	float3 Direction;
	float Distance;
	float4 Radiance;
};

struct PassParameters
{
	uint RaysPerProbe;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWStructuredBuffer<RayHitInfo> uRayHitInfo : register(u0);
RWTexture2D<float4> uIrracianceMap : register(u0);

StructuredBuffer<RayHitInfo> tRayHitInfo : register(t0);
RWTexture2D<float4> uVisualizeTexture : register(u0);

float3 GetProbePosition(uint index)
{
	uint3 probeCoordinates = UnFlatten3D(index, cView.ProbeVolumeDimensions);
	float3 volumeExtents = cView.SceneBoundsMax - cView.SceneBoundsMin;
	float3 probeSpacing = volumeExtents / (cView.ProbeVolumeDimensions - 1);
	float3 probePosition = cView.SceneBoundsMin + probeCoordinates * probeSpacing;
	return probePosition;
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

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void TraceRaysCS(
	uint threadId : SV_DispatchThreadID,
	uint groupIndex : SV_GroupID,
	uint groupThreadId : SV_GroupThreadID)
{
	uint numRays = 32;

	uint probeIdx = groupIndex;
	float3 probePosition = GetProbePosition(probeIdx);

	uint rayIndex = groupThreadId;
	while(rayIndex < numRays)
	{
		float angle = (float)rayIndex / numRays * 2 * PI;
		float3 direction = normalize(float3(cos(angle), 0, -sin(angle)));

		RayDesc ray;
		ray.Origin = probePosition;
		ray.Direction = direction;
		ray.TMin = 0;
		ray.TMax = RAY_MAX_T;

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

		float4 radiance = 0;
		float distance = 0;

		if(q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			MeshInstance instance = GetMeshInstance(q.CandidateInstanceID());
			VertexAttribute vertex = GetVertexAttributes(instance, q.CandidateTriangleBarycentrics(), q.CandidatePrimitiveIndex(), q.CandidateObjectToWorld4x3());
			MaterialData material = GetMaterial(instance.Material);
			MaterialProperties surface = GetMaterialProperties(material, vertex.UV, 0);
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

			radiance = float4(totalResult.Diffuse, 1);
			distance = q.CommittedRayT();
		}
		else
		{
			radiance = float4(GetSky(ray.Direction), 1);
			distance = -1;
		}

		RayHitInfo hit = (RayHitInfo)0;
		hit.Direction = ray.Direction;
		hit.Distance = distance;
		hit.Radiance = radiance;
		uRayHitInfo[probeIdx * MAX_RAYS_PER_PROBE + rayIndex] = hit;

		rayIndex += THREAD_GROUP_SIZE;
	}
}

[numthreads(32, 1, 1)]
void UpdateIrradianceCS(uint threadId : SV_DispatchThreadID)
{

}

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float3 Normal : NORMAL;
};

InterpolantsVSToPS VisualizeIrradianceVS(
	uint vertexId : SV_VertexID,
	uint instanceId : SV_InstanceID)
{
	uint probeIdx = instanceId;
	float3 probePosition = GetProbePosition(probeIdx);

	float3 pos = SPHERE[vertexId].xyz;
	float3 worldPos = 0.1f * pos + probePosition;

	InterpolantsVSToPS output;
	output.Position = mul(float4(worldPos, 1), cView.ViewProjection);
	output.Normal = pos;
	return output;
}

float4 VisualizeIrradiancePS(InterpolantsVSToPS input) : SV_Target0
{
	return float4(input.Normal, 1);
}
