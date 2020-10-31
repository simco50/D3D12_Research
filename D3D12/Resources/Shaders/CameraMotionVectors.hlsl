#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1), visibility=SHADER_VISIBILITY_ALL), " \

struct ShaderParameters
{
    float4x4 ReprojectionMatrix;
};

ConstantBuffer<ShaderParameters> cParameters : register(b0);

Texture2D tDepthTexture : register(t0);
RWTexture2D<float2> uVelocity  : register(u0);

struct CS_INPUT
{
    uint3 DispatchThreadId : SV_DISPATCHTHREADID;
};

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void CSMain(CS_INPUT input)
{
    float4 pos = float4(input.DispatchThreadId.xy + 0.5f, 0, 1);
    pos.z = tDepthTexture.Load(int3(input.DispatchThreadId.xy, 0)).r;
    float4 posPrev = mul(pos, cParameters.ReprojectionMatrix);
    uVelocity[input.DispatchThreadId.xy] = (pos - posPrev).xy;
}