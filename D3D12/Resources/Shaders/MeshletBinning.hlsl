#include "Common.hlsli"
#include "VisibilityBuffer.hlsli"
#include "WaveOps.hlsli"
#include "ShaderDebugRender.hlsli"
#include "D3D12.hlsli"

/*
	Meshlet culling outputs visible meshlets in a single unordered list.
	In order to support multiple rasterization PSOs, the list must be sorted by PSO.
	These shaders classifies meshlets by PSO bin and outputs an indirection table sorted
	by PSO, combined with a header specifying at which index each bin starts and
	how many elements it contains.

	This is done in the following passes:
		1. For each meshlet, find its associated bin and increment the counter of that bin
		2. Do a prefix sum on all the bin counters and compute the start index of each bin
		3. For each meshlet, find its associated bin and write its index into
			the indirection list at the offset computed in the previous step.
*/

static const int COUNTER_PHASE1_VISIBLE_MESHLETS = 0;
static const int COUNTER_PHASE2_VISIBLE_MESHLETS = 1;

struct BinningParameters
{
	uint NumBins;
	uint IsSecondPhase;
};
ConstantBuffer<BinningParameters> cBinningParams 				: register(b0);

RWBuffer<uint> uMeshletCounts 									: register(u0);
RWStructuredBuffer<uint4> uMeshletOffsetAndCounts 				: register(u0);
RWBuffer<uint> uGlobalMeshletCounter 							: register(u1);
RWStructuredBuffer<uint> uBinnedMeshlets 						: register(u1);
RWStructuredBuffer<D3D12_DISPATCH_ARGUMENTS> uDispatchArguments : register(u2);

StructuredBuffer<MeshletCandidate> tVisibleMeshlets 			: register(t0);
Buffer<uint> tCounter_VisibleMeshlets 							: register(t1);
Buffer<uint> tMeshletCounts 									: register(t0);

uint GetNumMeshlets()
{
	if(cBinningParams.IsSecondPhase)
		return tCounter_VisibleMeshlets[COUNTER_PHASE2_VISIBLE_MESHLETS];
	else
		return tCounter_VisibleMeshlets[COUNTER_PHASE1_VISIBLE_MESHLETS];
}

[numthreads(1, 1, 1)]
void PrepareArgsCS()
{
	for(uint i = 0; i < cBinningParams.NumBins; ++i)
		uMeshletCounts[i] = 0;
	uGlobalMeshletCounter[0] = 0;

	D3D12_DISPATCH_ARGUMENTS args;
    args.ThreadGroupCount = uint3(DivideAndRoundUp(GetNumMeshlets(), 64), 1, 1);
    uDispatchArguments[0] = args;
}

// Bin by whether the meshlet is alpha tested or not
uint GetBin(uint meshletIndex)
{
	if(cBinningParams.IsSecondPhase)
		meshletIndex += tCounter_VisibleMeshlets[COUNTER_PHASE1_VISIBLE_MESHLETS];

	MeshletCandidate candidate = tVisibleMeshlets[meshletIndex];
    InstanceData instance = GetInstance(candidate.InstanceID);
	MaterialData material = GetMaterial(instance.MaterialIndex);
	return material.RasterBin;
}

[numthreads(64, 1, 1)]
void ClassifyMeshletsCS(uint threadID : SV_DispatchThreadID)
{
	uint meshletIndex = threadID;
	if(meshletIndex >= GetNumMeshlets())
		return;

	uint bin = GetBin(meshletIndex);

	// WaveOps optimzed loop to write meshlet indices to its associated bins.
	bool finished = false;
	while(WaveActiveAnyTrue(!finished))
	{
		// Mask out all threads which are already finished
		if(!finished)
		{
			const uint firstBin = WaveReadLaneFirst(bin);
			if(firstBin == bin)
			{
				// Accumulate the meshlet count for all active threads
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
	if(bin >= cBinningParams.NumBins)
		return;

	// Compute the amount of meshlets for each bin and prefix sum to get the global index offset
	uint numMeshlets = tMeshletCounts[bin];
	uint offset = WavePrefixSum(numMeshlets);
	uint globalOffset;
	if(WaveIsFirstLane())
		InterlockedAdd(uGlobalMeshletCounter[0], numMeshlets, globalOffset);
	offset += WaveReadLaneFirst(globalOffset);
	uMeshletOffsetAndCounts[bin] = uint4(0, 1, 1, offset);

#if 0
	TextWriter writer = CreateTextWriter(float2(10, threadID * 25 + 10 + cBinningParams.IsSecondPhase * 100));
	String binText = TEXT("Bin ");
	writer.Text(binText);
	writer.Int(bin);
	String colonText = TEXT(": ");
	writer.Text(colonText);
	writer.Int(numMeshlets);
#endif
}

[numthreads(64, 1, 1)]
void WriteBinsCS(uint threadID : SV_DispatchThreadID)
{
	uint meshletIndex = threadID;
	if(meshletIndex >= GetNumMeshlets())
		return;

	uint bin = GetBin(meshletIndex);

	uint offset = uMeshletOffsetAndCounts[bin].w;
	uint meshletOffset;

	// WaveOps optimzed loop to write meshlet indices to its associated bins.

	// Loop until all meshlets have their indices written
	bool finished = false;
	while(WaveActiveAnyTrue(!finished))
	{
		// Mask out all threads which are already finished
		if(!finished)
		{
			// Get the bin of the first thread
			const uint firstBin = WaveReadLaneFirst(bin);
			if(firstBin == bin)
			{
				// All threads which have the same bin as the first active lane writes its index
				uint originalValue;
				uint count = WaveActiveCountBits(true);
				if(WaveIsFirstLane())
					InterlockedAdd(uMeshletOffsetAndCounts[firstBin].x, count, originalValue);
				meshletOffset = WaveReadLaneFirst(originalValue) + WavePrefixCountBits(true);
				finished = true;
			}
		}
	}

	if(cBinningParams.IsSecondPhase)
		meshletIndex += tCounter_VisibleMeshlets[COUNTER_PHASE1_VISIBLE_MESHLETS];

	uBinnedMeshlets[offset + meshletOffset] = meshletIndex;
}
