#define RootSig "DescriptorTable(SRV(t0, numDescriptors = 1)), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1)) "

struct CS_Input
{
    uint3 ThreadID : SV_DISPATCHTHREADID;
};

StructuredBuffer<uint> tInActiveClusters : register(t0);
RWStructuredBuffer<uint> uOutCompactedClusters : register(u0);

[RootSignature(RootSig)]
[numthreads(64, 1, 1)]
void CompactClusters(CS_Input input)
{
    uint clusterIndex = input.ThreadID.x;
    if(tInActiveClusters[clusterIndex] > 0)
    {
        uint index = uOutCompactedClusters.IncrementCounter();
        uOutCompactedClusters[index] = clusterIndex;
    }
}