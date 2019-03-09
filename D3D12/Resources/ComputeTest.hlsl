cbuffer RootData : register(b0)
{
    float2 TexelSize;
}

Texture2D Buffer0 : register(t0);
RWTexture2D<float4> BufferOut : register(u0);
SamplerState Sampler : register(s0);

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID )
{
    float2 texCoord = TexelSize * (DTid.xy + 0.5f);
    float4 color = Buffer0.SampleLevel(Sampler, texCoord, 0);
    BufferOut[DTid.xy] = color;
}