#pragma once

#include "Constants.hlsli"
#include "Bindless.hlsli"
#include "Interop/ShaderInterop.h"
#include "Packing.hlsli"

// Constants
DEFINE_CONSTANTS_AS(ViewUniforms, cView, 2);

//Static samplers
SamplerState sLinearWrap :								  	register(s0,  space1);
SamplerState sLinearClamp :								 	register(s1,  space1);
SamplerState sLinearBorder :								register(s2,  space1);
SamplerState sPointWrap :								   	register(s3,  space1);
SamplerState sPointClamp :								  	register(s4,  space1);
SamplerState sPointBorder :								 	register(s5,  space1);
SamplerState sAnisoWrap :								   	register(s6,  space1);
SamplerState sAnisoClamp :								  	register(s7,  space1);
SamplerState sAnisoBorder :									register(s8,  space1);
SamplerState sMaterialSampler :							 	register(s9,  space1);
SamplerComparisonState sLinearClampComparisonGreater :		register(s10, space1);
SamplerComparisonState sLinearWrapComparisonGreater :		register(s11, space1);

InstanceData GetInstance(uint index)
{
	return cView.InstancesBuffer[index];
}

MeshData GetMesh(uint index)
{
	return cView.MeshesBuffer[index];
}

MaterialData GetMaterial(uint index)
{
	return cView.MaterialsBuffer[index];
}

Light GetLight(uint index)
{
	return cView.LightsBuffer[index];
}

uint3 GetPrimitive(MeshData mesh, uint primitiveIndex)
{
	uint3 indices;
	if(mesh.IndexByteSize == 4)
	{
		indices = mesh.DataBuffer.LoadStructure<uint3>(primitiveIndex, mesh.IndicesOffset);
	}
	else
	{
		uint byteOffset = primitiveIndex * 3 * 2;
		uint alignedByteOffset = byteOffset & ~3;
		uint2 four16BitIndices = mesh.DataBuffer.LoadStructure<uint2>(0, mesh.IndicesOffset + alignedByteOffset);

		if (byteOffset == alignedByteOffset)
		{
			indices.x = four16BitIndices.x & 0xffff;
			indices.y = four16BitIndices.x >> 16;
			indices.z = four16BitIndices.y & 0xffff;
		}
		else
		{
			indices.x = four16BitIndices.x >> 16;
			indices.y = four16BitIndices.y & 0xffff;
			indices.z = four16BitIndices.y >> 16;
		}
	}
	return indices;
}

struct Vertex
{
	float3 Position;
	float2 UV;
	float3 Normal;
	float4 Tangent;
	uint Color;
};

Vertex LoadVertex(MeshData mesh, uint vertexId)
{
	Vertex vertex;
	vertex.Position = mesh.DataBuffer.LoadStructure<float3>(vertexId, mesh.PositionsOffset);
	vertex.UV = RG16_FLOAT::Unpack(mesh.DataBuffer.LoadStructure<uint>(vertexId, mesh.UVsOffset));

	uint2 normalData = mesh.DataBuffer.LoadStructure<uint2>(vertexId, mesh.NormalsOffset);
	vertex.Normal = RGB10A2_SNORM::Unpack(normalData.x).xyz;
	vertex.Tangent = RGB10A2_SNORM::Unpack(normalData.y);

	vertex.Color = 0xFFFFFFFF;
	if(mesh.ColorsOffset != ~0u)
		vertex.Color = mesh.DataBuffer.LoadStructure<uint>(vertexId, mesh.ColorsOffset);
	return vertex;
}
