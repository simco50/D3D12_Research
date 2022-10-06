#include "Common.hlsli"
#include "HZB.hlsli"
#include "D3D12.hlsli"
#include "VisibilityBuffer.hlsli"

#ifndef OCCLUSION_FIRST_PASS
#define OCCLUSION_FIRST_PASS 1
#endif

struct VisibleCluster
{
    uint InstanceID;
    uint ClusterIndex;
};

RWStructuredBuffer<VisibleCluster> uClustersToProcess : register(u0);
RWBuffer<uint> uCounter_ClustersToProcess : register(u1);

RWStructuredBuffer<uint> uCulledInstances : register(u2);
RWBuffer<uint> uCounter_CulledInstances : register(u3);

RWStructuredBuffer<VisibleCluster> uCulledClusters : register(u4);
RWBuffer<uint> uCounter_CulledClusters : register(u5);

RWStructuredBuffer<D3D12_DISPATCH_ARGUMENTS> uDispatchArguments : register(u0);

StructuredBuffer<VisibleCluster> tClustersToProcess : register(t0);
Buffer<uint> tCounter_ClustersToProcess : register(t1);

StructuredBuffer<uint> tInstancesToProcess : register(t0);
Buffer<uint> tCounter_CulledInstances : register(t1);

Texture2D<float> tHZB : register(t2);

#define WAVE_OPS 1

#if WAVE_OPS
template<typename T>
void InterlockedAdd_WaveOps(T bufferResource, uint elementIndex, out uint originalValue)
{
	uint numValues = WaveActiveCountBits(true);
	if(WaveIsFirstLane())
		InterlockedAdd(bufferResource[elementIndex], numValues, originalValue);
	originalValue = WaveReadLaneFirst(originalValue) + WavePrefixCountBits(true);
}

template<typename T>
void InterlockedAdd_Varying_WaveOps(T bufferResource, uint elementIndex, uint numValues, out uint originalValue)
{
	uint count = WaveActiveSum(numValues);
	if(WaveIsFirstLane())
		InterlockedAdd(bufferResource[elementIndex], count, originalValue);
	originalValue = WaveReadLaneFirst(originalValue) + WavePrefixSum(numValues);
}
#else
template<typename T>
void InterlockedAdd_WaveOps(T bufferResource, uint elementIndex, out uint originalValue)
{
	InterlockedAdd(bufferResource[elementIndex], originalValue);
}

template<typename T>
void InterlockedAdd_Varying_WaveOps(T bufferResource, uint elementIndex, uint numValues, out uint originalValue)
{
	InterlockedAdd(bufferResource[elementIndex], originalValue);
}
#endif

[numthreads(64, 1, 1)]
void CullInstancesCS(uint threadID : SV_DispatchThreadID)
{
#if OCCLUSION_FIRST_PASS
    uint numInstances = cView.NumInstances;
#else
    uint numInstances = tCounter_CulledInstances[0];
#endif

    if(threadID >= numInstances)
    {
        return;
    }

#if OCCLUSION_FIRST_PASS
    InstanceData instance = GetInstance(threadID);
#else
    InstanceData instance = GetInstance(tInstancesToProcess[threadID]);
#endif

    MeshData mesh = GetMesh(instance.MeshIndex);

	FrustumCullData cullData = FrustumCull(instance.BoundsOrigin, instance.BoundsExtents, cView.ViewProjection);
	bool isVisible = cullData.IsVisible;
	bool wasVisible = true;

	if(isVisible)
	{
#if OCCLUSION_FIRST_PASS
		FrustumCullData prevCullData = FrustumCull(instance.BoundsOrigin, instance.BoundsExtents, cView.ViewProjectionPrev);
		wasVisible = HZBCull(prevCullData, tHZB);
		if(!wasVisible)
		{
			uint elementOffset = 0;
			InterlockedAdd_WaveOps(uCounter_CulledInstances, 0, elementOffset);
			uCulledInstances[elementOffset] = instance.ID;
		}
#else
		isVisible = HZBCull(cullData, tHZB);
#endif
	}

    if(isVisible && wasVisible)
    {
        uint elementOffset;
        InterlockedAdd_Varying_WaveOps(uCounter_ClustersToProcess, 0, mesh.MeshletCount, elementOffset);
        for(uint i = 0; i < mesh.MeshletCount; ++i)
        {
            VisibleCluster cluster;
            cluster.InstanceID = instance.ID;
            cluster.ClusterIndex = i;
            uClustersToProcess[elementOffset + i] = cluster;
        }
    }
}

#define NUM_AS_THREADS 32

[numthreads(1, 1, 1)]
void BuildMeshShaderIndirectArgs(uint threadID : SV_DispatchThreadID)
{
    uint numMeshlets = tCounter_ClustersToProcess[0];
    D3D12_DISPATCH_ARGUMENTS args;
    args.ThreadGroupCountX = DivideAndRoundUp(numMeshlets, NUM_AS_THREADS);
    args.ThreadGroupCountY = 1;
    args.ThreadGroupCountZ = 1;
    uDispatchArguments[0] = args;
}

