#include "Common.hlsli"
#include "HZB.hlsli"
#include "VisibilityBuffer.hlsli"
#include "WaveOps.hlsli"
#include "ShaderDebugRender.hlsli"


/*
	-- 2 Phase Occlusion Culling --

	Works under the assumption that it's likely that objects visible in the previous frame, will be visible this frame.

	In Phase 1, we render all objects that were visible last frame by testing against the previous HZB.
	Occluded objects are stored in a list, to be processed later.
	The HZB is constructed from the current result.
	Phase 2 tests all previously occluded objects against the new HZB and renders unoccluded.
	The HZB is constructed again from this result to be used in the next frame.

	Cull both on a per-instance level as on a per-meshlet level.
	Leverage Mesh/Amplification shaders to drive per-meshlet culling.

	https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf
*/

// Debug draw bounding box around occluded instances
#define VISUALIZE_OCCLUDED 0

#ifndef OCCLUSION_FIRST_PASS
#define OCCLUSION_FIRST_PASS 1
#endif

// If enabled, perform 2 phase occlusion culling
// If disabled, no occlusion culling and only 1 pass.
#ifndef OCCLUSION_CULL
#define OCCLUSION_CULL 1
#endif

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

#if OCCLUSION_FIRST_PASS
static const int VisibleMeshletCounter = COUNTER_PHASE1_VISIBLE_MESHLETS;	// Index of counter for visible meshlets in current phase.
#else
static const int VisibleMeshletCounter = COUNTER_PHASE2_VISIBLE_MESHLETS;	// Index of counter for visible meshlets in current phase.
#endif

struct ClearParams
{
	RWStructuredBufferH<uint4> MeshletOffsetAndCounts;
};
DEFINE_CONSTANTS(ClearParams, 0);

struct CullParams
{
	uint2 HZBDimensions;
	RWStructuredBufferH<MeshletCandidate> CandidateMeshlets;	// List of meshlets to process
	RWStructuredBufferH<uint> Counter_CandidateMeshlets;		// Number of meshlets to process
	RWStructuredBufferH<uint> PhaseTwoInstances;				// List of instances which need to be tested in Phase 2
	RWStructuredBufferH<uint> Counter_PhaseTwoInstances;		// Number of instances which need to be tested in Phase 2
	RWStructuredBufferH<MeshletCandidate> VisibleMeshlets;		// List of meshlets to rasterize
	RWStructuredBufferH<uint> Counter_VisibleMeshlets;			// Number of meshlets to rasterize
	RWStructuredBufferH<uint4> MeshletOffsetAndCounts;
	RWStructuredBufferH<uint> BinnedMeshlets;
	Texture2DH<float> HZB;										// Current HZB texture
};
DEFINE_CONSTANTS(CullParams, 0);

struct EntryRecord
{
	uint GridSize : SV_DispatchGrid;
};

struct MeshletCullData
{
	uint InstanceID;
	uint GridSize : SV_DispatchGrid;
};

struct VisibleMeshlet
{
	MeshletCandidate Candidate;
	uint VisibleIndex;
};

/*
	Per-instance culling
*/
[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(MAX_NUM_INSTANCES / NUM_CULL_INSTANCES_THREADS, 1, 1)]
[NodeIsProgramEntry]
[numthreads(NUM_CULL_INSTANCES_THREADS, 1, 1)]
void CullInstancesCS(
	DispatchNodeInputRecord<EntryRecord> input,
	[MaxRecords(NUM_CULL_INSTANCES_THREADS)] NodeOutput<MeshletCullData> CullMeshletsCS,
	uint threadID : SV_DispatchThreadID
	)
{
#if OCCLUSION_FIRST_PASS
	uint numInstances = cView.NumInstances;
#else
	uint numInstances = cCullParams.Counter_PhaseTwoInstances[0];
#endif

	if(threadID >= numInstances)
        return;

#if OCCLUSION_FIRST_PASS
	uint instanceIndex = threadID;
#else
	uint instanceIndex = cCullParams.PhaseTwoInstances[threadID];
#endif

	InstanceData instance = GetInstance(instanceIndex);

#if OCCLUSION_FIRST_PASS
	MaterialData material = GetMaterial(instance.MaterialIndex);
	if(material.RasterBin == 0xFFFFFFFF)
		return;
#endif

    MeshData mesh = GetMesh(instance.MeshIndex);

	// Frustum test instance against the current view
	FrustumCullData cullData = FrustumCull(instance.LocalBoundsOrigin, instance.LocalBoundsExtents, instance.LocalToWorld, cView.WorldToClip);
	bool isVisible = cullData.IsVisible;
	bool wasOccluded = false;

	#if OCCLUSION_CULL
	if(isVisible)
	{
#if OCCLUSION_FIRST_PASS
		// Frustum test instance against the *previous* view to determine if it was visible last frame
		FrustumCullData prevCullData = FrustumCull(instance.LocalBoundsOrigin, instance.LocalBoundsExtents, instance.LocalToWorldPrev, cView.WorldToClipPrev);
		if (prevCullData.IsVisible)
		{
			// Occlusion test instance against the HZB
			wasOccluded = !HZBCull(prevCullData, cCullParams.HZB.Get(), cCullParams.HZBDimensions);
		}

		// If the instance was occluded the previous frame, we can't be sure it's still occluded this frame.
		// Add it to the list to re-test in the second phase.
		if(wasOccluded)
		{
			uint elementOffset = 0;
			InterlockedAdd_WaveOps(cCullParams.Counter_PhaseTwoInstances.Get(), 0, 1, elementOffset);
			if(elementOffset < MAX_NUM_INSTANCES)
				cCullParams.PhaseTwoInstances.Store(elementOffset, instance.ID);
		}
#else
		// Occlusion test instance against the updated HZB
		isVisible = HZBCull(cullData, cCullParams.HZB.Get(), cCullParams.HZBDimensions);
#endif
	}
#endif

	// If instance is visible and wasn't occluded in the previous frame, submit it
	uint num_records = isVisible && !wasOccluded ? 1 : 0;
	ThreadNodeOutputRecords<MeshletCullData> outputs = CullMeshletsCS.GetThreadNodeOutputRecords(num_records);
	if(num_records)
	{
		outputs.Get().InstanceID = instanceIndex;
		outputs.Get().GridSize = DivideAndRoundUp(mesh.MeshletCount, NUM_CULL_INSTANCES_THREADS);
	}
	outputs.OutputComplete();

#if VISUALIZE_OCCLUDED
	if(wasOccluded)
	{
		DrawOBB(instance.LocalBoundsOrigin, instance.LocalBoundsExtents, instance.LocalToWorld, Colors::Green);
	}
#endif
}


