struct CS_Input
{
    uint3 ThreadID : SV_DISPATCHTHREADID;
};

StructuredBuffer<uint> tInActiveClusters : register(t0);
RWStructuredBuffer<uint> uOutActiveClusters : register(u0);

[numthreads(64, 1, 1)]
void CompactClusters(CS_Input input)
{
    uint clusterIndex = input.ThreadID.x;
    if(tInActiveClusters[clusterIndex] > 0)
    {
        uint index = uOutActiveClusters.IncrementCounter();
        uOutActiveClusters[index] = clusterIndex;
    }
}