#include "Common.hlsli"
#include "VisibilityBuffer.hlsli"
#include "WaveOps.hlsli"
#include "ShaderDebugRender.hlsli"
#include "D3D12.hlsli"

struct AllocateParameters
{
	uint NumBins;
};
ConstantBuffer<AllocateParameters> cAllocateParams : register(b0);

RWBuffer<uint> uMeshletCounts : register(u0);
RWStructuredBuffer<uint4> uMeshletOffsetAndCounts : register(u0);
RWBuffer<uint> uGlobalMeshletCounter : register(u1);
RWStructuredBuffer<uint> uBinnedMeshlets : register(u1);
RWStructuredBuffer<D3D12_DISPATCH_ARGUMENTS> uDispatchArguments : register(u2);

StructuredBuffer<MeshletCandidate> tVisibleMeshlets : register(t0);
Buffer<uint> tCounter_VisibleMeshlets : register(t1);
Buffer<uint> tMeshletCounts : register(t0);

#define COUNTER_PHASE1_VISIBLE_MESHLETS 0
#define COUNTER_PHASE2_VISIBLE_MESHLETS 3
#if OCCLUSION_FIRST_PASS
static const int VisibleMeshletCounter = COUNTER_PHASE1_VISIBLE_MESHLETS;
#else
static const int VisibleMeshletCounter = COUNTER_PHASE2_VISIBLE_MESHLETS;
#endif

[numthreads(1, 1, 1)]
void PrepareArgsCS()
{
	for(uint i = 0; i < cAllocateParams.NumBins; ++i)
	{
		uMeshletCounts[i] = 0;
	}
	uGlobalMeshletCounter[0] = 0;

	D3D12_DISPATCH_ARGUMENTS args;
    args.ThreadGroupCount = uint3(DivideAndRoundUp(tCounter_VisibleMeshlets[VisibleMeshletCounter], 64), 1, 1);
    uDispatchArguments[0] = args;
}

uint GetBin(uint meshletIndex)
{
#if !OCCLUSION_FIRST_PASS
	meshletIndex += tCounter_VisibleMeshlets[COUNTER_PHASE1_VISIBLE_MESHLETS];
#endif
	MeshletCandidate candidate = tVisibleMeshlets[meshletIndex];
    InstanceData instance = GetInstance(candidate.InstanceID);
	MaterialData material = GetMaterial(instance.MaterialIndex);
	return material.AlphaCutoff < 1.0f;
}

[numthreads(64, 1, 1)]
void ClassifyMeshletsCS(uint threadID : SV_DispatchThreadID)
{
	if(threadID >= tCounter_VisibleMeshlets[VisibleMeshletCounter])
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
	uMeshletOffsetAndCounts[bin] = uint4(0, 1, 1, offset);

	TextWriter writer = CreateTextWriter(float2(10, threadID * 25 + 10));
	writer.Int(numMeshlets);
}

[numthreads(64, 1, 1)]
void WriteBinsCS(uint threadID : SV_DispatchThreadID)
{
	uint meshletIndex = threadID;
	if(meshletIndex >= tCounter_VisibleMeshlets[VisibleMeshletCounter])
		return;

	uint binIndex = GetBin(meshletIndex);

	uint offset = uMeshletOffsetAndCounts[binIndex].w;
	uint meshletOffset;
	InterlockedAdd(uMeshletOffsetAndCounts[binIndex].x, 1, meshletOffset);
	uBinnedMeshlets[offset + meshletOffset] = meshletIndex;
}
