#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"
#include "RaytracingCommon.hlsli"
#include "Random.hlsli"
#include "DDGICommon.hlsli"

#define RAY_CONE_TEXTURE_LOD 1
#define SECONDARY_SHADOW_RAY 1

struct PassParams
{
	Texture2DH<float> Depth;
	Texture2DH<float4> PreviousSceneColor;
	Texture2DH<float2> SceneNormals;
	Texture2DH<float> SceneRoughness;
	RWTexture2DH<float4> Output;
};
DEFINE_CONSTANTS(PassParams, 0);


[shader("raygeneration")]
void RayGen()
{
	float2 dimInv = rcp(DispatchRaysDimensions().xy);
	uint2 launchIndex = DispatchRaysIndex().xy;
	float2 uv = TexelToUV(launchIndex, dimInv);

	float depth = cPassParams.Depth.SampleLevel(sPointClamp, uv, 0).r;
	float4 colorSample = cPassParams.PreviousSceneColor.SampleLevel(sLinearClamp, uv, 0);
	float3 N = Octahedral::Unpack(cPassParams.SceneNormals.SampleLevel(sLinearClamp, uv, 0));
	float R = cPassParams.SceneRoughness.SampleLevel(sLinearClamp, uv, 0);

	float3 worldPosition = WorldPositionFromDepth(uv, depth, cView.ClipToWorld);

	float reflectivity = R;

	if(depth > 0 && reflectivity > 0.0f)
	{
		float3 V = normalize(worldPosition - cView.ViewToWorld[3].xyz);
		float3 R = reflect(V, N);

		float3 radiance = 0;

		RayDesc ray;
		ray.Origin = worldPosition;
		ray.Direction = R;
		ray.TMin = RAY_BIAS;
		ray.TMax = FLT_MAX;
		MaterialRayPayload payload = TraceMaterialRay(ray, cView.TLAS.Get());

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
				attenuation *= TraceOcclusionRay(rayDesc, cView.TLAS.Get());
#else
				attenuation = 0.0f;
#endif // SECONDARY_SHADOW_RAY

				if(attenuation <= 0.0f)
					continue;

				radiance += DefaultLitBxDF(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, N, V, L) * attenuation * light.GetColor();
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
	cPassParams.Output.Store(launchIndex, colorSample);
}
