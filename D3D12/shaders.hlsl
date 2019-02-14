cbuffer Data : register(b0)
{
	float4x4 WorldViewProjection;
}

struct VSInput
{
	float3 position : POSITION;
};

struct PSInput
{
	float4 position : SV_POSITION;
};

PSInput VSMain(VSInput input)
{
	PSInput result;
	
	result.position = mul(float4(input.position, 1.0f), WorldViewProjection);

	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(1,0,1,1);
}