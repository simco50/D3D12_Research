#define RootSig ROOT_SIG("CBV(b0), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1)), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1))")

#define BLOCK_SIZE 8

struct Parameters
{
	uint2 TargetDimensions;
	float2 TargetDimensionsInv;
	float EdgeThreshold;
};

ConstantBuffer<Parameters> cParameters : register(b0);
RWTexture2D<float> uOutput : register(u0);
Texture2D tInput : register(t0);

[RootSignature(RootSig)]
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CSMain(uint3 DispatchThreadId : SV_DispatchThreadID)
{
	if(DispatchThreadId.x < cParameters.TargetDimensions.x && DispatchThreadId.y < cParameters.TargetDimensions.y)
	{
		const int2 offsets[] = {
			int2(-1, -1), int2(0, -1), int2(1, -1),
			int2(-1, 0), int2(0, 0), int2(1, 0),
			int2(-1, 1), int2(0, 1), int2(1, 1),
		};
		float luminance[8];
		for(int i = 0; i < 8; ++i)
		{
			float3 color = tInput.Load(int3(DispatchThreadId.xy + offsets[i], 0));
			luminance[i] = dot(color, float3(0.2125f, 0.7154f, 0.0721f));
		}
		float x = luminance[0] + 2 * luminance[3] + luminance[5] - luminance[2] - 2 * luminance[4] - luminance[7];
		float y = luminance[0] + 2 * luminance[1] + luminance[2] - luminance[5] - 2 * luminance[6] - luminance[7];
		float edge = sqrt(x*x + y*y);
		uOutput[DispatchThreadId.xy] = edge;
	}
}
