#include "Common.hlsli"
#include "RayTracingCommon.hlsli"
#include "Lighting.hlsli"
#include "DDGICommon.hlsli"

struct PassParams
{
	float3 RandomVector;
	float RandomAngle;
	uint VolumeIndex;
	
	RWTypedBufferH<float4> RayHitInfo;
};
DEFINE_CONSTANTS(PassParams, 0);

/**
	- TraceRays -
	Cast N uniformly distributed rays from each probe.
*/

[shader("raygeneration")]
void TraceRaysRGS()
{
	uint probeIdx = DispatchRaysIndex().y;
	uint rayIndex = DispatchRaysIndex().x;
	DDGIVolume volume = GetDDGIVolume(cPassParams.VolumeIndex);
	uint3 probeIdx3D = GetDDGIProbeIndex3D(volume, probeIdx);

	// If the probe is inactive, just trace the stable rays to determine if we have to re-activate the probe
	if(!DDGIIsProbeActive(volume, probeIdx3D) && rayIndex >= DDGI_NUM_STABLE_RAYS)
		return;

	float3 probePosition = GetDDGIProbePosition(volume, probeIdx3D);
	const float maxDepth = MaxComponent(volume.ProbeSize) * 2;
	float3x3 randomRotation = AngleAxis3x3(cPassParams.RandomAngle, cPassParams.RandomVector);

	RayDesc ray;
	ray.Origin = probePosition;
	ray.Direction = DDGIGetRayDirection(rayIndex, volume.NumRaysPerProbe, randomRotation);
	ray.TMin = RAY_BIAS;
	ray.TMax = FLT_MAX;
	MaterialRayPayload payload = TraceMaterialRay(ray, cView.TLAS.Get());

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
				attenuation *= TraceOcclusionRay(rayDesc, cView.TLAS.Get());

				if(attenuation <= 0.0f)
					continue;

				float3 diffuse = (attenuation * saturate(dot(N, L))) * Diffuse_Lambert(brdfData.Diffuse);
				radiance += diffuse * light.GetColor();
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

	cPassParams.RayHitInfo.Store(probeIdx * volume.MaxRaysPerProbe + rayIndex, float4(radiance, depth));
}
