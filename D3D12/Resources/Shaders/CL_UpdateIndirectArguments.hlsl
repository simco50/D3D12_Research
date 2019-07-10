ByteAddressBuffer tInActiveClustersCounter : register(t0);
RWByteAddressBuffer uOutArguments : register(u0);

[numthreads(1, 1, 1)]
void UpdateIndirectArguments()
{
    uint clusterCount = tInActiveClustersCounter.Load(0);
    uOutArguments.Store3(0, uint3(clusterCount, 1, 1));
}