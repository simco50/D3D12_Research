#include "Common.hlsli"
#include "RayTracingCommon.hlsli"
#include "Lighting.hlsli"
#include "DDGICommon.hlsli"

struct PassParameters
{
	float3 RandomVector;
	float RandomAngle;
	float HistoryBlendWeight;
	uint VolumeIndex;
};

ConstantBuffer<PassParameters> cPass : register(b0);
RWBuffer<float4> uRayHitInfo : register(u0);

/**
	- TraceRays -
	Cast N uniformly distributed rays from each probe.
*/

[shader("raygeneration")]
void TraceRaysRGS()
{
	uint probeIdx = DispatchRaysIndex().y;
	uint rayIndex = DispatchRaysIndex().x;

	DDGIVolume volume = GetDDGIVolume(cPass.VolumeIndex);

	uint3 probeIdx3D = GetDDGIProbeIndex3D(volume, probeIdx);
	float3 probePosition = GetDDGIProbePosition(volume, probeIdx3D);
	const float maxDepth = Max3(volume.ProbeSize) * 2;
	float3x3 randomRotation = AngleAxis3x3(cPass.RandomAngle, cPass.RandomVector);

	RaytracingAccelerationStructure TLAS = ResourceDescriptorHeap[cView.TLASIndex];

	uint numRays = volume.NumRaysPerProbe;
	float3 direction = direction = mul(SphericalFibonacci(rayIndex, numRays), randomRotation);

	RayDesc ray;
	ray.Origin = probePosition;
	ray.Direction = direction;
	ray.TMin = RAY_BIAS;
	ray.TMax = RAY_MAX_T;
	RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];
	MaterialRayPayload payload = TraceMaterialRay(ray, tlas);

	float3 radiance = 0;
	float depth = maxDepth;

	if(payload.IsHit())
	{
		depth = min(payload.HitT, depth);

		if(payload.IsFrontFace())
		{
			MeshInstance instance = GetMeshInstance(payload.InstanceID);
			float4x4 world = GetTransform(NonUniformResourceIndex(instance.World));
			VertexAttribute vertex = GetVertexAttributes(instance, payload.Barycentrics, payload.PrimitiveID, (float4x3)world);
			MaterialData material = GetMaterial(instance.Material);
			const uint textureMipLevel = 6;
			MaterialProperties surface = GetMaterialProperties(material, vertex.UV, textureMipLevel);
			BrdfData brdfData = GetBrdfData(surface);

			float3 hitLocation = probePosition + direction * payload.HitT;
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

				RayDesc ray;
				ray.Origin = hitLocation;
				ray.Direction = normalize(L);
				ray.TMin = RAY_BIAS;
				ray.TMax = length(L);
				RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];
				attenuation *= TraceOcclusionRay(ray, tlas);

				if(attenuation <= 0.0f)
					continue;

				float3 diffuse = (attenuation * saturate(dot(N, L))) * Diffuse_Lambert(brdfData.Diffuse);
				radiance += diffuse * light.GetColor() * light.Intensity;
			}

			radiance += surface.Emissive;
			radiance += Diffuse_Lambert(min(brdfData.Diffuse, 0.9f)) * SampleDDGIIrradiance(hitLocation, N, direction);
		}
		else
		{
			// If backfacing, make negative so probes get pushed through the backface when offset.
			depth *= -0.2f;
		}
	}
	else
	{
		radiance = GetSky(direction);
	}

	uRayHitInfo[probeIdx * volume.MaxRaysPerProbe + rayIndex] = float4(radiance, depth);
}
