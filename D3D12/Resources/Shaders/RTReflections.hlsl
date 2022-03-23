#include "Common.hlsli"
#include "ShadingModels.hlsli"
#include "Lighting.hlsli"
#include "RaytracingCommon.hlsli"
#include "Random.hlsli"

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
		
		MaterialRayPayload payload = TraceMaterialRay(worldPosition, R);
		if(payload.IsHit())
		{
			MeshInstance instance = GetMeshInstance(payload.InstanceID);
			float4x4 world = GetTransform(NonUniformResourceIndex(instance.World));
			VertexAttribute vertex = GetVertexAttributes(instance, payload.Barycentrics, payload.PrimitiveID, (float4x3)world);
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
					L = RAY_MAX_T * -light.Direction;
				}

				int shadowIndex = GetShadowIndex(light, pos, hitLocation);
				bool castShadowRay = true;
				if(shadowIndex >= 0)
				{
					float4x4 lightViewProjection = cView.LightViewProjections[shadowIndex];
					float4 lightPos = mul(float4(hitLocation, 1), lightViewProjection);
					lightPos.xyz /= lightPos.w;
					lightPos.x = lightPos.x / 2.0f + 0.5f;
					lightPos.y = lightPos.y / -2.0f + 0.5f;
					attenuation *= LightTextureMask(light, shadowIndex, hitLocation);

					if(all(lightPos >= 0) && all(lightPos <= 1))
					{
						Texture2D shadowTexture = ResourceDescriptorHeap[cView.ShadowMapOffset + shadowIndex];
						attenuation *= shadowTexture.SampleCmpLevelZero(sLinearClampComparisonGreater, lightPos.xy, lightPos.z);
						castShadowRay = false;
					}
				}

				if(castShadowRay)
				{
#if SECONDARY_SHADOW_RAY
					attenuation *= TraceOcclusionRay(hitLocation, normalize(L), length(L));
#else
					attenuation = 0.0f;
#endif // SECONDARY_SHADOW_RAY
				}

				if(attenuation <= 0.0f)
					continue;

				LightResult result = DefaultLitBxDF(brdfData.Specular, brdfData.Roughness, brdfData.Diffuse, N, V, normalize(L), attenuation);
				radiance += result.Diffuse * light.GetColor() * light.Intensity;
				radiance += result.Specular * light.GetColor() * light.Intensity;
				radiance += surface.Emissive;
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
