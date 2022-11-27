#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"
#include "RaytracingCommon.hlsli"
#include "Random.hlsli"
#include "DDGICommon.hlsli"

#define RAY_CONE_TEXTURE_LOD 1
#define SECONDARY_SHADOW_RAY 1

struct PassParameters
{
	float ViewPixelSpreadAngle;
};

Texture2D tDepth : register(t0);
Texture2D tPreviousSceneColor :	register(t1);
Texture2D<float2> tSceneNormals : register(t2);
Texture2D<float> tSceneRoughness : register(t3);

RWTexture2D<float4> uOutput : register(u0);
ConstantBuffer<PassParameters> cPass : register(b0);

[shader("raygeneration")]
void RayGen()
{
	float2 dimInv = rcp(DispatchRaysDimensions().xy);
	uint2 launchIndex = DispatchRaysIndex().xy;
	float2 uv = (float2)(launchIndex + 0.5f) * dimInv;

	float depth = tDepth.SampleLevel(sLinearClamp, uv, 0).r;
	float4 colorSample = tPreviousSceneColor.SampleLevel(sLinearClamp, uv, 0);
	float3 N = DecodeNormalOctahedron(tSceneNormals.SampleLevel(sLinearClamp, uv, 0));
	float R = tSceneRoughness.SampleLevel(sLinearClamp, uv, 0);

	float3 worldPosition = WorldFromDepth(uv, depth, cView.ViewProjectionInverse);

	float reflectivity = R;

	if(depth > 0 && reflectivity > 0.0f)
	{
		float3 V = normalize(worldPosition - cView.ViewInverse[3].xyz);
		float3 R = reflect(V, N);

		float3 radiance = 0;

		RayDesc ray;
		ray.Origin = worldPosition;
		ray.Direction = R;
		ray.TMin = RAY_BIAS;
		ray.TMax = FLT_MAX;
		RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];
		MaterialRayPayload payload = TraceMaterialRay(ray, tlas);

		if(payload.IsHit())
		{
			InstanceData instance = GetInstance(payload.InstanceID);
			VertexAttribute vertex = GetVertexAttributes(instance, payload.Barycentrics, payload.PrimitiveID);
			MaterialData material = GetMaterial(instance.MaterialIndex);
			const uint textureMipLevel = 2;
			MaterialProperties surface = EvaluateMaterial(material, vertex, textureMipLevel);
			BrdfData brdfData = GetBrdfData(surface);

			float3 hitLocation = worldPosition + R * payload.HitT;
			float3 N = vertex.Normal;

			for(uint lightIndex = 0; lightIndex < cView.LightCount; ++lightIndex)
			{
				Light light = GetLight(lightIndex);
				float3 L;
				float attenuation = GetAttenuation(light, hitLocation, L);
				if(attenuation <= 0.0f)
					continue;

#if SECONDARY_SHADOW_RAY
				RayDesc rayDesc = CreateLightOcclusionRay(light, hitLocation);
				attenuation *= TraceOcclusionRay(rayDesc, tlas);
#else
				attenuation = 0.0f;
#endif // SECONDARY_SHADOW_RAY
				
				if(attenuation <= 0.0f)
					continue;

				LightResult result = DefaultLitBxDF(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, N, V, L, attenuation);
				radiance += result.Diffuse * light.GetColor() * light.Intensity;
				radiance += result.Specular * light.GetColor() * light.Intensity;
			}

			radiance += surface.Emissive;
			radiance += Diffuse_Lambert(brdfData.Diffuse) * SampleDDGIIrradiance(hitLocation, N, -V);
		}
		else
		{
			radiance += GetSky(R);
		}

		colorSample += reflectivity * float4(radiance, 0);
	}
	uOutput[launchIndex] = colorSample;
}