/*
	Per-meshlet culling
*/
void MeshletCull(MeshletCandidate candidate, NodeOutputArray<VisibleMeshlet> meshOutputNodes)
{
	InstanceData instance = GetInstance(candidate.InstanceID);
	MeshData mesh = GetMesh(instance.MeshIndex);

	if(candidate.MeshletIndex >= mesh.MeshletCount)
		return;

	// Frustum test meshlet against the current view
	Meshlet::Bounds bounds = mesh.DataBuffer.LoadStructure<Meshlet::Bounds>(candidate.MeshletIndex, mesh.MeshletBoundsOffset);

	FrustumCullData cullData = FrustumCull(bounds.LocalCenter, bounds.LocalExtents, instance.LocalToWorld, cView.WorldToClip);
	bool isVisible = cullData.IsVisible;
	bool wasOccluded = false;

#if OCCLUSION_CULL
	if(isVisible)
	{
#if OCCLUSION_FIRST_PASS
		// Frustum test meshlet against the *previous* view to determine if it was visible last frame
		FrustumCullData prevCullData = FrustumCull(bounds.LocalCenter, bounds.LocalExtents, instance.LocalToWorldPrev, cView.WorldToClipPrev);
		if(prevCullData.IsVisible)
		{
			// Occlusion test meshlet against the HZB
			wasOccluded = !HZBCull(prevCullData, cCullParams.HZB.Get(), cCullParams.HZBDimensions);
		}

		// If the meshlet was occluded the previous frame, we can't be sure it's still occluded this frame.
		// Add it to the list to re-test in the second phase.
		if(wasOccluded)
		{
			// Limit how many meshlets we're writing based on the buffer size
			uint globalMeshletIndex;
			InterlockedAdd_WaveOps(cCullParams.Counter_CandidateMeshlets.Get(), COUNTER_TOTAL_CANDIDATE_MESHLETS, 1, globalMeshletIndex);
			if(globalMeshletIndex < MAX_NUM_MESHLETS)
			{
				uint elementOffset;
				InterlockedAdd_WaveOps(cCullParams.Counter_CandidateMeshlets.Get(), COUNTER_PHASE2_CANDIDATE_MESHLETS, 1, elementOffset);
				cCullParams.CandidateMeshlets.Store(elementOffset, candidate);
			}
		}

		isVisible = !wasOccluded;
#else
		// Occlusion test meshlet against the updated HZB
		isVisible = HZBCull(cullData, cCullParams.HZB.Get(), cCullParams.HZBDimensions);
#endif
	}
#endif

	// If meshlet is visible and wasn't occluded in the previous frame, submit it
	MaterialData material = GetMaterial(instance.MaterialIndex);
	ThreadNodeOutputRecords<VisibleMeshlet> meshShaderRecord = meshOutputNodes[material.RasterBin].GetThreadNodeOutputRecords(isVisible ? 1 : 0);
	if(isVisible)
	{
		uint visibleIndex;
		InterlockedAdd_WaveOps(cCullParams.Counter_VisibleMeshlets.Get(), 0, 1, visibleIndex);
		cCullParams.VisibleMeshlets.Store(visibleIndex, candidate);

		meshShaderRecord.Get().Candidate = candidate;
		meshShaderRecord.Get().VisibleIndex = visibleIndex;
	}
	meshShaderRecord.OutputComplete();
}


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(128, 1, 1)]
[numthreads(NUM_CULL_MESHLETS_THREADS, 1, 1)]
void CullMeshletsCS(
	DispatchNodeInputRecord<MeshletCullData> meshletRecords,
	[MaxRecords(NUM_CULL_MESHLETS_THREADS)][NodeArraySize(NUM_RASTER_BINS)] NodeOutputArray<VisibleMeshlet> MeshNodes,
	uint threadIndex : SV_DispatchThreadID)
{
	MeshletCandidate candidate;
	candidate.InstanceID = meshletRecords.Get().InstanceID;
	candidate.MeshletIndex = threadIndex;
	MeshletCull(candidate, MeshNodes);
}

