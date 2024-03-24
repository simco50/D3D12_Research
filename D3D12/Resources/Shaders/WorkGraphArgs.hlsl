#include "Common.hlsli"
#include "HZB.hlsli"
#include "D3D12.hlsli"
#include "VisibilityBuffer.hlsli"
#include "WaveOps.hlsli"
#include "ShaderDebugRender.hlsli"

#define NUM_CULL_INSTANCES_THREADS 64

// Element index of counter for total amount of candidate meshlets.
static const int COUNTER_TOTAL_CANDIDATE_MESHLETS 	= 0;
// Element index of counter for amount of candidate meshlets in Phase 1.
static const int COUNTER_PHASE1_CANDIDATE_MESHLETS 	= 1;
// Element index of counter for amount of candidate meshlets in Phase 2.
static const int COUNTER_PHASE2_CANDIDATE_MESHLETS 	= 2;
// Element index of counter for amount of visible meshlets in Phase 1.
static const int COUNTER_PHASE1_VISIBLE_MESHLETS 	= 0;
// Element index of counter for amount of visible meshlets in Phase 2.
static const int COUNTER_PHASE2_VISIBLE_MESHLETS 	= 1;

struct Phase2Args
{
	D3D12_MULTI_NODE_GPU_INPUT Header;
	D3D12_NODE_GPU_INPUT InstanceCullInput;
	D3D12_NODE_GPU_INPUT MeshletCullInput;
	uint InstanceCullRecords;
	uint MeshletCullRecords;
};

RWStructuredBuffer<Phase2Args> uWorkGraphArguments					: register(u0);

Buffer<uint> tCounter_CandidateMeshlets 							: register(t1);	// Number of meshlets to process
Buffer<uint> tCounter_PhaseTwoInstances 							: register(t2);	// Number of instances which need to be tested in Phase 2

[numthreads(1, 1, 1)]
void PreparePhase2Args()
{
	uWorkGraphArguments[0].InstanceCullRecords = DivideAndRoundUp(tCounter_PhaseTwoInstances[0], NUM_CULL_INSTANCES_THREADS);
	uWorkGraphArguments[0].MeshletCullRecords = DivideAndRoundUp(tCounter_CandidateMeshlets[COUNTER_PHASE2_CANDIDATE_MESHLETS], NUM_CULL_INSTANCES_THREADS);
}
