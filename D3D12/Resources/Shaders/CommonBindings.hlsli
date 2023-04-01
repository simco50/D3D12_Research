#pragma once

#include "Constants.hlsli"
#include "Interop/InteropMain.h"

//CBVs
ConstantBuffer<ViewUniforms> cView : 						register(b100);

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

template<typename T>
T BufferLoad(uint bufferIndex, uint elementIndex, uint byteOffset = 0)
{
	ByteAddressBuffer buffer = ResourceDescriptorHeap[NonUniformResourceIndex(bufferIndex)];
	return buffer.Load<T>(elementIndex * sizeof(T) + byteOffset);
}

float4 Sample2D(int index, SamplerState s, float2 uv, uint2 offset = 0)
{
	Texture2D tex = ResourceDescriptorHeap[index];
	return tex.Sample(s, uv, offset);
}

float4 SampleLevel2D(int index, SamplerState s, float2 uv, float level, uint2 offset = 0)
{
	Texture2D tex = ResourceDescriptorHeap[index];
	return tex.SampleLevel(s, uv, level, offset);
}

float4 SampleGrad2D(int index, SamplerState s, float2 uv,  float2 ddx, float2 ddy, uint2 offset = 0)
{
	Texture2D tex = ResourceDescriptorHeap[index];
	return tex.SampleGrad(s, uv, ddx, ddy, offset);
}

InstanceData GetInstance(uint index)
{
	StructuredBuffer<InstanceData> meshes = ResourceDescriptorHeap[cView.InstancesIndex];
	return meshes[NonUniformResourceIndex(index)];
}

MeshData GetMesh(uint index)
{
	StructuredBuffer<MeshData> meshes = ResourceDescriptorHeap[cView.MeshesIndex];
	return meshes[NonUniformResourceIndex(index)];
}

MaterialData GetMaterial(uint index)
{
	StructuredBuffer<MaterialData> materials = ResourceDescriptorHeap[cView.MaterialsIndex];
	return materials[NonUniformResourceIndex(index)];
}

Light GetLight(uint index)
{
	StructuredBuffer<Light> lights = ResourceDescriptorHeap[cView.LightsIndex];
	return lights[NonUniformResourceIndex(index)];
}

uint3 GetPrimitive(MeshData mesh, uint primitiveIndex)
{
	uint3 indices;
	if(mesh.IndexByteSize == 4)
	{
		indices = BufferLoad<uint3>(mesh.BufferIndex, primitiveIndex, mesh.IndicesOffset);
	}
	else
	{
		uint byteOffset = primitiveIndex * 3 * 2;
		uint alignedByteOffset = byteOffset & ~3;
		uint2 four16BitIndices = BufferLoad<uint2>(mesh.BufferIndex, 0, mesh.IndicesOffset + alignedByteOffset);

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
