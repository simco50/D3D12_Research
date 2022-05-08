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
			MeshInstance instance = GetMeshInstance(payload.InstanceID);
			VertexAttribute vertex = GetVertexAttributes(instance, payload.Barycentrics, payload.PrimitiveID);
			MaterialData material = GetMaterial(instance.Material);
			const uint textureMipLevel = 2;
			MaterialProperties surface = GetMaterialProperties(material, vertex.UV, textureMipLevel);
			BrdfData brdfData = GetBrdfData(surface);

			float3 hitLocation = worldPosition + R * payload.HitT;
			float3 N = vertex.Normal;

			float3 viewPosition = mul(float4(hitLocation, 1), cView.View).xyz;
			float4 pos = float4(0, 0, 0, viewPosition.z);

			for(uint lightIndex = 0; lightIndex < cView.LightCount; ++lightIndex)
			{
				Light light = GetLight(lightIndex);
				float attenuation = GetAttenuation(light, hitLocation);
				if(attenuation <= 0.0f)
					continue;

				float3 L = light.Position - hitLocation;
				if(light.IsDirectional)
				{
					L = 100000.0f * -light.Direction;
				}

				if(light.CastShadows)
				{
					attenuation *= LightTextureMask(light, hitLocation);
				}

#if SECONDARY_SHADOW_RAY
				RayDesc rayDesc;
				rayDesc.Origin = hitLocation;
				rayDesc.Direction = normalize(L);
				rayDesc.TMin = RAY_BIAS;
				rayDesc.TMax = length(L);
				RaytracingAccelerationStructure tlas = ResourceDescriptorHeap[cView.TLASIndex];
				attenuation *= TraceOcclusionRay(rayDesc, tlas);
#else
				attenuation = 0.0f;
#endif // SECONDARY_SHADOW_RAY

				radiance += surface.Emissive;
				radiance += Diffuse_Lambert(brdfData.Diffuse) * SampleDDGIIrradiance(hitLocation, N, -V);

				if(attenuation <= 0.0f)
					continue;

				LightResult result = DefaultLitBxDF(brdfData.Specular, brdfData.pRoughness, brdfData.Diffuse, N, V, normalize(L), attenuation);
				radiance += result.Diffuse * light.GetColor() * light.Intensity;
				radiance += result.Specular * light.GetColor() * light.Intensity;
			}
		}
		else
		{
			radiance += GetSky(R);
		}

		colorSample += reflectivity * float4(radiance, 0);
	}
	uOutput[launchIndex] = colorSample;
}
