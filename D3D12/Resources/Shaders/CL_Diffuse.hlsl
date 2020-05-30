#include "Common.hlsli"
#include "Lighting.hlsli"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_ALL), " \
				"CBV(b2, visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 3)), " \
				"DescriptorTable(SRV(t3, numDescriptors = 5), visibility=SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, visibility = SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s1, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL, comparisonFunc=COMPARISON_GREATER), "

cbuffer PerObjectData : register(b0)
{
	float4x4 cWorld;
	float4x4 cWorldViewProj;
}

cbuffer PerFrameData : register(b1)
{
	float4x4 cView;
	float4x4 cProjection;
	float4x4 cViewInverse;
    uint4 cClusterDimensions;
	float2 cScreenDimensions;
	float cNearZ;
	float cFarZ;
    float2 cClusterSize;
	float cSliceMagicA;
	float cSliceMagicB;
}

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
	float4 positionVS : POSITION_VS;
	float4 positionWS : POSITION_WS;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : TEXCOORD1;
};

Texture2D tDiffuseTexture : register(t0);
Texture2D tNormalTexture : register(t1);
Texture2D tSpecularTexture : register(t2);

SamplerState sDiffuseSampler : register(s0);

StructuredBuffer<uint2> tLightGrid : register(t4);
StructuredBuffer<uint> tLightIndexList : register(t5);
StructuredBuffer<Light> Lights : register(t6);
Texture2D tAO : register(t7);

uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cSliceMagicA - cSliceMagicB);
}

LightResult DoLight(float4 pos, float3 vPos, float3 worldPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
    uint3 clusterIndex3D = uint3(floor(pos.xy / cClusterSize), GetSliceFromDepth(vPos.z));
    uint clusterIndex1D = clusterIndex3D.x + (cClusterDimensions.x * (clusterIndex3D.y + cClusterDimensions.y * clusterIndex3D.z));

	uint startOffset = tLightGrid[clusterIndex1D].x;
	uint lightCount = tLightGrid[clusterIndex1D].y;
	LightResult totalResult = (LightResult)0;

	for(uint i = 0; i < lightCount; ++i)
	{
		uint lightIndex = tLightIndexList[startOffset + i];
		Light light = Lights[lightIndex];
		LightResult result = DoLight(light, specularColor, diffuseColor, roughness, pos, worldPos, vPos, N, V);
		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	return totalResult;
}

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
	PSInput result;
	result.positionWS = mul(float4(input.position, 1.0f), cWorld);
	result.positionVS = mul(result.positionWS, cView);
	result.position = mul(float4(input.position, 1.0f), cWorldViewProj);
	result.texCoord = input.texCoord;
	result.normal = normalize(mul(input.normal, (float3x3)cWorld));
	result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
	result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
	return result;
}

#define G_SCATTERING 0.0001f
float ComputeScattering(float LoV)
{
	float result = 1.0f - G_SCATTERING * G_SCATTERING;
	result /= (4.0f * PI * pow(1.0f + G_SCATTERING * G_SCATTERING - (2.0f * G_SCATTERING) * LoV, 1.5f));
	return result;
}

[earlydepthstencil]
float4 PSMain(PSInput input) : SV_TARGET
{
	float4 baseColor = tDiffuseTexture.Sample(sDiffuseSampler, input.texCoord);
	float3 specular = 0.5f;
	float metalness = 0;
	float r = 0.5f;

	float3 diffuseColor = ComputeDiffuseColor(baseColor.rgb, metalness);
	float3 specularColor = ComputeF0(specular.r, baseColor.rgb, metalness);

	float3x3 TBN = float3x3(normalize(input.tangent), normalize(input.bitangent), normalize(input.normal));
	float3 N = TangentSpaceNormalMapping(tNormalTexture, sDiffuseSampler, TBN, input.texCoord, true);
	float3 V = normalize(cViewInverse[3].xyz - input.positionWS.xyz);

	LightResult lighting = DoLight(input.position, input.positionVS.xyz, input.positionWS.xyz, N, V, diffuseColor, specularColor, r);
	
	float3 color = lighting.Diffuse + lighting.Specular;

	float ao = tAO.SampleLevel(sDiffuseSampler, (float2)input.position.xy / cScreenDimensions, 0).r;
	color += ApplyAmbientLight(diffuseColor, ao, Lights[0].GetColor().rgb * 0.1f);

#define VOLUMETRIC_LIGHT 1
#if VOLUMETRIC_LIGHT
	const float fogValue = 0.1f;
	const uint samples = 10;
	float3 cameraPos = cViewInverse[3].xyz;
	float3 worldPos = input.positionWS.xyz;
	float3 rayVector = cameraPos - worldPos;
	float3 rayStep = rayVector / samples;
	float3 accumFog = 0.0f.xxx;

	float3 currentPosition = worldPos;

	static float DitherPattern[4][4] = 
		{{ 0.0f, 0.5f, 0.125f, 0.625f},
		{ 0.75f, 0.22f, 0.875f, 0.375f},
		{ 0.1875f, 0.6875f, 0.0625f, 0.5625},
		{ 0.9375f, 0.4375f, 0.8125f, 0.3125}};
		
	float ditherValue = DitherPattern[floor(input.position.x) % 4][floor(input.position.y) % 4];
	currentPosition += rayStep * ditherValue;

	for(int i = 0; i < samples; ++i)
	{
		float4 vPos = mul(float4(currentPosition, 1), cView);
		float4 splits = vPos.z > cCascadeDepths;
		int cascadeIndex = dot(splits, float4(1, 1, 1, 1));
		int shadowMapIndex = Lights[0].ShadowIndex + cascadeIndex;

		float4x4 lightViewProjection = cLightViewProjections[shadowMapIndex];
		float4 lightPos = mul(float4(currentPosition, 1), lightViewProjection);
		lightPos.xyz /= lightPos.w;
		lightPos.x = lightPos.x / 2.0f + 0.5f;
		lightPos.y = lightPos.y / -2.0f + 0.5f;

		float2 shadowMapStart = cShadowMapOffsets[shadowMapIndex].xy;
		float2 normalizedShadowMapSize = cShadowMapOffsets[shadowMapIndex].zw;

		float2 texCoord = shadowMapStart + float2(lightPos.x * normalizedShadowMapSize.x, lightPos.y * normalizedShadowMapSize.y); 
		float shadowDepth = tShadowMapTexture.SampleLevel(sDiffuseSampler, texCoord, 0).r;
		if(shadowDepth < lightPos.z)
		{
			accumFog += fogValue * ComputeScattering(dot(rayVector, Lights[0].Direction)).xxx * Lights[0].GetColor().rgb * Lights[0].Intensity;
		}
		currentPosition += rayStep;
	}
	accumFog /= samples;
	color += accumFog;
#endif

	return float4(color, baseColor.a);
}