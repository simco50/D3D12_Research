#pragma once

#include "Common.hlsli"

//CBVs
ConstantBuffer<ViewUniforms> cView : register(b100);

//SRVs
StructuredBuffer<Light> tLights :						   register(t5);
Texture2D tAO :											 register(t6);
Texture2D tDepth :										  register(t7);
Texture2D tPreviousSceneColor :							 register(t8);
Texture2D tSceneNormals :								   register(t9);
StructuredBuffer<MaterialData> tMaterials :				 register(t10);
StructuredBuffer<MeshData> tMeshes :						register(t11);
StructuredBuffer<MeshInstance> tMeshInstances :			 register(t12);

//Static samplers
SamplerState sLinearWrap :								  register(s10);
SamplerState sLinearClamp :								 register(s11);
SamplerState sLinearBorder :								register(s12);
SamplerState sPointWrap :								   register(s13);
SamplerState sPointClamp :								  register(s14);
SamplerState sPointBorder :								 register(s15);
SamplerState sAnisoWrap :								   register(s16);
SamplerState sAnisoClamp :								  register(s17);
SamplerState sAnisoBorder :								 register(s18);
SamplerState sMaterialSampler :							 register(s19);
SamplerComparisonState sDepthComparison :				   register(s20);
//Bindless samples
SamplerState sSamplerTable[] :							  register(s0, space100);
//Bindless SRVs
Texture2D tTexture2DTable[] :							   register(t0, space100);
Texture3D tTexture3DTable[] :							   register(t0, space101);
TextureCube tTextureCubeTable[] :						   register(t0, space102);
Texture2DArray tTexture2DArrayTable[] :					 register(t0, space103);
ByteAddressBuffer tBufferTable[] :						  register(t0, space104);
RaytracingAccelerationStructure tTLASTable[] :			  register(t0, space105);
//Bindless UAVs
RWByteAddressBuffer uRWBufferTable[] :					  register(u0, space100);
RWTexture2D<float4> URWTexture2DTable[] :				   register(u0, space101);
RWTexture3D<float4> URWTexture3DTable[] :				   register(u0, space102);

#define ROOT_SIG(elements) elements ", " DEFAULT_ROOT_SIG_PARAMS

