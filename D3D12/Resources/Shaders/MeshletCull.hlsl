#include "Common.hlsli"
#include "HZB.hlsli"
#include "D3D12.hlsli"
#include "VisibilityBuffer.hlsli"
#include "WaveOps.hlsli"
#include "ShaderDebugRender.hlsli"

#ifndef OCCLUSION_FIRST_PASS
#define OCCLUSION_FIRST_PASS 1
#endif

#ifndef ALPHA_MASK
#define ALPHA_MASK 0
#endif

struct MeshletCandidate
{
    uint InstanceID;
    uint MeshletIndex;
};

RWStructuredBuffer<MeshletCandidate> uMeshletsToProcess : register(u0);
RWBuffer<uint> uCounter_MeshletsToProcess : register(u1);

RWStructuredBuffer<uint> uCulledInstances : register(u2);
RWBuffer<uint> uCounter_CulledInstances : register(u3);

RWStructuredBuffer<MeshletCandidate> uCulledMeshlets : register(u4);
RWBuffer<uint> uCounter_CulledMeshlets : register(u5);

RWStructuredBuffer<D3D12_DISPATCH_ARGUMENTS> uDispatchArguments : register(u0);

StructuredBuffer<MeshletCandidate> tMeshletsToProcess : register(t0);
Buffer<uint> tCounter_MeshletsToProcess : register(t1);

StructuredBuffer<uint> tInstancesToProcess : register(t0);
Buffer<uint> tCounter_CulledInstances : register(t1);

Texture2D<float> tHZB : register(t2);

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
        InterlockedAdd_Varying_WaveOps(uCounter_MeshletsToProcess, 0, mesh.MeshletCount, elementOffset);
        for(uint i = 0; i < mesh.MeshletCount; ++i)
        {
            MeshletCandidate meshlet;
            meshlet.InstanceID = instance.ID;
            meshlet.MeshletIndex = i;
            uMeshletsToProcess[elementOffset + i] = meshlet;
        }
    }
}

#define NUM_AS_THREADS 32

[numthreads(1, 1, 1)]
void BuildMeshShaderIndirectArgs(uint threadID : SV_DispatchThreadID)
{
    uint numMeshlets = tCounter_MeshletsToProcess[0];
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
	uint InstanceIDs[NUM_AS_THREADS];
	uint MeshletIndices[NUM_AS_THREADS];
};

groupshared PayloadData gsPayload;

#if __SHADER_TARGET_STAGE == __SHADER_STAGE_AMPLIFICATION
[numthreads(NUM_AS_THREADS, 1, 1)]
void CullAndDrawMeshletsAS(uint threadID : SV_DispatchThreadID)
{
	bool shouldSubmit = false;

	if(threadID < tCounter_MeshletsToProcess[0])
	{
		MeshletCandidate meshlet = tMeshletsToProcess[threadID];
		InstanceData instance = GetInstance(meshlet.InstanceID);
		MeshData mesh = GetMesh(instance.MeshIndex);
		MeshletBounds bounds = BufferLoad<MeshletBounds>(mesh.BufferIndex, meshlet.MeshletIndex, mesh.MeshletBoundsOffset);

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
				InterlockedAdd_WaveOps(uCounter_CulledMeshlets, 0, elementOffset);
				uCulledMeshlets[elementOffset] = meshlet;
			}
#else
			isVisible = HZBCull(cullData, tHZB);
#endif
		}

		shouldSubmit = isVisible && wasVisible;
		if(shouldSubmit)
		{
			uint index = WavePrefixCountBits(shouldSubmit);
			gsPayload.InstanceIDs[index] = meshlet.InstanceID;
			gsPayload.MeshletIndices[index] = meshlet.MeshletIndex;
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
#if ALPHA_MASK
	float2 UV : TEXCOORD;
#endif
};

VertexAttribute FetchVertexAttributes(MeshData mesh, float4x4 world, uint vertexId)
{
	VertexAttribute result = (VertexAttribute)0;
	float3 Position = BufferLoad<float3>(mesh.BufferIndex, vertexId, mesh.PositionsOffset);
	float3 positionWS = mul(float4(Position, 1.0f), world).xyz;
	result.Position = mul(float4(positionWS, 1.0f), cView.ViewProjection);
#if ALPHA_MASK
	if(mesh.UVsOffset != 0xFFFFFFFF)
		result.UV = UnpackHalf2(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
#endif
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
	uint meshletIndex = payload.MeshletIndices[groupID];
	uint instanceID = payload.InstanceIDs[groupID];

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
#if ALPHA_MASK
	InstanceData instance = GetInstance(primitiveData.InstanceID);
	MaterialData material = GetMaterial(instance.MaterialIndex);
	float opacity = Sample2D(material.Diffuse, sMaterialSampler, vertexData.UV).w;
	if(opacity < material.AlphaCutoff)
		discard;
#endif

	VisBufferData Data;
	Data.ObjectID = primitiveData.InstanceID;
	Data.PrimitiveID = primitiveData.PrimitiveID;
	Data.MeshletID = primitiveData.MeshletID;
	return Data;
}

[numthreads(1, 1, 1)]
void PrintStatsCS(uint threadId : SV_DispatchThreadID)
{
	uint numInstances = cView.NumInstances;
	uint occludedInstances = uCounter_CulledInstances[0];
	uint visibleInstances = numInstances - occludedInstances;
	uint phase1Meshlets = uCounter_MeshletsToProcess[0];
	uint phase2Meshlets = uCounter_CulledMeshlets[0];

	TextWriter writer = CreateTextWriter(float2(20, 20));

	writer = writer + 'S' + 'c' + 'e' + 'n'  + 'e'  + ' ';
	writer = writer + 'i' + 'n' + 's' + 't'  + 'a'  + 'n'  + 'c'  + 'e'  + 's' + ' ';
	writer.Int(numInstances);
	writer.NewLine();

	writer = writer + '-' + '-' + '-' + 'P' + 'h' + 'a' + 's' + 'e' + ' ' + '1' + '-' + '-' + '-';
	writer.NewLine();

	writer = writer + 'O' + 'c' + 'c' + 'l'  + 'u'  + 'd'  + 'e'  + 'd' + ' ';
	writer = writer + 'i' + 'n' + 's' + 't'  + 'a'  + 'n'  + 'c'  + 'e'  + 's' + ' ';
	writer.Int(occludedInstances);
	writer.NewLine();

	writer = writer + 'P' + 'r' + 'o' + 'c'  + 'e'  + 's'  + 's'  + 'e' + 'd' + ' ';
	writer = writer + 'm' + 'e' + 's' + 'h'  + 'l'  + 'e'  + 't'  + 's' + ' ';
	writer.Int(phase1Meshlets);
	writer.NewLine();

	writer = writer + '-' + '-' + '-' + 'P' + 'h' + 'a' + 's' + 'e' + ' ' + '2' + '-' + '-' + '-';
	writer.NewLine();

	writer = writer + 'P' + 'r' + 'o' + 'c'  + 'e'  + 's'  + 's'  + 'e' + 'd' + ' ';
	writer = writer + 'm' + 'e' + 's' + 'h'  + 'l'  + 'e'  + 't'  + 's' + ' ';
	writer.Int(phase2Meshlets);
	writer.NewLine();
}