#if !OCCLUSION_FIRST_PASS

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(128, 1, 1)]
[numthreads(NUM_CULL_MESHLETS_THREADS, 1, 1)]
void CullMeshletsEntryCS(
	DispatchNodeInputRecord<EntryRecord> input,
	[MaxRecords(NUM_CULL_MESHLETS_THREADS)][NodeArraySize(NUM_RASTER_BINS)] NodeOutputArray<VisibleMeshlet> MeshNodes,
	uint threadIndex : SV_DispatchThreadID)
{
	uint numMeshlets = cCullParams.Counter_CandidateMeshlets[COUNTER_PHASE2_CANDIDATE_MESHLETS];
	if(threadIndex >= numMeshlets)
		return;

	MeshletCandidate candidate = cCullParams.CandidateMeshlets[threadIndex];
	MeshletCull(candidate, MeshNodes);
}

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[NodeIsProgramEntry]
[numthreads(1, 1, 1)]
void KickPhase2NodesCS(
	[MaxRecords(1)] NodeOutput<EntryRecord> CullMeshletsEntryCS,
	[MaxRecords(1)] NodeOutput<EntryRecord> CullInstancesCS)
{
	// Kick per-instance culling
	{
		uint num_instances = cCullParams.Counter_PhaseTwoInstances[0];
		ThreadNodeOutputRecords<EntryRecord> record = CullInstancesCS.GetThreadNodeOutputRecords(num_instances > 0 ? 1 : 0);
		if(num_instances > 0)
			record.Get().GridSize = DivideAndRoundUp(num_instances, NUM_CULL_INSTANCES_THREADS);
		record.OutputComplete();
	}

	// Kick per-meshlet culling
	{
		uint num_meshlets = cCullParams.Counter_CandidateMeshlets[COUNTER_PHASE2_CANDIDATE_MESHLETS];
		ThreadNodeOutputRecords<EntryRecord> record = CullMeshletsEntryCS.GetThreadNodeOutputRecords(num_meshlets > 0 ? 1 : 0);
		if(num_meshlets > 0)
			record.Get().GridSize = DivideAndRoundUp(num_meshlets, NUM_CULL_MESHLETS_THREADS);
		record.OutputComplete();
	}
}

#endif

#ifdef SHADER_COMPUTE

[shader("compute")]
[numthreads(1, 1, 1)]
void ClearRasterBins()
{
	for(int i = 0; i < NUM_RASTER_BINS; ++i)
	{
		// Hack, hardcoded offset
		uint binOffset = (MAX_NUM_MESHLETS / NUM_RASTER_BINS) * i;
		cClearParams.MeshletOffsetAndCounts.Store(i, uint4(0, 1, 1, binOffset));
	}
}

#endif

// Obviously this is very stupid, but since there are no mesh nodes yet,
// do this just to hook it up to the existing mesh shaders as a test
void RenderMeshlet(VisibleMeshlet meshlet, uint pipelineBin)
{
	uint binOffset = cCullParams.MeshletOffsetAndCounts[pipelineBin].w;

	uint binnedMeshletIndex;
	InterlockedAdd(cCullParams.MeshletOffsetAndCounts.Get()[pipelineBin].x, 1, binnedMeshletIndex);
	cCullParams.BinnedMeshlets.Store(binOffset + binnedMeshletIndex, meshlet.VisibleIndex);
}


[Shader("node")]
[NodeID("MeshNodes", 0)]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[numthreads(1, 1, 1)]
void ShadeMeshOpaque(DispatchNodeInputRecord<VisibleMeshlet> inputData)
{
	RenderMeshlet(inputData.Get(), 0);
}


[Shader("node")]
[NodeID("MeshNodes", 1)]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1, 1, 1)]
[numthreads(1, 1, 1)]
void ShadeMeshAlphaMask(DispatchNodeInputRecord<VisibleMeshlet> inputData)
{
	RenderMeshlet(inputData.Get(), 1);
}
