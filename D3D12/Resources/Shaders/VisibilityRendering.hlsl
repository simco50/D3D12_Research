#include "Common.hlsli"
#include "CommonBindings.hlsli"
#include "Random.hlsli"
#include "VisibilityBuffer.hlsli"

ConstantBuffer<InstanceData> cObject : register(b0);

struct InterpolantsVSToPS
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD;
};

InterpolantsVSToPS FetchVertexAttributes(MeshData mesh, float4x4 world, uint vertexId)
{
	InterpolantsVSToPS result;
	float3 Position = BufferLoad<float3>(mesh.BufferIndex, vertexId, mesh.PositionsOffset);
	float3 positionWS = mul(float4(Position, 1.0f), world).xyz;
	result.Position = mul(float4(positionWS, 1.0f), cView.ViewProjection);
	result.UV = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
	return result;
}

bool IsVisible(MeshData mesh, float4x4 world, uint meshlet)
{
	MeshletBounds cullData = BufferLoad<MeshletBounds>(mesh.BufferIndex, meshlet, mesh.MeshletBoundsOffset);

	float4 center = mul(float4(cullData.Center, 1), world);

	for(int i = 0; i < 6; ++i)
	{
		if(dot(center, cView.FrustumPlanes[i]) > cullData.Radius)
		{
			return false;
		}
	}

	float3 viewLocation = cView.ViewPosition.xyz;
	float3 coneApex = mul(float4(cullData.ConeApex, 1), world).xyz;
	float3 coneAxis = mul(cullData.ConeAxis, (float3x3)world);
	float3 view = normalize(viewLocation - coneApex);
	if (dot(view, coneAxis) >= cullData.ConeCutoff)
	{
		return false;
	}
	return true;
}

struct PayloadData
{
	uint Indices[32];
};

groupshared PayloadData gsPayload;

[numthreads(32, 1, 1)]
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

#define NUM_MESHLET_THREADS 32

struct PrimitiveData
{
	uint PrimitiveID : SV_PrimitiveID;
};

[outputtopology("triangle")]
[numthreads(NUM_MESHLET_THREADS, 1, 1)]
void MSMain(
	in uint groupThreadID : SV_GroupIndex,
	in payload PayloadData payload,
	in uint groupID : SV_GroupID,
	out vertices InterpolantsVSToPS verts[MESHLET_MAX_VERTICES],
	out indices uint3 triangles[MESHLET_MAX_TRIANGLES],
	out primitives PrimitiveData primitives[MESHLET_MAX_TRIANGLES])
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
		InterpolantsVSToPS result = FetchVertexAttributes(mesh, world, vertexId);
		verts[i] = result;
	}

	for(uint i = groupThreadID; i < meshlet.TriangleCount; i += NUM_MESHLET_THREADS)
	{
		MeshletTriangle tri = BufferLoad<MeshletTriangle>(mesh.BufferIndex, i + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);
		triangles[i] = uint3(tri.V0, tri.V1, tri.V2);

		PrimitiveData pri;
		// The primitiveID here is not the same as SV_PrimitiveID because the order of a regular index buffer is different.
		// So this doesn't work :( Problem for another day.
		pri.PrimitiveID = i + meshlet.TriangleOffset;
		primitives[i] = pri;
	}
}

InterpolantsVSToPS VSMain(uint vertexId : SV_VertexID)
{
	MeshData mesh = GetMesh(cObject.Mesh);
	float4x4 world = GetTransform(cObject.World);
	InterpolantsVSToPS result = FetchVertexAttributes(mesh, world, vertexId);
	return result;
}

VisBufferData PSMain(
    InterpolantsVSToPS input,
    uint primitiveID : SV_PrimitiveID) : SV_TARGET0
{
#ifdef ALPHA_TEST
	MaterialData material = GetMaterial(cObject.Material);
	float opacity = material.BaseColorFactor.a;
	if(material.Diffuse != INVALID_HANDLE)
	{
		opacity *= Sample2D(material.Diffuse, sMaterialSampler, input.UV).a;
	}
	if(opacity < material.AlphaCutoff)
	{
		discard;
	}
#endif

	VisBufferData Data;
	Data.ObjectID = cObject.World;
	Data.PrimitiveID = primitiveID;
	return Data;
}
