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

#define NUM_MESHLET_THREADS 32

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

#ifndef ALPHA_MASK
#define ALPHA_MASK 0
#endif

#ifndef DEPTH_ONLY
#define DEPTH_ONLY 0
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

struct CullParams
{
	uint2 HZBDimensions;
};

ConstantBuffer<CullParams> cCullParams						: register(b0);

RWStructuredBuffer<MeshletCandidate> uCandidateMeshlets 	: register(u0);	// List of meshlets to process
RWStructuredBuffer<uint> uCounter_CandidateMeshlets 		: register(u1);	// Number of meshlets to process
RWStructuredBuffer<uint> uPhaseTwoInstances 				: register(u2);	// List of instances which need to be tested in Phase 2
RWStructuredBuffer<uint> uCounter_PhaseTwoInstances 		: register(u3);	// Number of instances which need to be tested in Phase 2
RWStructuredBuffer<MeshletCandidate> uVisibleMeshlets 		: register(u4);	// List of meshlets to rasterize
RWStructuredBuffer<uint> uCounter_VisibleMeshlets 			: register(u5);	// Number of meshlets to rasterize

Texture2D<float> tHZB 										: register(t0);	// Current HZB texture

struct EntryRecord
{
	uint GridSize : SV_DispatchGrid;
};

struct MeshletCullData
{
	uint InstanceID;
	uint GridSize : SV_DispatchGrid;
};

struct VisibleMeshletIndex
{
	uint Index;
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
	uint numInstances = uCounter_PhaseTwoInstances[0];
#endif

	if(threadID >= numInstances)
        return;

#if OCCLUSION_FIRST_PASS
	uint instanceIndex = threadID;
#else
	uint instanceIndex = uPhaseTwoInstances[threadID];
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
			wasOccluded = !HZBCull(prevCullData, tHZB, cCullParams.HZBDimensions);
		}

		// If the instance was occluded the previous frame, we can't be sure it's still occluded this frame.
		// Add it to the list to re-test in the second phase.
		if(wasOccluded)
		{
			uint elementOffset = 0;
			InterlockedAdd_WaveOps(uCounter_PhaseTwoInstances, 0, 1, elementOffset);
			if(elementOffset < MAX_NUM_INSTANCES)
				uPhaseTwoInstances[elementOffset] = instance.ID;
		}
#else
		// Occlusion test instance against the updated HZB
		isVisible = HZBCull(cullData, tHZB, cCullParams.HZBDimensions);
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
void MeshletCull(MeshletCandidate candidate, NodeOutputArray<VisibleMeshletIndex> meshOutputNodes)
{
	InstanceData instance = GetInstance(candidate.InstanceID);
	MeshData mesh = GetMesh(instance.MeshIndex);

	if(candidate.MeshletIndex >= mesh.MeshletCount)
		return;

	// Frustum test meshlet against the current view
	Meshlet::Bounds bounds = ByteBufferLoad<Meshlet::Bounds>(mesh.BufferIndex, candidate.MeshletIndex, mesh.MeshletBoundsOffset);

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
			wasOccluded = !HZBCull(prevCullData, tHZB, cCullParams.HZBDimensions);
		}

		// If the meshlet was occluded the previous frame, we can't be sure it's still occluded this frame.
		// Add it to the list to re-test in the second phase.
		if(wasOccluded)
		{
			// Limit how many meshlets we're writing based on the buffer size
			uint globalMeshletIndex;
			InterlockedAdd_WaveOps(uCounter_CandidateMeshlets, COUNTER_TOTAL_CANDIDATE_MESHLETS, 1, globalMeshletIndex);
			if(globalMeshletIndex < MAX_NUM_MESHLETS)
			{
				uint elementOffset;
				InterlockedAdd_WaveOps(uCounter_CandidateMeshlets, COUNTER_PHASE2_CANDIDATE_MESHLETS, 1, elementOffset);
				uCandidateMeshlets[elementOffset] = candidate;
			}
		}

		isVisible = !wasOccluded;
#else
		// Occlusion test meshlet against the updated HZB
		isVisible = HZBCull(cullData, tHZB, cCullParams.HZBDimensions);
#endif
	}
#endif

	// If meshlet is visible and wasn't occluded in the previous frame, submit it
	MaterialData material = GetMaterial(instance.MaterialIndex);
	ThreadNodeOutputRecords<VisibleMeshletIndex> meshShaderRecord = meshOutputNodes[material.RasterBin].GetThreadNodeOutputRecords(isVisible ? 1 : 0);
	if(isVisible)
	{
		uint visibleIndex;
		InterlockedAdd_WaveOps(uCounter_VisibleMeshlets, 0, 1, visibleIndex);
		uVisibleMeshlets[visibleIndex] = candidate;
		meshShaderRecord.Get().Index = visibleIndex;
	}
	meshShaderRecord.OutputComplete();
}


