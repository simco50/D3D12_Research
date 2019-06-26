#include "Common.hlsl"
#include "Constants.hlsl"
#include "Lighting.hlsl"

cbuffer PerObjectData : register(b0)
{
	float4x4 cWorld;
	float4x4 cWorldViewProjection;
	float4x4 cWorldView;
}

cbuffer PerFrameData : register(b1)
{
	float4x4 cViewInverse;
    uint4 cClusterDimensions;
	float2 cScreenDimensions;
	float cNearZ;
	float cFarZ;
    float2 cClusterSize;
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
	float4 vsPosition : POSITION_VS;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	float3 bitangent : TEXCOORD1;
	float4 worldPosition : TEXCOORD3;
};

Texture2D tDiffuseTexture : register(t0);
SamplerState sDiffuseSampler : register(s0);

Texture2D tNormalTexture : register(t1);
SamplerState sNormalSampler : register(s1);

Texture2D tSpecularTexture : register(t2);

StructuredBuffer<uint2> tLightGrid : register(t3);
StructuredBuffer<uint> tLightIndexList : register(t4);

StructuredBuffer<Light> Lights : register(t5);

uint GetSliceFromDepth(float depth)
{
    float aConstant = cClusterDimensions.z / log(cFarZ / cNearZ);
    float bConstant = (cClusterDimensions.z * log(cNearZ)) / log(cFarZ / cNearZ);
    return floor(log(depth) * aConstant - bConstant);
}

LightResult DoLight(float4 position, float4 viewSpacePosition, float3 worldPosition, float3 normal, float3 viewDirection)
{
	uint zSlice = GetSliceFromDepth(viewSpacePosition.z);
    uint2 clusterIndexXY = floor(position.xy / cClusterSize);
    uint clusterIndex1D = clusterIndexXY.x + (clusterIndexXY.y * cClusterDimensions.x) + (zSlice * (cClusterDimensions.x * cClusterDimensions.y));

	uint startOffset = tLightGrid[clusterIndex1D].x;
	uint lightCount = tLightGrid[clusterIndex1D].y;
	LightResult totalResult = (LightResult)0;

	for(uint i = 0; i < lightCount; ++i)
	{
		uint lightIndex = tLightIndexList[startOffset + i];
		Light light = Lights[lightIndex];

		LightResult result = (LightResult)0;

		switch(light.Type)
		{
		case LIGHT_DIRECTIONAL:
			result = DoDirectionalLight(light, worldPosition, normal, viewDirection);
			break;
		case LIGHT_POINT:
			result = DoPointLight(light, worldPosition, normal, viewDirection);
			break;
		case LIGHT_SPOT:
			result = DoSpotLight(light, worldPosition, normal, viewDirection);
			break;
		default:
			//Unsupported light type
			result.Diffuse = float4(1, 0, 1, 1);
			result.Specular = float4(0, 0, 0, 1);
			break;
		}

		totalResult.Diffuse += result.Diffuse;
		totalResult.Specular += result.Specular;
	}

	return totalResult;
}

float3 CalculateNormal(float3 normal, float3 tangent, float3 bitangent, float2 texCoord, bool invertY)
{
	float3x3 normalMatrix = float3x3(tangent, bitangent, normal);
	float3 sampledNormal = tNormalTexture.Sample(sNormalSampler, texCoord).rgb;
	sampledNormal.xy = sampledNormal.xy * 2.0f - 1.0f;
	if(invertY)
	{
		sampledNormal.y = -sampledNormal.y;
	}
	sampledNormal = normalize(sampledNormal);
	return mul(sampledNormal, normalMatrix);
}

PSInput VSMain(VSInput input)
{
	PSInput result;
	
	result.position = mul(float4(input.position, 1.0f), cWorldViewProjection);
	result.texCoord = input.texCoord;
	result.normal = normalize(mul(input.normal, (float3x3)cWorld));
	result.tangent = normalize(mul(input.tangent, (float3x3)cWorld));
	result.bitangent = normalize(mul(input.bitangent, (float3x3)cWorld));
	result.worldPosition = mul(float4(input.position, 1.0f), cWorld);
	result.vsPosition = mul(float4(input.position, 1.0f), cWorldView);
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 diffuseSample = tDiffuseTexture.Sample(sDiffuseSampler, input.texCoord);

	float3 viewDirection = normalize(input.worldPosition.xyz - cViewInverse[3].xyz);
	float3 normal = CalculateNormal(normalize(input.normal), normalize(input.tangent), normalize(input.bitangent), input.texCoord, true);

    LightResult lightResults = DoLight(input.position, input.vsPosition, input.worldPosition.xyz, input.normal, viewDirection);
    float4 specularSample = tSpecularTexture.Sample(sDiffuseSampler, input.texCoord);
    lightResults.Specular *= specularSample;
   	lightResults.Diffuse *= diffuseSample;

	float4 color = saturate(lightResults.Diffuse + lightResults.Specular);
	color.a = diffuseSample.a;

	return color;
}