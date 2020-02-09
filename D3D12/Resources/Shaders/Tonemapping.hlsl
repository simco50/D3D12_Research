#define RootSig "DescriptorTable(SRV(t0, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), " \
				"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, visibility = SHADER_VISIBILITY_PIXEL), " \

struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
};

Texture2D tColor : register(t0);
SamplerState sColorSampler : register(s0);

[RootSignature(RootSig)]
PSInput VSMain(uint index : SV_VERTEXID)
{
	PSInput output;
	output.position.x = (float)(index / 2) * 4.0f - 1.0f;
	output.position.y = (float)(index % 2) * 4.0f - 1.0f;
	output.position.z = 0.0f;
	output.position.w = 1.0f;

	output.texCoord.x = (float)(index / 2) * 2.0f;
	output.texCoord.y = 1.0f - (float)(index % 2) * 2.0f;

	return output;
}

float3 LinearTosRGB(in float3 color)
{
	return color;
    float3 x = color * 12.92f;
    float3 y = 1.055f * pow(saturate(color), 1.0f / 2.4f) - 0.055f;

    float3 clr = color;
    clr.r = color.r < 0.0031308f ? x.r : y.r;
    clr.g = color.g < 0.0031308f ? x.g : y.g;
    clr.b = color.b < 0.0031308f ? x.b : y.b;

    return clr;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float3 color = LinearTosRGB(tColor.Sample(sColorSampler, input.texCoord).rgb);
		
	return float4(color, 1);
}