#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1)), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1))")

struct ShaderParameters
{
	float4x4 ReprojectionMatrix;
	float2 InvScreenDimensions;
};

ConstantBuffer<ShaderParameters> cParameters : register(b0);

Texture2D tDepthTexture : register(t0);
RWTexture2D<float2> uVelocity  : register(u0);

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void CSMain(uint3 threadID : SV_DispatchThreadID)
{
	float2 uv = cParameters.InvScreenDimensions * (threadID.xy + 0.5f);
	float depth = tDepthTexture[threadID.xy].r;
	float4 pos = float4(uv, depth, 1);
	float4 prevPos = mul(pos, cParameters.ReprojectionMatrix);
	prevPos.xyz /= prevPos.w;
	uVelocity[threadID.xy] = (prevPos - pos).xy;
}
