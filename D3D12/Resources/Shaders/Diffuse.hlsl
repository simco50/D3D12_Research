#include "Common.hlsli"
#include "Lighting.hlsli"

#define BLOCK_SIZE 16

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_ALL), " \
				"CBV(b2, visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 3), visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t3, numDescriptors = 4), visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(SRV(t10, numDescriptors = 32, space = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, visibility = SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s1, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, visibility = SHADER_VISIBILITY_PIXEL, comparisonFunc=COMPARISON_GREATER), " \

struct PerObjectData
{
	float4x4 World;
	float4x4 WorldViewProj;
};

struct PerViewData
{
	float4x4 View;
	float4x4 ViewInverse;
	float4x4 Projection;
	float2 ScreenDimensions;
	float NearZ;
	float FarZ;
#if CLUSTERED_FORWARD
    int4 ClusterDimensions;
    int2 ClusterSize;
	float2 LightGridParams;
#endif
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

LightResult DoLight(float4 pos, float3 worldPos, float3 vPos, float3 N, float3 V, float3 diffuseColor, float3 specularColor, float roughness)
{
#if TILED_FORWARD
	uint2 tileIndex = uint2(floor(pos.xy / BLOCK_SIZE));
	uint startOffset = tLightGrid[tileIndex].x;
	uint lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
	uint3 clusterIndex3D = uint3(floor(pos.xy / cViewData.ClusterSize), GetSliceFromDepth(vPos.z));
    uint clusterIndex1D = clusterIndex3D.x + (cViewData.ClusterDimensions.x * (clusterIndex3D.y + cViewData.ClusterDimensions.y * clusterIndex3D.z));
	uint startOffset = tLightGrid[clusterIndex1D].x;
	uint lightCount = tLightGrid[clusterIndex1D].y;
#endif
	LightResult totalResult = (LightResult)0;

	for(uint i = 0; i < lightCount; ++i)
	{
		uint lightIndex = tLightIndexList[startOffset + i];
		Light light = tLights[lightIndex];
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
	result.position = mul(float4(input.position, 1.0f), cObjectData.WorldViewProj);
	result.positionWS = mul(float4(input.position, 1.0f), cObjectData.World).xyz;
	result.positionVS = mul(float4(result.positionWS, 1.0f), cViewData.View).xyz;
	result.texCoord = input.texCoord;
	result.normal = normalize(mul(input.normal, (float3x3)cObjectData.World));
	result.tangent = normalize(mul(input.tangent, (float3x3)cObjectData.World));
	result.bitangent = normalize(mul(input.bitangent, (float3x3)cObjectData.World));
	return result;
}

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
	float3 V = normalize(cViewData.ViewInverse[3].xyz - input.positionWS);

	LightResult lighting = DoLight(input.position, input.positionWS, input.positionVS, N, V, diffuseColor, specularColor, r);
	
	float3 color = lighting.Diffuse + lighting.Specular; 

	//Constant ambient
	float ao = tAO.SampleLevel(sDiffuseSampler, (float2)input.position.xy / cViewData.ScreenDimensions, 0).r;
	color += ApplyAmbientLight(diffuseColor, ao, tLights[0].GetColor().rgb * 0.1f);
	color += ApplyVolumetricLighting(cViewData.ViewInverse[3].xyz, input.positionWS.xyz, input.position.xyz, cViewData.View, tLights[0], 10);

	return float4(color, baseColor.a);
}