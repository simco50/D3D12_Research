cbuffer PerObjectData : register(b0)
{
	float4x4 WorldViewProjection;
}

#ifdef ALPHA_BLEND
Texture2D tAlphaTexture : register(t0);
SamplerState sAlphaSampler : register(s0);
#endif

struct VSInput
{
	float3 position : POSITION;
#ifdef ALPHA_BLEND
	float2 texCoord : TEXCOORD;
#endif
};

struct PSInput
{
	float4 position : SV_POSITION;
#ifdef ALPHA_BLEND
	float2 texCoord : TEXCOORD;
#endif
};

PSInput VSMain(VSInput input)
{
	PSInput result = (PSInput)0;
	result.position = mul(float4(input.position, 1.0f), WorldViewProjection);
#ifdef ALPHA_BLEND
	result.texCoord = input.texCoord;
#endif
	return result;
}

#ifdef ALPHA_BLEND
void PSMain(PSInput input)
{
	if(tAlphaTexture.Sample(sAlphaSampler, input.texCoord).a == 0.0f)
	{
		discard;
	}
}
#endif