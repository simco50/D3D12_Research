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

	// If the probe is inactive, just trace the stable rays to determine if we have to re-activate the probe
	if(!DDGIIsProbeActive(volume, probeIdx3D) && rayIndex >= DDGI_NUM_STABLE_RAYS)
		return;

	float3 probePosition = GetDDGIProbePosition(volume, probeIdx3D);
	const float maxDepth = Max3(volume.ProbeSize) * 2;
	float3x3 randomRotation = AngleAxis3x3(cPass.RandomAngle, cPass.RandomVector);

	RaytracingAccelerationStructure TLAS = ResourceDescriptorHeap[cView.TLASIndex];

	RayDesc ray;
	ray.Origin = probePosition;
	ray.Direction = DDGIGetRayDirection(rayIndex, volume.NumRaysPerProbe, randomRotation);
	ray.TMin = RAY_BIAS;
	ray.TMax = FLT_MAX;
	RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];
	MaterialRayPayload payload = TraceMaterialRay(ray, tlas);

	float depth = maxDepth;
	float3 radiance = 0;

	if(payload.IsHit())
	{
		depth = min(payload.HitT, depth);

		if(payload.IsFrontFace())
		{
			InstanceData instance = GetInstance(payload.InstanceID);
			VertexAttribute vertex = GetVertexAttributes(instance, payload.Barycentrics, payload.PrimitiveID);
			MaterialData material = GetMaterial(instance.MaterialIndex);
			const uint textureMipLevel = 6;
			MaterialProperties surface = EvaluateMaterial(material, vertex, textureMipLevel);
			BrdfData brdfData = GetBrdfData(surface);

			float3 hitLocation = probePosition + ray.Direction * payload.HitT;
			float3 N = vertex.Normal;

			for(uint lightIndex = 0; lightIndex < cView.LightCount; ++lightIndex)
			{
				Light light = GetLight(lightIndex);

				float3 L;
				float attenuation = GetAttenuation(light, hitLocation, L);
				if(attenuation <= 0.0f)
					continue;

				RayDesc rayDesc = CreateLightOcclusionRay(light, hitLocation);
				attenuation *= TraceOcclusionRay(rayDesc, tlas);

				if(attenuation <= 0.0f)
					continue;

				float3 diffuse = (attenuation * saturate(dot(N, L))) * Diffuse_Lambert(brdfData.Diffuse);
				radiance += diffuse * light.GetColor() * light.Intensity;
			}

			radiance += surface.Emissive;
			radiance += Diffuse_Lambert(min(brdfData.Diffuse, 0.9f)) * SampleDDGIIrradiance(hitLocation, N, ray.Direction);
		}
		else
		{
			// If backfacing, make negative so probes get pushed through the backface when offset.
			depth *= DDGI_BACKFACE_DEPTH_MULTIPLIER;
		}
	}
	else
	{
		radiance = GetSky(ray.Direction);
	}

	uRayHitInfo[probeIdx * volume.MaxRaysPerProbe + rayIndex] = float4(radiance, depth);
}
