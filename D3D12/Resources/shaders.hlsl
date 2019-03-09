cbuffer PerObjectData : register(b0)
{
	float4x4 World;
	float4x4 WorldViewProjection;
}

struct VSInput
{
	float3 position : POSITION;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
};

SamplerState sDiffuse : register(s0);
Texture2D tDiffuse : register(t0);
Texture2D tNormal : register(t1);

float3 CalculateNormal(float3 normal, float3 tangent, float2 texCoord, bool invertY)
{
	float3 binormal = normalize(cross(tangent, normal));
	float3x3 normalMatrix = float3x3(tangent, binormal, normal);

	float3 sampledNormal = tNormal.Sample(sDiffuse, texCoord).rgb;
	sampledNormal = sampledNormal * 2.0f - 1.0f;
	if(invertY)
		sampledNormal.y = -sampledNormal.y;
	sampledNormal = saturate(sampledNormal);

	return mul(sampledNormal, normalMatrix);
}

PSInput VSMain(VSInput input)
{
	PSInput result;
	
	result.position = mul(float4(input.position, 1.0f), WorldViewProjection);
	result.texCoord = input.texCoord;
	result.normal = normalize(mul(input.normal, (float3x3)World));
	result.tangent = normalize(mul(input.tangent, (float3x3)World));
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 lightDirection = -normalize(float3(-1, -1, 1));

	//float3 normal = CalculateNormal(input.normal, input.tangent, input.texCoord, false);
	float diffuse = saturate(dot(lightDirection, input.normal));

	float4 s = tDiffuse.Sample(sDiffuse, input.texCoord);

	return diffuse * s;
}