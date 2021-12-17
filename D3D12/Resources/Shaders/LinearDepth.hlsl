#include "CommonBindings.hlsli"

#define RootSig ROOT_SIG("CBV(b0), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1)), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1))")

#define BLOCK_SIZE 16

struct PassParameters
{
	float Near;
	float Far;
};

ConstantBuffer<PassParameters> cPassData : register(b0);
RWTexture2D<float> uOutput : register(u0);
Texture2D<float> tInput : register(t0);

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(uint3 threadId : SV_DispatchThreadID)
{
	uint width, height;
	uOutput.GetDimensions(width, height);
	if(threadId.x < width && threadId.y < height)
	{
		uOutput[threadId.xy] = LinearizeDepth01(tInput[threadId.xy], cPassData.Near, cPassData.Far);
	}
}