[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(128, 1, 1)]
[numthreads(NUM_CULL_MESHLETS_THREADS, 1, 1)]
void CullMeshletsCS(
	DispatchNodeInputRecord<MeshletCullData> meshletRecords,
	[MaxRecords(NUM_CULL_MESHLETS_THREADS)][NodeArraySize(NUM_RASTER_BINS)] NodeOutputArray<VisibleMeshletIndex> MeshNodes,
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
	[MaxRecords(NUM_CULL_MESHLETS_THREADS)][NodeArraySize(NUM_RASTER_BINS)] NodeOutputArray<VisibleMeshletIndex> MeshNodes,
	uint threadIndex : SV_DispatchThreadID)
{
	uint numMeshlets = uCounter_CandidateMeshlets[COUNTER_PHASE2_CANDIDATE_MESHLETS];
	if(threadIndex >= numMeshlets)
		return;

	MeshletCandidate candidate = uCandidateMeshlets[threadIndex];
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
		uint num_instances = uCounter_PhaseTwoInstances[0];
		ThreadNodeOutputRecords<EntryRecord> record = CullInstancesCS.GetThreadNodeOutputRecords(num_instances > 0 ? 1 : 0);
		if(num_instances > 0)
			record.Get().GridSize = DivideAndRoundUp(num_instances, NUM_CULL_INSTANCES_THREADS);
		record.OutputComplete();
	}

	// Kick per-meshlet culling
	{
		uint num_meshlets = uCounter_CandidateMeshlets[COUNTER_PHASE2_CANDIDATE_MESHLETS];
		ThreadNodeOutputRecords<EntryRecord> record = CullMeshletsEntryCS.GetThreadNodeOutputRecords(num_meshlets > 0 ? 1 : 0);
		if(num_meshlets > 0)
			record.Get().GridSize = DivideAndRoundUp(num_meshlets, NUM_CULL_MESHLETS_THREADS);
		record.OutputComplete();
	}
}

#endif

#ifndef MESH_NODE_INDEX
#define MESH_NODE_INDEX -1
#endif

#if MESH_NODE_INDEX != -1

struct PrimitiveAttribute
{
	uint PrimitiveID : SV_PrimitiveID;
	uint VisibleMeshletIndex : VISIBLE_INDEX;
};

struct VertexAttribute
{
	float4 Position : SV_Position;
#if ALPHA_MASK
	float2 UV : TEXCOORD;
#endif
};

VertexAttribute FetchVertexAttributes(MeshData mesh, float4x4 world, uint vertexId)
{
	Vertex vertex = LoadVertex(mesh, vertexId);

	VertexAttribute result = (VertexAttribute)0;
	float3 positionWS = mul(float4(vertex.Position, 1.0f), world).xyz;
	result.Position = mul(float4(positionWS, 1.0f), cView.WorldToClip);
#if ALPHA_MASK
	result.UV = vertex.UV;
#endif
	return result;
}

[Shader("node")]
[NodeID("MeshNodes", MESH_NODE_INDEX)]
[NodeLaunch("mesh")]
[outputtopology("triangle")]
[NodeDispatchGrid(1, 1, 1)]
[numthreads(NUM_MESHLET_THREADS, 1, 1)]
void MSMain(
	in uint groupThreadID : SV_GroupIndex,
	in uint groupID : SV_GroupID,
	in DispatchNodeInputRecord<VisibleMeshletIndex> inputData,
	out vertices VertexAttribute verts[MESHLET_MAX_VERTICES],
	out indices uint3 triangles[MESHLET_MAX_TRIANGLES],
	out primitives PrimitiveAttribute primitives[MESHLET_MAX_TRIANGLES])
{
	uint visibleMeshletIndex = inputData.Get().Index;
	MeshletCandidate candidate = uVisibleMeshlets[visibleMeshletIndex];
	InstanceData instance = GetInstance(candidate.InstanceID);

	MeshData mesh = GetMesh(instance.MeshIndex);
	Meshlet meshlet = ByteBufferLoad<Meshlet>(mesh.BufferIndex, candidate.MeshletIndex, mesh.MeshletOffset);
	SetMeshOutputCounts(meshlet.VertexCount, meshlet.TriangleCount);

	for(uint i = groupThreadID; i < meshlet.VertexCount; i += NUM_MESHLET_THREADS)
	{
		uint vertexId = ByteBufferLoad<uint>(mesh.BufferIndex, i + meshlet.VertexOffset, mesh.MeshletVertexOffset);
		VertexAttribute result = FetchVertexAttributes(mesh, instance.LocalToWorld, vertexId);
		verts[i] = result;
	}

	for(uint i = groupThreadID; i < meshlet.TriangleCount; i += NUM_MESHLET_THREADS)
	{
		Meshlet::Triangle tri = ByteBufferLoad<Meshlet::Triangle>(mesh.BufferIndex, i + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);
		triangles[i] = uint3(tri.V0, tri.V1, tri.V2);
		PrimitiveAttribute pri;
		pri.PrimitiveID = i;
		pri.VisibleMeshletIndex = visibleMeshletIndex;
		primitives[i] = pri;
	}
}

void PSMain(
	VertexAttribute vertexData,
	PrimitiveAttribute primitiveData
#if !DEPTH_ONLY
	, out uint visBufferPixel : SV_TARGET0
#endif
)
{
#if ALPHA_MASK
	MeshletCandidate candidate = uVisibleMeshlets[primitiveData.VisibleMeshletIndex];
	InstanceData instance = GetInstance(candidate.InstanceID);
	MaterialData material = GetMaterial(instance.MaterialIndex);
	float opacity = material.BaseColorFactor.a;
	if(material.Diffuse != INVALID_HANDLE)
		opacity = Sample2D(material.Diffuse, sMaterialSampler, vertexData.UV).w;
	if(opacity < material.AlphaCutoff)
		discard;
#endif

#if !DEPTH_ONLY
	visBufferPixel = PackVisBuffer(primitiveData.VisibleMeshletIndex, primitiveData.PrimitiveID);
#endif

#if ENABLE_DEBUG_DATA
	InterlockedAdd(uDebugData[(uint2)vertexData.Position.xy], 1);
#endif
}

#endif