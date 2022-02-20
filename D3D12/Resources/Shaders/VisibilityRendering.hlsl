#include "Common.hlsli"
#include "CommonBindings.hlsli"
#include "Random.hlsli"
#include "VisibilityBuffer.hlsli"

ConstantBuffer<InstanceData> cObject : register(b0);

bool IsVisible(MeshData mesh, float4x4 world, uint meshlet)
{
	MeshletBounds cullData = BufferLoad<MeshletBounds>(mesh.BufferIndex, meshlet, mesh.MeshletBoundsOffset);

	float4 center = mul(float4(cullData.Center, 1), world);
	float3 radius3 = abs(mul(cullData.Radius.xxx, (float3x3)world));
	float radius = max(radius3.x, max(radius3.y, radius3.z));
	float3 coneAxis = normalize(mul(cullData.ConeAxis, (float3x3)world));

	for(int i = 0; i < 6; ++i)
	{
		if(dot(center, cView.FrustumPlanes[i]) > radius)
		{
			return false;
		}
	}

	float3 viewLocation = cView.ViewPosition.xyz;
	if(dot(viewLocation - center.xyz, coneAxis) >= cullData.ConeCutoff * length(center.xyz - viewLocation) + radius)
	{
		return false;
	}
	return true;
}

#define NUM_AS_THREADS 32

struct PayloadData
{
	uint Indices[NUM_AS_THREADS];
};

groupshared PayloadData gsPayload;

[numthreads(NUM_AS_THREADS, 1, 1)]
void ASMain(uint threadID : SV_DispatchThreadID)
{
	bool visible = false;

	MeshData mesh = GetMesh(cObject.Mesh);
	float4x4 world = GetTransform(cObject.World);
	if (threadID < mesh.MeshletCount)
	{
		visible = IsVisible(mesh, world, threadID);
	}

	if (visible)
	{
		uint index = WavePrefixCountBits(visible);
		gsPayload.Indices[index] = threadID;
	}

	// Dispatch the required number of MS threadgroups to render the visible meshlets
	uint visibleCount = WaveActiveCountBits(visible);
	DispatchMesh(visibleCount, 1, 1, gsPayload);
}

struct PrimitiveAttribute
{
	uint PrimitiveID : SV_PrimitiveID;
	uint MeshletID : MESHLET_ID;
};

struct VertexAttribute
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD;
};

VertexAttribute FetchVertexAttributes(MeshData mesh, float4x4 world, uint vertexId)
{
	VertexAttribute result;
	float3 Position = BufferLoad<float3>(mesh.BufferIndex, vertexId, mesh.PositionsOffset);
	float3 positionWS = mul(float4(Position, 1.0f), world).xyz;
	result.Position = mul(float4(positionWS, 1.0f), cView.ViewProjection);
	result.UV = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
	return result;
}

#define NUM_MESHLET_THREADS 32

[outputtopology("triangle")]
[numthreads(NUM_MESHLET_THREADS, 1, 1)]
void MSMain(
	in uint groupThreadID : SV_GroupIndex,
	in payload PayloadData payload,
	in uint groupID : SV_GroupID,
	out vertices VertexAttribute verts[MESHLET_MAX_VERTICES],
	out indices uint3 triangles[MESHLET_MAX_TRIANGLES],
	out primitives PrimitiveAttribute primitives[MESHLET_MAX_TRIANGLES])
{
	MeshData mesh = GetMesh(cObject.Mesh);

	uint meshletIndex = payload.Indices[groupID];
	if(meshletIndex >= mesh.MeshletCount)
	{
		return;
	}

	Meshlet meshlet = BufferLoad<Meshlet>(mesh.BufferIndex, meshletIndex, mesh.MeshletOffset);

	SetMeshOutputCounts(meshlet.VertexCount, meshlet.TriangleCount);

	float4x4 world = GetTransform(cObject.World);
	for(uint i = groupThreadID; i < meshlet.VertexCount; i += NUM_MESHLET_THREADS)
	{
		uint vertexId = BufferLoad<uint>(mesh.BufferIndex, i + meshlet.VertexOffset, mesh.MeshletVertexOffset);
		VertexAttribute result = FetchVertexAttributes(mesh, world, vertexId);
		verts[i] = result;
	}

	for(uint i = groupThreadID; i < meshlet.TriangleCount; i += NUM_MESHLET_THREADS)
	{
		MeshletTriangle tri = BufferLoad<MeshletTriangle>(mesh.BufferIndex, i + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);
		triangles[i] = uint3(tri.V0, tri.V1, tri.V2);

		PrimitiveAttribute pri;
		pri.PrimitiveID = i;
		pri.MeshletID = meshletIndex;
		primitives[i] = pri;
	}
}

VisBufferData PSMain(
    VertexAttribute vertexData,
    PrimitiveAttribute primitiveData) : SV_TARGET0
{
#ifdef ALPHA_TEST
	MaterialData material = GetMaterial(cObject.Material);
	float opacity = material.BaseColorFactor.a;
	if(material.Diffuse != INVALID_HANDLE)
	{
		opacity *= Sample2D(material.Diffuse, sMaterialSampler, vertexData.UV).a;
	}
	if(opacity < material.AlphaCutoff)
	{
		discard;
	}
#endif

	VisBufferData Data;
	Data.ObjectID = cObject.World;
	Data.PrimitiveID = primitiveData.PrimitiveID;
	Data.MeshletID = primitiveData.MeshletID;
	return Data;
}
