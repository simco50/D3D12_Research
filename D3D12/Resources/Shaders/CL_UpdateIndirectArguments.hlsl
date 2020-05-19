#define RootSig "DescriptorTable(SRV(t0, numDescriptors = 1)), " \
				"DescriptorTable(UAV(u0, numDescriptors = 1)) "

ByteAddressBuffer tInActiveClustersCounter : register(t0);
RWByteAddressBuffer uOutArguments : register(u0);

[RootSignature(RootSig)]
[numthreads(1, 1, 1)]
void UpdateIndirectArguments()
{
    uint clusterCount = tInActiveClustersCounter.Load(0);
    uOutArguments.Store3(0, uint3(clusterCount, 1, 1));
}