#define DEFAULT_ROOT_SIG_PARAMS \
	"DescriptorTable(" \
		"SRV(t0, numDescriptors = unbounded, space = 100, offset = 0), " \
		"SRV(t0, numDescriptors = unbounded, space = 101, offset = 0), " \
		"SRV(t0, numDescriptors = unbounded, space = 102, offset = 0), " \
		"SRV(t0, numDescriptors = unbounded, space = 103, offset = 0), " \
		"SRV(t0, numDescriptors = unbounded, space = 104, offset = 0), " \
		"SRV(t0, numDescriptors = unbounded, space = 105, offset = 0), " \
		"SRV(t0, numDescriptors = unbounded, space = 106, offset = 0), " \
		"SRV(t0, numDescriptors = unbounded, space = 107, offset = 0), " \
		"SRV(t0, numDescriptors = unbounded, space = 108, offset = 0), " \
		"SRV(t0, numDescriptors = unbounded, space = 109, offset = 0), " \
		\
		"UAV(u0, numDescriptors = unbounded, space = 100, offset = 0), " \
		"UAV(u0, numDescriptors = unbounded, space = 101, offset = 0), " \
		"UAV(u0, numDescriptors = unbounded, space = 102, offset = 0), " \
		"UAV(u0, numDescriptors = unbounded, space = 103, offset = 0), " \
		"UAV(u0, numDescriptors = unbounded, space = 104, offset = 0), " \
		"UAV(u0, numDescriptors = unbounded, space = 105, offset = 0), " \
		"UAV(u0, numDescriptors = unbounded, space = 106, offset = 0), " \
		"UAV(u0, numDescriptors = unbounded, space = 107, offset = 0), " \
		"UAV(u0, numDescriptors = unbounded, space = 108, offset = 0), " \
		"UAV(u0, numDescriptors = unbounded, space = 109, offset = 0), " \
	"visibility=SHADER_VISIBILITY_ALL), " \
	"DescriptorTable(" \
		"Sampler(s0, numDescriptors = unbounded, space = 100, offset = 0) ," \
	"visibility=SHADER_VISIBILITY_ALL), " \
	"StaticSampler(s10, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_WRAP, addressV=TEXTURE_ADDRESS_WRAP, addressW=TEXTURE_ADDRESS_WRAP), " \
	"StaticSampler(s11, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP), " \
	"StaticSampler(s12, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_BORDER, addressV=TEXTURE_ADDRESS_BORDER, addressW=TEXTURE_ADDRESS_BORDER), " \
	"StaticSampler(s13, filter=FILTER_MIN_MAG_MIP_POINT, addressU=TEXTURE_ADDRESS_WRAP, addressV=TEXTURE_ADDRESS_WRAP, addressW=TEXTURE_ADDRESS_WRAP), " \
	"StaticSampler(s14, filter=FILTER_MIN_MAG_MIP_POINT, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP), " \
	"StaticSampler(s15, filter=FILTER_MIN_MAG_MIP_POINT, addressU=TEXTURE_ADDRESS_BORDER, addressV=TEXTURE_ADDRESS_BORDER, addressW=TEXTURE_ADDRESS_BORDER), " \
	"StaticSampler(s16, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, addressU=TEXTURE_ADDRESS_WRAP, addressV=TEXTURE_ADDRESS_WRAP, addressW=TEXTURE_ADDRESS_WRAP), " \
	"StaticSampler(s17, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP), " \
	"StaticSampler(s18, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, addressU=TEXTURE_ADDRESS_BORDER, addressV=TEXTURE_ADDRESS_BORDER, addressW=TEXTURE_ADDRESS_BORDER), " \
	"StaticSampler(s19, filter=FILTER_ANISOTROPIC, maxAnisotropy = 4, addressU=TEXTURE_ADDRESS_WRAP, addressV=TEXTURE_ADDRESS_WRAP, addressW=TEXTURE_ADDRESS_WRAP), " \
	"StaticSampler(s20, filter=FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, comparisonFunc=COMPARISON_GREATER, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP)" \

template<typename T>
T BufferLoad(uint bufferIndex, uint elementIndex, uint byteOffset = 0)
{
	ByteAddressBuffer buffer = tBufferTable[bufferIndex];
	return buffer.Load<T>(elementIndex * sizeof(T) + byteOffset);
}

float4 Sample2D(int index, SamplerState s, float2 uv, uint2 offset = 0)
{
	return tTexture2DTable[index].Sample(s, uv, offset);
}

float4 SampleLevel2D(int index, SamplerState s, float2 uv, float level, uint2 offset = 0)
{
	return tTexture2DTable[index].SampleLevel(s, uv, level, offset);
}

float4 SampleGrad2D(int index, SamplerState s, float2 uv,  float2 ddx, float2 ddy, uint2 offset = 0)
{
	return tTexture2DTable[index].SampleGrad(s, uv, ddx, ddy, offset);
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

MeshInstance GetMeshInstance(uint index)
{
	StructuredBuffer<MeshInstance> meshes = ResourceDescriptorHeap[cView.MeshInstancesIndex];
	return meshes[index];
}

MeshData GetMesh(uint index)
{
	StructuredBuffer<MeshData> meshes = ResourceDescriptorHeap[cView.MeshesIndex];
	return meshes[index];
}

MaterialData GetMaterial(uint index)
{
	StructuredBuffer<MaterialData> materials = ResourceDescriptorHeap[cView.MaterialsIndex];
	return materials[index];
}

Light GetLight(uint index)
{
	StructuredBuffer<Light> lights = ResourceDescriptorHeap[cView.LightsIndex];
	return lights[index];
}
