Texture2D Buffer0 : register(t0);
RWTexture2D<float4> BufferOut : register(u0);

[numthreads(2, 2, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID )
{
    BufferOut[DTid.xy] = Buffer0[DTid.xy * 8];
}