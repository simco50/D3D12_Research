#include "Common.hlsli"
#include "Lighting.hlsli"

#define BLOCK_SIZE 16

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_ALL), " \
				"CBV(b2, visibility=SHADER_VISIBILITY_PIXEL), " \
				GLOBAL_BINDLESS_TABLE ", " \
				"DescriptorTable(SRV(t2, numDescriptors = 8), visibility=SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, visibility = SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s1, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, visibility = SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s2, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL, comparisonFunc=COMPARISON_GREATER), " \

struct PerObjectData
{
	float4x4 World;
    int Diffuse;
    int Normal;
    int Roughness;
    int Metallic;
	int VertexBuffer;
};

struct PerViewData
{
	float4x4 View;
	float4x4 Projection;
	float4x4 ProjectionInverse;
	float4x4 ViewProjection;
	float4x4 ReprojectionMatrix;
	float4 ViewPosition;
	float2 InvScreenDimensions;
	float NearZ;
	float FarZ;
	int FrameIndex;
	int SsrSamples;
	int LightCount;
	int padd;
#if CLUSTERED_FORWARD
    int4 ClusterDimensions;
    int2 ClusterSize;
	float2 LightGridParams;
#endif
	int3 VolumeFogDimensions;
};

ConstantBuffer<PerObjectData> cObjectData : register(b0);
ConstantBuffer<PerViewData> cViewData : register(b1);

struct VSInput
{
	float3 position : POSITION;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : TEXCOORD1;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 positionWS : POSITION_WS;
	float3 positionVS : POSITION_VS;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : TEXCOORD1;
};

Texture3D<float4> tLightScattering : register(t2);

#if TILED_FORWARD
Texture2D<uint2> tLightGrid : register(t3);
#elif CLUSTERED_FORWARD
StructuredBuffer<uint2> tLightGrid : register(t3);
#endif
StructuredBuffer<uint> tLightIndexList : register(t4);

#if CLUSTERED_FORWARD
uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cViewData.LightGridParams.x - cViewData.LightGridParams.y);
}
#endif

float ScreenSpaceShadows(float3 worldPos, float3 lightDirection, int stepCount, float rayLength, float ditherOffset)
{
	float4 rayStartPS = mul(float4(worldPos, 1), cViewData.ViewProjection);
	float4 rayDirPS = mul(float4(-lightDirection * rayLength, 0), cViewData.ViewProjection);
	float4 rayEndPS = rayStartPS + rayDirPS;
	rayStartPS.xyz /= rayStartPS.w;
	rayEndPS.xyz /= rayEndPS.w;
	float3 rayStep = rayEndPS.xyz - rayStartPS.xyz;
	float stepSize = 1.0f / stepCount;

	float4 rayDepthClip = rayStartPS + mul(float4(0, 0, rayLength, 0), cViewData.Projection);
	rayDepthClip.xyz /= rayDepthClip.w;
	float tolerance = abs(rayDepthClip.z - rayStartPS.z) * stepSize * 2;

	float occlusion = 0.0f;
	float hitStep = -1.0f;

	float n = stepSize * ditherOffset + stepSize;

	[unroll]
	for(uint i = 0; i < stepCount; ++i)
	{
		float3 rayPos = rayStartPS.xyz + n * rayStep;
		float depth = tDepth.SampleLevel(sDiffuseSampler, rayPos.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f), 0).r;
		float diff = rayPos.z - depth;

		bool hit = abs(diff + tolerance) < tolerance;
		hitStep = hit && hitStep < 0.0f ? n : hitStep;
		n += stepSize;
	}
	if(hitStep > 0.0f)
	{
		float2 hitUV = rayStartPS.xy + n * rayStep.xy;
		hitUV = hitUV * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
		occlusion = ScreenFade(hitUV);
	}
	return 1.0f - occlusion;
}

