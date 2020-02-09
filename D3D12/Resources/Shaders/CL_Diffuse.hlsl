#include "Common.hlsl"
#include "Lighting.hlsl"

#define RootSig "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
				"CBV(b0, visibility=SHADER_VISIBILITY_VERTEX), " \
				"CBV(b1, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 3)), " \
				"DescriptorTable(SRV(t3, numDescriptors = 3), visibility=SHADER_VISIBILITY_PIXEL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1)), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), "

cbuffer PerObjectData : register(b0)
{
	float4x4 cWorld;
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

StructuredBuffer<uint2> tLightGrid : register(t3);
StructuredBuffer<uint> tLightIndexList : register(t4);
StructuredBuffer<Light> Lights : register(t5);
Texture2D tHeatMapTexture : register(t6);

uint GetSliceFromDepth(float depth)
{
    return floor(log(depth) * cSliceMagicA - cSliceMagicB);
}

int GetLightCount(float4 vPos, float4 pos)
{
	uint zSlice = GetSliceFromDepth(vPos.z);
    uint2 clusterIndexXY = floor(pos.xy / cClusterSize);
    uint clusterIndex1D = clusterIndexXY.x + (clusterIndexXY.y * cClusterDimensions.x) + (zSlice * (cClusterDimensions.x * cClusterDimensions.y));
	return tLightGrid[clusterIndex1D].y;
}

LightResult DoLight(float4 pos, float4 vPos, float3 wPos, float3 N, float3 V)
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
		LightResult result = DoLight(light, wPos, N, V);
		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	return totalResult;
}

float3 CalculateNormal(float3 N, float3 T, float3 BT, float2 tex, bool invertY)
{
	float3x3 normalMatrix = float3x3(T, BT, N);
	float3 sampledNormal = tNormalTexture.Sample(sDiffuseSampler, tex).rgb;
	sampledNormal.xy = sampledNormal.xy * 2.0f - 1.0f;
	if(invertY)
	{
		sampledNormal.y = -sampledNormal.y;
	}
	sampledNormal = normalize(sampledNormal);
	return mul(sampledNormal, normalMatrix);
}

[RootSignature(RootSig)]
PSInput VSMain(VSInput input)
{
	PSInput result;
	result.positionWS = mul(float4(input.position, 1.0f), cWorld);
	result.positionVS = mul(result.positionWS, cView);
	result.position = mul(result.positionVS, cProjection);
	result.texCoord = input.texCoord;
	result.normal = normalize(mul(input.normal, (float3x3)cWorld));
	result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
	result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	//float2 uv = float2((float)GetLightCount(input.positionVS, input.position) / 100, 0);
	//return tHeatMapTexture.Sample(sDiffuseSampler, uv);

	float4 diffuseSample = tDiffuseTexture.Sample(sDiffuseSampler, input.texCoord);

	float3 V = normalize(input.positionWS.xyz - cViewInverse[3].xyz);
	float3 N = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, true);

    LightResult lightResults = DoLight(input.position, input.positionVS, input.positionWS.xyz, N, V);
    float4 specularSample = tSpecularTexture.Sample(sDiffuseSampler, input.texCoord);
    lightResults.Specular *= specularSample;
   	lightResults.Diffuse *= diffuseSample;

	float4 color = saturate(lightResults.Diffuse + lightResults.Specular);
	color.a = diffuseSample.a;

	return color;
}