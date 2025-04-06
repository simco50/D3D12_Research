#include "Common.hlsli"
#include "VisibilityBuffer.hlsli"

/*
	Mesh/Pixel shader for rasterizing the result of the Meshlet culling passes.
	Supports visibility buffer and depth-only output.
*/

#ifndef ALPHA_MASK
#define ALPHA_MASK 0
#endif

#ifndef DEPTH_ONLY
#define DEPTH_ONLY 0
#endif

#ifndef ENABLE_DEBUG_DATA
#define ENABLE_DEBUG_DATA 0
#endif

#define NUM_MESHLET_THREADS 32

struct RasterParams
{
	uint BinIndex;
	StructuredBufferH<MeshletCandidate> VisibleMeshlets;
	StructuredBufferH<uint> BinnedMeshlets;
	StructuredBufferH<uint4> MeshletBinData;
	RWTexture2DH<uint> DebugData;
};
DEFINE_CONSTANTS(RasterParams, 0);

struct PrimitiveAttribute
{
	uint PrimitiveID : SV_PrimitiveID;
	uint CandidateIndex : CANDIDATE_INDEX;
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

[outputtopology("triangle")]
	[numthreads(NUM_MESHLET_THREADS, 1, 1)]
	void MSMain(
	in uint groupThreadID : SV_GroupIndex,
	in uint groupID : SV_GroupID,
	out vertices VertexAttribute verts[MESHLET_MAX_VERTICES],
	out indices uint3 triangles[MESHLET_MAX_TRIANGLES],
	out primitives PrimitiveAttribute primitives[MESHLET_MAX_TRIANGLES])
{
	uint meshletIndex = groupID;
	// Find actual meshlet index by offsetting based on classification data
	meshletIndex += cRasterParams.MeshletBinData[cRasterParams.BinIndex].w;
	meshletIndex = cRasterParams.BinnedMeshlets[meshletIndex];

	MeshletCandidate candidate = cRasterParams.VisibleMeshlets[meshletIndex];
	InstanceData instance = GetInstance(candidate.InstanceID);
	MeshData mesh = GetMesh(instance.MeshIndex);
	Meshlet meshlet = mesh.DataBuffer.LoadStructure<Meshlet>(candidate.MeshletIndex, mesh.MeshletOffset);

	SetMeshOutputCounts(meshlet.VertexCount, meshlet.TriangleCount);

	for(uint i = groupThreadID; i < meshlet.VertexCount; i += NUM_MESHLET_THREADS)
	{
		uint vertexId = mesh.DataBuffer.LoadStructure<uint>(i + meshlet.VertexOffset, mesh.MeshletVertexOffset);
		VertexAttribute result = FetchVertexAttributes(mesh, instance.LocalToWorld, vertexId);
		verts[i] = result;
	}

	for(uint i = groupThreadID; i < meshlet.TriangleCount; i += NUM_MESHLET_THREADS)
	{
		Meshlet::Triangle tri =  mesh.DataBuffer.LoadStructure<Meshlet::Triangle>(i + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);
		triangles[i] = uint3(tri.V0, tri.V1, tri.V2);

		PrimitiveAttribute pri;
		pri.PrimitiveID = i;
		pri.CandidateIndex = meshletIndex;
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
	MeshletCandidate candidate = cRasterParams.VisibleMeshlets[primitiveData.CandidateIndex];
	InstanceData instance = GetInstance(candidate.InstanceID);
	MaterialData material = GetMaterial(instance.MaterialIndex);
	float opacity = material.BaseColorFactor.a;
	if(material.Diffuse.IsValid())
		opacity = material.Diffuse.Sample(sMaterialSampler, vertexData.UV).w;
	if(opacity < material.AlphaCutoff)
		discard;
#endif

#if !DEPTH_ONLY
	visBufferPixel = PackVisBuffer(primitiveData.CandidateIndex, primitiveData.PrimitiveID);
#endif

#if ENABLE_DEBUG_DATA
	InterlockedAdd(cRasterParams.DebugData.Get()[(uint2)vertexData.Position.xy], 1);
#endif
}
