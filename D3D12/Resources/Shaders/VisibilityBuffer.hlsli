#pragma once

// Meshlet which is a candidate for visibility culling
struct MeshletCandidate
{
    uint InstanceID;
    uint MeshletIndex;
};

struct VisBufferData
{
	// Indirection into MeshletCandidatesBuffer containing InstanceID/MeshletIndex pair
	uint MeshletCandidateIndex : 25;
	uint PrimitiveID : 7;
};

template<typename T>
T BaryInterpolate(T a, T b, T c, float3 barycentrics)
{
	return a * barycentrics.x + b * barycentrics.y + c * barycentrics.z;
}

struct BaryDerivs
{
	float3 Barycentrics;
	float3 DDX_Barycentrics;
	float3 DDY_Barycentrics;
};

BaryDerivs ComputeBarycentrics(float2 pixelCS, float4 vertexCS0, float4 vertexCS1, float4 vertexCS2)
{
	BaryDerivs result;

	float3 pos0 = vertexCS0.xyz / vertexCS0.w;
	float3 pos1 = vertexCS1.xyz / vertexCS1.w;
	float3 pos2 = vertexCS2.xyz / vertexCS2.w;

	float3 RcpW = rcp(float3(vertexCS0.w, vertexCS1.w, vertexCS2.w));

	float3 pos120X = float3(pos1.x, pos2.x, pos0.x);
	float3 pos120Y = float3(pos1.y, pos2.y, pos0.y);
	float3 pos201X = float3(pos2.x, pos0.x, pos1.x);
	float3 pos201Y = float3(pos2.y, pos0.y, pos1.y);

	float3 C_dx = pos201Y - pos120Y;
	float3 C_dy = pos120X - pos201X;

	float3 C = C_dx * (pixelCS.x - pos120X) + C_dy * (pixelCS.y - pos120Y);
	float3 G = C * RcpW;

	float H = dot(C, RcpW);
	float rcpH = rcp(H);

	result.Barycentrics = G * rcpH;

	float3 G_dx = C_dx * RcpW;
	float3 G_dy = C_dy * RcpW;

	float H_dx = dot(C_dx, RcpW);
	float H_dy = dot(C_dy, RcpW);

	result.DDX_Barycentrics = (G_dx * H - G * H_dx) * (rcpH * rcpH) * ( 2.0f * cView.ViewportDimensionsInv.x);
	result.DDY_Barycentrics = (G_dy * H - G * H_dy) * (rcpH * rcpH) * (-2.0f * cView.ViewportDimensionsInv.y);

	return result;
}


struct VisBufferVertexAttribute
{
	float3 Position;
	float2 UV;
	float3 Normal;
	float4 Tangent;
	uint Color;

	float2 DX;
	float2 DY;
	float3 Barycentrics;
};

VisBufferVertexAttribute GetVertexAttributes(float2 screenUV, InstanceData instance, uint meshletIndex, uint primitiveID)
{
	MeshData mesh = GetMesh(instance.MeshIndex);
	Meshlet meshlet = BufferLoad<Meshlet>(mesh.BufferIndex, meshletIndex, mesh.MeshletOffset);
	Meshlet::Triangle tri = BufferLoad<Meshlet::Triangle>(mesh.BufferIndex, primitiveID + meshlet.TriangleOffset, mesh.MeshletTriangleOffset);

	uint3 indices = uint3(
		BufferLoad<uint>(mesh.BufferIndex, tri.V0 + meshlet.VertexOffset, mesh.MeshletVertexOffset),
		BufferLoad<uint>(mesh.BufferIndex, tri.V1 + meshlet.VertexOffset, mesh.MeshletVertexOffset),
		BufferLoad<uint>(mesh.BufferIndex, tri.V2 + meshlet.VertexOffset, mesh.MeshletVertexOffset)
	);

	VisBufferVertexAttribute vertices[3];
	float3 positions[3];
	for(uint i = 0; i < 3; ++i)
	{
		uint vertexId = indices[i];
		float3 position = Unpack_RGBA16_SNORM(BufferLoad<uint2>(mesh.BufferIndex, vertexId, mesh.PositionsOffset)).xyz;
		positions[i] = mul(float4(position, 1), instance.LocalToWorld).xyz;
        vertices[i].UV = Unpack_RG16_FLOAT(BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.UVsOffset));
        uint2 normalData = BufferLoad<uint2>(mesh.BufferIndex, vertexId, mesh.NormalsOffset);
        vertices[i].Normal = Unpack_RGB10A2_SNORM(normalData.x).xyz;
        vertices[i].Tangent = Unpack_RGB10A2_SNORM(normalData.y);
		if(mesh.ColorsOffset != ~0u)
			vertices[i].Color = BufferLoad<uint>(mesh.BufferIndex, vertexId, mesh.ColorsOffset);
		else
			vertices[i].Color = 0xFFFFFFFF;
	}

	float4 clipPos0 = mul(float4(positions[0], 1), cView.ViewProjection);
	float4 clipPos1 = mul(float4(positions[1], 1), cView.ViewProjection);
	float4 clipPos2 = mul(float4(positions[2], 1), cView.ViewProjection);
	float2 pixelClip = screenUV * 2 - 1;
	pixelClip.y *= -1;
	BaryDerivs bary = ComputeBarycentrics(pixelClip, clipPos0, clipPos1, clipPos2);

	VisBufferVertexAttribute outVertex;
	outVertex.UV = BaryInterpolate(vertices[0].UV, vertices[1].UV, vertices[2].UV, bary.Barycentrics);
    outVertex.Normal = normalize(mul(BaryInterpolate(vertices[0].Normal, vertices[1].Normal, vertices[2].Normal, bary.Barycentrics), (float3x3)instance.LocalToWorld));
	float4 tangent = BaryInterpolate(vertices[0].Tangent, vertices[1].Tangent, vertices[2].Tangent, bary.Barycentrics);
    outVertex.Tangent = float4(normalize(mul(tangent.xyz, (float3x3)instance.LocalToWorld)), tangent.w);
	outVertex.Color = vertices[0].Color;
    outVertex.Position = BaryInterpolate(positions[0], positions[1], positions[2], bary.Barycentrics);
	outVertex.DX = BaryInterpolate(vertices[0].UV, vertices[1].UV, vertices[2].UV, bary.DDX_Barycentrics);
	outVertex.DY = BaryInterpolate(vertices[0].UV, vertices[1].UV, vertices[2].UV, bary.DDY_Barycentrics);
	outVertex.Barycentrics = bary.Barycentrics;
	return outVertex;
}
