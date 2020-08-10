#include "Common.hlsli"

#define RootSig "CBV(b0, visibility=SHADER_VISIBILITY_ALL), " \
				"DescriptorTable(SRV(t0, numDescriptors = 3)), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1))"

#define BLOCK_SIZE 16

Texture2D<float4> tInput : register(t0);
Texture2D<float> tDepth : register(t1);
RWTexture2D<float4> uOutput : register(u0);

cbuffer Data : register(b0)
{
    float4x4 cProjectionInverse;
    int4 cClusterDimensions;
    int2 cClusterSize;
    float cSliceMagicA;
	float cSliceMagicB;
    float cNear;
    float cFar;
}

#if TILED_FORWARD
Texture2D<uint2> tLightGrid : register(t2);
#elif CLUSTERED_FORWARD
StructuredBuffer<uint2> tLightGrid : register(t2);
#endif

static float4 DEBUG_COLORS[] = {
	float4(0,4,141, 255) / 255,
	float4(5,10,255, 255) / 255,
	float4(0,164,255, 255) / 255,
	float4(0,255,189, 255) / 255,
	float4(0,255,41, 255) / 255,
	float4(117,254,1, 255) / 255,
	float4(255,239,0, 255) / 255,
	float4(255,86,0, 255) / 255,
	float4(204,3,0, 255) / 255,
	float4(65,0,1, 255) / 255,
};

float EdgeDetection(uint2 index, uint width, uint height)
{
    float reference = LinearizeDepth(tDepth.Load(uint3(index, 0)), cNear, cFar);
    uint2 offsets[8] = {
        uint2(-1, -1),
        uint2(-1, 0),
        uint2(-1, 1),
        uint2(0, -1),
        uint2(0, 1),
        uint2(1, -1),
        uint2(1, 0),
        uint2(1, 1)
    };
    float sampledValue = 0;
    for(int j = 0; j < 8; j++) 
    {
        sampledValue += LinearizeDepth(tDepth.Load(uint3(index + offsets[j], 0)), cNear, cFar);
    }
    sampledValue /= 8;
    return lerp(1, 0, step(0.007f, length(reference - sampledValue)));
}

[RootSignature(RootSig)]
[numthreads(16, 16, 1)]
void DebugLightDensityCS(uint3 threadId : SV_DispatchThreadId)
{
    uint width, height;
    tInput.GetDimensions(width, height);
    if(threadId.x < width && threadId.y < height)
    {
#if TILED_FORWARD
        uint2 tileIndex = uint2(floor(threadId.xy / BLOCK_SIZE));
        uint lightCount = tLightGrid[tileIndex].y;
#elif CLUSTERED_FORWARD
        float depth = tDepth.Load(uint3(threadId.xy, 0));
        float3 viewPos = ScreenToView(float4(0, 0, depth, 1), float2(1, 1), cProjectionInverse).xyz;
        uint slice = floor(log(viewPos.z) * cSliceMagicA - cSliceMagicB);
        uint3 clusterIndex3D = uint3(floor(threadId.xy / cClusterSize), slice);
        uint clusterIndex1D = clusterIndex3D.x + (cClusterDimensions.x * (clusterIndex3D.y + cClusterDimensions.y * clusterIndex3D.z));
        uint lightCount = tLightGrid[clusterIndex1D].y;
#endif
        uOutput[threadId.xy] = EdgeDetection(threadId.xy, width, height) * DEBUG_COLORS[min(9, lightCount)];
    }
}