[numthreads(1, 1, 1)]
void BuildInstanceCullIndirectArgs(uint threadID : SV_DispatchThreadID)
{
    uint numInstances = tCounter_CulledInstances[0];
    D3D12_DISPATCH_ARGUMENTS args;
    args.ThreadGroupCountX = DivideAndRoundUp(numInstances, 64);
    args.ThreadGroupCountY = 1;
    args.ThreadGroupCountZ = 1;
    uDispatchArguments[0] = args;
}

struct PayloadData
{
	uint InstanceIndices[NUM_AS_THREADS];
	uint MeshletIndices[NUM_AS_THREADS];
};

groupshared PayloadData gsPayload;

#if __SHADER_TARGET_STAGE == __SHADER_STAGE_AMPLIFICATION
[numthreads(NUM_AS_THREADS, 1, 1)]
void CullAndDrawMeshletsAS(uint threadID : SV_DispatchThreadID)
{
	bool shouldSubmit = false;

	if(threadID < tCounter_ClustersToProcess[0])
	{
		VisibleCluster cluster = tClustersToProcess[threadID];
		InstanceData instance = GetInstance(cluster.InstanceID);
		MeshData mesh = GetMesh(instance.MeshIndex);
		MeshletBounds bounds = BufferLoad<MeshletBounds>(mesh.BufferIndex, cluster.ClusterIndex, mesh.MeshletBoundsOffset);

		float4x4 world = instance.LocalToWorld;
		float4 center = mul(float4(bounds.Center, 1), world);
		float3 radius3 = abs(mul(bounds.Radius.xxx, (float3x3)world));
		float radius = Max3(radius3);
		float3 coneAxis = normalize(mul(bounds.ConeAxis, (float3x3)world));

		FrustumCullData cullData = FrustumCull(center.xyz, radius3, cView.ViewProjection);
		bool isVisible = cullData.IsVisible;
		bool wasVisible = true;

		if(isVisible)
		{
#if OCCLUSION_FIRST_PASS
			FrustumCullData prevCullData = FrustumCull(center.xyz, radius3, cView.ViewProjectionPrev);
			if(prevCullData.IsVisible)
			{
				wasVisible = HZBCull(prevCullData, tHZB);
			}
			if(!wasVisible)
			{
				uint elementOffset;
				InterlockedAdd_WaveOps(uCounter_CulledClusters, 0, elementOffset);
				uCulledClusters[elementOffset] = cluster;
			}
#else
			isVisible = HZBCull(cullData, tHZB);
#endif
		}

		shouldSubmit = isVisible && wasVisible;
		if(shouldSubmit)
		{
			uint index = WavePrefixCountBits(shouldSubmit);
			gsPayload.InstanceIndices[index] = cluster.InstanceID;
			gsPayload.MeshletIndices[index] = cluster.ClusterIndex;
		}
	}

	uint visibleCount = WaveActiveCountBits(shouldSubmit);
	DispatchMesh(visibleCount, 1, 1, gsPayload);
}
#endif

struct PrimitiveAttribute
{
	uint PrimitiveID : SV_PrimitiveID;
	uint MeshletID : MESHLET_ID;
	uint InstanceID : INSTANCE_ID;
};

struct VertexAttribute
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD;
};

VertexAttribute FetchVertexAttributes(MeshData mesh, float4x4 world, uint vertexId)
{
	VertexAttribute result = (VertexAttribute)0;
	float3 Position = BufferLoad<float3>(mesh.BufferIndex, vertexId, mesh.PositionsOffset);
	float3 positionWS = mul(float4(Position, 1.0f), world).xyz;
	result.Position = mul(float4(positionWS, 1.0f), cView.ViewProjection);
	if(mesh.UVsOffset != 0xFFFFFFFF)
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
	uint instanceID = payload.InstanceIndices[groupID];
	uint meshletIndex = payload.MeshletIndices[groupID];

	InstanceData instance = GetInstance(instanceID);
	MeshData mesh = GetMesh(instance.MeshIndex);
	Meshlet meshlet = BufferLoad<Meshlet>(mesh.BufferIndex, meshletIndex, mesh.MeshletOffset);

	SetMeshOutputCounts(meshlet.VertexCount, meshlet.TriangleCount);

	for(uint i = groupThreadID; i < meshlet.VertexCount; i += NUM_MESHLET_THREADS)
	{
		uint vertexId = BufferLoad<uint>(mesh.BufferIndex, i + meshlet.VertexOffset, mesh.MeshletVertexOffset);
		VertexAttribute result = FetchVertexAttributes(mesh, instance.LocalToWorld, vertexId);
		verts[i] = result;
	}

	for(uint i = groupThreadID; i < meshlet.TriangleCount; i += NUM_MESHLET_THREADS)
	{
		MeshletTriangle tri = BufferLoad<MeshletTriangle>(mesh.BufferIndex, i + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);
		triangles[i] = uint3(tri.V0, tri.V1, tri.V2);

		PrimitiveAttribute pri;
		pri.PrimitiveID = i;
		pri.MeshletID = meshletIndex;
		pri.InstanceID = instanceID;
		primitives[i] = pri;
	}
}

VisBufferData PSMain(
    VertexAttribute vertexData,
    PrimitiveAttribute primitiveData) : SV_TARGET0
{
	VisBufferData Data;
	Data.ObjectID = primitiveData.InstanceID;
	Data.PrimitiveID = primitiveData.PrimitiveID;
	Data.MeshletID = primitiveData.MeshletID;
	return Data;
}
