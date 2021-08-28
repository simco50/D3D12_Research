#include "CBT.hlsli"

#define RootSig "CBV(b0), " \
                "CBV(b1), " \
				"DescriptorTable(UAV(u0, numDescriptors = 2)), " \
				"DescriptorTable(SRV(t0, numDescriptors = 1))"

RWByteAddressBuffer uCBT : register(u0);
RWByteAddressBuffer uIndirectArgs : register(u1);

struct CommonArgs
{
    uint NumElements;
};

struct SumReductionData
{
    uint Depth;
};

ConstantBuffer<CommonArgs> cCommonArgs : register(b0);
ConstantBuffer<SumReductionData> cSumReductionData : register(b1);

[numthreads(1, 1, 1)]
void PrepareDispatchArgsCS(uint threadID : SV_DispatchThreadID)
{
    CBT cbt;
    cbt.Init(uCBT, cCommonArgs.NumElements);
    uIndirectArgs.Store3(0, uint3(cbt.NumNodes(), 1, 1));
}

[RootSignature(RootSig)]
[numthreads(1, 1, 1)]
void SumReductionCS(uint threadID : SV_DispatchThreadID)
{
    CBT cbt;
    cbt.Init(uCBT, cCommonArgs.NumElements);
    uint count = 1u << cSumReductionData.Depth;
    uint index = threadID.x;
    if(index < count)
    {
        index += count;
        uint leftChild = cbt.GetData(cbt.LeftChildIndex(index));
        uint rightChild = cbt.GetData(cbt.RightChildIndex(index));
        cbt.SetData(index, leftChild + rightChild);
    }
}

[numthreads(16, 1, 1)]
void UpdateCS(uint threadID : SV_DispatchThreadID)
{
    
}