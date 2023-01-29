#include "Common.hlsli"
#include "VisibilityBuffer.hlsli"
#include "WaveOps.hlsli"
#include "ShaderDebugRender.hlsli"

struct AllocateParameters
{
	uint NumBins;
};
ConstantBuffer<AllocateParameters> cAllocateParams : register(b0);

RWBuffer<uint> uMeshletCounts : register(u0);
RWStructuredBuffer<uint2> uMeshletOffsetAndCounts : register(u0);
RWBuffer<uint> uGlobalMeshletCounter : register(u1);
RWStructuredBuffer<uint> uBinnedMeshlets : register(u1);

StructuredBuffer<MeshletCandidate> tVisibleMeshlets : register(t0);
Buffer<uint> tCounter_VisibleMeshlets : register(t1);
Buffer<uint> tMeshletCounts : register(t0);

uint GetBin(uint meshletIndex)
{
	MeshletCandidate candidate = tVisibleMeshlets[meshletIndex];
    InstanceData instance = GetInstance(candidate.InstanceID);
	MaterialData material = GetMaterial(instance.MaterialIndex);
	return material.AlphaCutoff < 1.0f;
}

[numthreads(64, 1, 1)]
void ClassifyMeshletsCS(uint threadID : SV_DispatchThreadID)
{
	if(threadID >= tCounter_VisibleMeshlets[1])
		return;

	uint bin = GetBin(threadID);

	bool finished = false;
	while(WaveActiveAnyTrue(!finished))
	{
		if(!finished)
		{
			const uint firstBin = WaveReadLaneFirst(bin);
			if(firstBin == bin)
			{
				uint originalValue;
				InterlockedAdd_WaveOps(uMeshletCounts, firstBin, 1, originalValue);
				finished = true;
			}
		}
	}
}

[numthreads(64, 1, 1)]
void AllocateBinRangesCS(uint threadID : SV_DispatchThreadID)
{
	uint bin = threadID;
	if(bin >= cAllocateParams.NumBins)
		return;
	uint numMeshlets = tMeshletCounts[bin];
	uint offset;
	InterlockedAdd(uGlobalMeshletCounter[0], numMeshlets, offset);
	uMeshletOffsetAndCounts[bin] = uint2(offset, 0);

	TextWriter writer = CreateTextWriter(float2(10, threadID * 25 + 10));
	writer.Int(numMeshlets);
}

[numthreads(64, 1, 1)]
void WriteBinsCS(uint threadID : SV_DispatchThreadID)
{
	uint meshletIndex = threadID;
	if(meshletIndex >= tCounter_VisibleMeshlets[1])
		return;

	uint binIndex = GetBin(meshletIndex);

	uint offset = uMeshletOffsetAndCounts[binIndex].x;
	uint meshletOffset;
	InterlockedAdd(uMeshletOffsetAndCounts[binIndex].y, 1, meshletOffset);
	uBinnedMeshlets[offset + meshletOffset] = meshletIndex;
}
