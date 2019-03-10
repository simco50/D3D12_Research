cbuffer PerObjectData : register(b0)
{
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
};

PSInput VSMain(VSInput input)
{
	PSInput result = (PSInput)0;
	result.position = mul(float4(input.position, 1.0f), WorldViewProjection);
	return result;
}