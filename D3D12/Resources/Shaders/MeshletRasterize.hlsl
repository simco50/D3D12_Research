#include "Common.hlsli"
#include "VisibilityBuffer.hlsli"

#ifndef ALPHA_MASK
#define ALPHA_MASK 0
#endif

#ifndef DEPTH_ONLY
#define DEPTH_ONLY 0
#endif

#define NUM_MESHLET_THREADS 32

struct RasterParams
{
	uint BinIndex;
};
ConstantBuffer<RasterParams> cRasterParams : register(b0);

StructuredBuffer<MeshletCandidate> tVisibleMeshlets : 				register(t0);
StructuredBuffer<uint> tBinnedMeshlets : 							register(t1);
StructuredBuffer<uint4> tMeshletBinData :							register(t2);

RWTexture2D<uint> uDebugData : 										register(u0);


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
	VertexAttribute result = (VertexAttribute)0;
	float3 position = Unpack_RGBA16_SNORM(BufferLoad<uint2>(mesh.BufferIndex, vertexId, mesh.PositionsOffset)).xyz;
	float3 positionWS = mul(float4(position, 1.0f), world).xyz;
	result.Position = mul(float4(positionWS, 1.0f), cView.ViewProjection);
#if ALPHA_MASK
	if(mesh.UVsOffset != 0xFFFFFFFF)
		result.UV = Unpack_RG16_FLOAT(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
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
	meshletIndex += tMeshletBinData[cRasterParams.BinIndex].w;
	meshletIndex = tBinnedMeshlets[meshletIndex];

	MeshletCandidate candidate = tVisibleMeshlets[meshletIndex];
	InstanceData instance = GetInstance(candidate.InstanceID);
	MeshData mesh = GetMesh(instance.MeshIndex);
	Meshlet meshlet = BufferLoad<Meshlet>(mesh.BufferIndex, candidate.MeshletIndex, mesh.MeshletOffset);

	SetMeshOutputCounts(meshlet.VertexCount, meshlet.TriangleCount);

	for(uint i = groupThreadID; i < meshlet.VertexCount; i += NUM_MESHLET_THREADS)
	{
		uint vertexId = BufferLoad<uint>(mesh.BufferIndex, i + meshlet.VertexOffset, mesh.MeshletVertexOffset);
		VertexAttribute result = FetchVertexAttributes(mesh, instance.LocalToWorld, vertexId);
		verts[i] = result;
	}

	for(uint i = groupThreadID; i < meshlet.TriangleCount; i += NUM_MESHLET_THREADS)
	{
		Meshlet::Triangle tri = BufferLoad<Meshlet::Triangle>(mesh.BufferIndex, i + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);
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
	, out VisBufferData visBufferData : SV_TARGET0
#endif
)
{
#if ALPHA_MASK
	MeshletCandidate candidate = tVisibleMeshlets[primitiveData.CandidateIndex];
	InstanceData instance = GetInstance(candidate.InstanceID);
	MaterialData material = GetMaterial(instance.MaterialIndex);
	float opacity = material.BaseColorFactor.a;
	if(material.Diffuse != INVALID_HANDLE)
		opacity = Sample2D(material.Diffuse, sMaterialSampler, vertexData.UV).w;
	if(opacity < material.AlphaCutoff)
		discard;
#endif

#if !DEPTH_ONLY
	visBufferData.MeshletCandidateIndex = primitiveData.CandidateIndex;
	visBufferData.PrimitiveID = primitiveData.PrimitiveID;
#endif

#if ENABLE_DEBUG_DATA
	InterlockedAdd(uDebugData[(uint2)vertexData.Position.xy], 1);
#endif
}
