struct PSInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD;
};

Texture2D tCurrentTexture : register(t0);
SamplerState sPointSampler : register(s0);

Texture2D tHistoryTexture : register(t1);
SamplerState sLinearSampler : register(s1);

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

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 current = tCurrentTexture.Sample(sPointSampler, input.texCoord);
	float4 history = tHistoryTexture.Sample(sLinearSampler, input.texCoord);
	
	return lerp(history, current, 0.05f);
}