LightResult DoLight(float4 pos, float3 worldPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
#if TILED_FORWARD
	uint2 tileIndex = uint2(floor(pos.xy / BLOCK_SIZE));
	uint startOffset = tLightGrid[tileIndex].x;
	uint lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
	uint3 clusterIndex3D = uint3(floor(pos.xy / cViewData.ClusterSize), GetSliceFromDepth(pos.w));
    uint clusterIndex1D = clusterIndex3D.x + (cViewData.ClusterDimensions.x * (clusterIndex3D.y + cViewData.ClusterDimensions.y * clusterIndex3D.z));
	uint startOffset = tLightGrid[clusterIndex1D].x;
	uint lightCount = tLightGrid[clusterIndex1D].y;
#else
	uint lightCount = cViewData.LightCount;
#endif
	LightResult totalResult = (LightResult)0;

	for(uint i = 0; i < lightCount; ++i)
	{
#if TILED_FORWARD || CLUSTERED_FORWARD
		uint lightIndex = tLightIndexList[startOffset + i];
#else
		uint lightIndex = i;
#endif
		Light light = tLights[lightIndex];
		LightResult result = DoLight(light, specularColor, diffuseColor, roughness, pos, worldPos, N, V);

#define SCREEN_SPACE_SHADOWS 0
#if SCREEN_SPACE_SHADOWS
		float3 L = normalize(worldPos - light.Position);
		if(light.Type == LIGHT_DIRECTIONAL)
		{
			L = light.Direction;
		}

		float ditherValue = InterleavedGradientNoise(pos.xy, cViewData.FrameIndex);
		float length = 0.1f * pos.w * cViewData.ProjectionInverse[1][1];
		float occlusion = ScreenSpaceShadows(worldPos, L, 8, length, ditherValue);

		result.Diffuse *= occlusion;
		result.Specular *= occlusion;
#endif

		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	return totalResult;
}

[RootSignature(RootSig)]
PSInput VSMain(uint VertexId : SV_VertexID)
{
	PSInput result;
	VSInput input = tBufferTable[cObjectData.VertexBuffer].Load<VSInput>(VertexId * sizeof(VSInput));
	result.positionWS = mul(float4(input.position, 1.0f), cObjectData.World).xyz;
	result.positionVS = mul(float4(result.positionWS, 1.0f), cViewData.View).xyz;
	result.position = mul(float4(result.positionWS, 1.0f), cViewData.ViewProjection);
	result.texCoord = input.texCoord;
	result.normal = normalize(mul(input.normal, (float3x3)cObjectData.World));
	result.tangent = normalize(mul(input.tangent, (float3x3)cObjectData.World));
	result.bitangent = normalize(mul(input.bitangent, (float3x3)cObjectData.World));
	return result;
}

float3 ScreenSpaceReflections(float4 position, float3 positionVS, float3 N, float3 V, float R)
{
	float3 ssr = 0;
	const float roughnessThreshold = 0.6f;
	bool ssrEnabled = R < roughnessThreshold;
	if(ssrEnabled)
	{
		float reflectionThreshold = 0.0f;
		float3 reflectionWs = normalize(reflect(-V, N));
		if (dot(V, reflectionWs) <= reflectionThreshold)
		{
			uint frameIndex = cViewData.FrameIndex;
			float jitter = InterleavedGradientNoise(position.xy, frameIndex) - 1.0f;
			uint maxSteps = cViewData.SsrSamples.x;

			float3 rayStartVS = positionVS;
			float linearDepth = rayStartVS.z;
			float3 reflectionVs = mul(reflectionWs, (float3x3)cViewData.View);
			float3 rayEndVS = rayStartVS + (reflectionVs * linearDepth);

			float3 rayStart = ViewToWindow(rayStartVS, cViewData.Projection);
			float3 rayEnd = ViewToWindow(rayEndVS, cViewData.Projection);

			float3 rayStep = ((rayEnd - rayStart) / float(maxSteps));
			rayStep = rayStep / length(rayEnd.xy - rayStart.xy);
			float3 rayPos = rayStart + (rayStep * jitter);
			float zThickness = abs(rayStep.z);

			uint hitIndex = 0;
			float3 bestHit = rayPos;
			float prevSceneZ = rayStart.z;
			for (uint currStep = 0; currStep < maxSteps; currStep += 4)
			{
				uint4 step = float4(1, 2, 3, 4) + currStep;
				float4 sceneZ = float4(
					tDepth.SampleLevel(sClampSampler, rayPos.xy + rayStep.xy * step.x, 0).x,
					tDepth.SampleLevel(sClampSampler, rayPos.xy + rayStep.xy * step.y, 0).x,
					tDepth.SampleLevel(sClampSampler, rayPos.xy + rayStep.xy * step.z, 0).x,
					tDepth.SampleLevel(sClampSampler, rayPos.xy + rayStep.xy * step.w, 0).x
				);
				float4 currentPosition = rayPos.z + rayStep.z * step;
				uint4 zTest = abs(sceneZ - currentPosition - zThickness) < zThickness;
				uint zMask = (((zTest.x << 0) | (zTest.y << 1)) | (zTest.z << 2)) | (zTest.w << 3);
				if(zMask > 0)
				{
					uint firstHit = firstbitlow(zMask);
					if(firstHit > 0)
					{
						prevSceneZ = sceneZ[firstHit - 1];
					}
					bestHit = rayPos + (rayStep * float(currStep + firstHit + 1));
					float zAfter = sceneZ[firstHit] - bestHit.z;
					float zBefore = (prevSceneZ - bestHit.z) + rayStep.z;
					float weight = saturate(zAfter / (zAfter - zBefore));
					float3 prevRayPos = bestHit - rayStep;
					bestHit = (prevRayPos * weight) + (bestHit * (1.0f - weight));
					hitIndex = currStep + firstHit;
					break;
				}
				prevSceneZ = sceneZ.w;
			}

			float4 hitColor = 0;
			if (hitIndex > 0)
			{
				float4 texCoord = float4(bestHit.xy, 0, 1);
				texCoord = mul(texCoord, cViewData.ReprojectionMatrix);
				float2 distanceFromCenter = (float2(texCoord.x, texCoord.y) * 2.0f) - float2(1.0f, 1.0f);
				float edgeAttenuation = saturate((1.0 - ((float)hitIndex / maxSteps)) * 4.0f);
				edgeAttenuation *= smoothstep(0.0f, 0.5f, saturate(1.0 - dot(distanceFromCenter, distanceFromCenter)));
				float3 reflectionResult = tPreviousSceneColor.SampleLevel(sClampSampler, texCoord.xy, 0).xyz;
				hitColor = float4(reflectionResult, edgeAttenuation);
			}
			float roughnessMask = saturate(1.0f - (R / roughnessThreshold));
			float ssrWeight = (hitColor.w * roughnessMask);
			ssr = saturate(hitColor.xyz * ssrWeight);
		}
	}
	return ssr;
}

void PSMain(PSInput input,
			out float4 outColor : SV_TARGET0,
			out float4 outNormalRoughness : SV_TARGET1)
{
	float4 baseColor = tTexture2DTable[cObjectData.Diffuse].Sample(sDiffuseSampler, input.texCoord);
	float3 sampledNormal = tTexture2DTable[cObjectData.Normal].Sample(sDiffuseSampler, input.texCoord).xyz;
	float metalness = tTexture2DTable[cObjectData.Metallic].Sample(sDiffuseSampler, input.texCoord).r;
	float r = tTexture2DTable[cObjectData.Roughness].Sample(sDiffuseSampler, input.texCoord).r;
	float3 specular = 0.5f;

	float3 diffuseColor = ComputeDiffuseColor(baseColor.rgb, metalness);
	float3 specularColor = ComputeF0(specular.r, baseColor.rgb, metalness);

	float3x3 TBN = float3x3(normalize(input.tangent), normalize(input.bitangent), normalize(input.normal));
	float3 N = TangentSpaceNormalMapping(sampledNormal, TBN, true);
	float3 V = normalize(cViewData.ViewPosition.xyz - input.positionWS);	
	float3 ssr = ScreenSpaceReflections(input.position, input.positionVS, N, V, r);

	LightResult lighting = DoLight(input.position, input.positionWS, N, V, diffuseColor, specularColor, r);

	float2 screenUV = (float2)input.position.xy * cViewData.InvScreenDimensions;
	float ao = tAO.SampleLevel(sDiffuseSampler, screenUV, 0).r;
	float3 color = lighting.Diffuse + lighting.Specular;
	color += ApplyAmbientLight(diffuseColor, ao, tLights[0].GetColor().rgb * 0.1f);
	color += ssr * ao;

	float slice = sqrt((input.positionVS.z - cViewData.FarZ) / (cViewData.NearZ - cViewData.FarZ));
	float4 scatteringTransmittance = tLightScattering.SampleLevel(sClampSampler, float3(screenUV, slice), 0);
	color = color * scatteringTransmittance.w + scatteringTransmittance.rgb;

	outColor = float4(color, baseColor.a);

    float reflectivity = scatteringTransmittance.w * ao * saturate(pow(1.0 - saturate(dot(V, N)), 5.0));
	outNormalRoughness = float4(N, reflectivity);
	//outNormalRoughness = float4(input.normal, 1);
}
