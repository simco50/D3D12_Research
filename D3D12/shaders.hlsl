cbuffer Data : register(b0)
{
	float4x4 World;
	float4x4 WorldViewProjection;
}

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float3 normal : NORMAL;
};

PSInput VSMain(VSInput input)
{
	PSInput result;
	
	result.position = mul(float4(input.position, 1.0f), WorldViewProjection);
	result.normal = normalize(mul(input.normal, (float3x3)World));
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 lightDirection = -normalize(float3(-1, -1, 1));
	float diffuse = saturate(dot(lightDirection, input.normal));
	return diffuse * float4(1, 1, 1, 1);
}