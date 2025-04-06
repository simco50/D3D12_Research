#pragma once

#define DEFINE_CONSTANTS(name, slot) 									\
	StructuredBuffer<name> t##name : register(t##slot, space100);		\
	static const name c##name = t##name[0]

#define DEFINE_CONSTANTS_AS(name, alias, slot) 							\
	StructuredBuffer<name> t##name : register(t##slot, space100);		\
	static const name alias = t##name[0]


// Generic descriptor handle
struct DescriptorHandleBase
{
	bool IsValid() { return Index != 0xFFFFFFFF; }
	uint GetIndex() { return Index; }
	uint Index;
};

// Typed generic descriptor handle
template<typename ResourceType>
struct DescriptorHandleT : DescriptorHandleBase
{
    ResourceType Get()
	{
		ResourceType resource = ResourceDescriptorHeap[NonUniformResourceIndex(GetIndex())];
		return resource;
	}
};

// Internal: Base class for texture handles
template<typename ResourceType, typename StorageType, uint NumDimensions>
struct TextureBaseH_T : DescriptorHandleT<ResourceType>
{
    using DescriptorHandleT<ResourceType>::Get;

	StorageType operator[](vector<uint, NumDimensions> texel)
	{
		return Get()[texel];
	}

	StorageType Load(vector<uint, NumDimensions + 1> location)
	{
		return Get().Load(location);
	}
};

// Internal: Base class for read-only texture handles
template<typename ResourceType, typename StorageType, uint NumDimensions>
struct TextureH_T : TextureBaseH_T<ResourceType, StorageType, NumDimensions>
{
    using TextureBaseH_T<ResourceType, StorageType, NumDimensions>::Get;

	StorageType SampleLevel(SamplerState samplerState, vector<float, NumDimensions> uv, float mipLevel, vector<int, NumDimensions> offset)
	{
		return Get().SampleLevel(samplerState, uv, mipLevel, offset);
	}

	StorageType SampleLevel(SamplerState samplerState, vector<float, NumDimensions> uv, float mipLevel)
	{
		return Get().SampleLevel(samplerState, uv, mipLevel);
	}
	
	StorageType SampleGrad(SamplerState samplerState, vector<float, NumDimensions> uv, vector<float, NumDimensions> dd_x, vector<float, NumDimensions> dd_y, vector<int, NumDimensions> offset = 0)
	{
		return Get().SampleGrad(samplerState, uv, dd_x, dd_y, offset);
	}

	StorageType Sample(SamplerState samplerState, vector<float, NumDimensions> uv, vector<int, NumDimensions> offset = 0)
	{
		return Get().Sample(samplerState, uv, offset);
	}
};

// Define all read-only texture types
template<typename StorageType> struct Texture1DH 		: TextureH_T<Texture1D<StorageType>, StorageType, 1> {};
template<typename StorageType> struct Texture2DH 		: TextureH_T<Texture2D<StorageType>, StorageType, 2> {};
template<typename StorageType> struct Texture3DH 		: TextureH_T<Texture3D<StorageType>, StorageType, 3> {};
template<typename StorageType> struct TextureCubeH 		: TextureH_T<TextureCube<StorageType>, StorageType, 3> {};
template<typename StorageType> struct Texture1DArrayH 	: TextureH_T<Texture1DArray<StorageType>, StorageType, 2> {};
template<typename StorageType> struct Texture2DArrayH 	: TextureH_T<Texture2DArray<StorageType>, StorageType, 3> {};
template<typename StorageType> struct TextureCubeArrayH : TextureH_T<Texture2DArray<StorageType>, StorageType, 4> {};

// Internal: Base class for read-write texture handles
template<typename ResourceType, typename StorageType, uint NumDimensions>
struct RWTextureH_T : TextureBaseH_T<ResourceType, StorageType, NumDimensions>
{
    using TextureBaseH_T<ResourceType, StorageType, NumDimensions>::Get;

	void Store(vector<uint, NumDimensions> texel, StorageType value)
	{
		Get()[texel] = value;
	}
};

// Define all read-write texture types
template<typename StorageType> struct RWTexture1DH 		: RWTextureH_T<RWTexture1D<StorageType>, StorageType, 1> {};
template<typename StorageType> struct RWTexture2DH 		: RWTextureH_T<RWTexture2D<StorageType>, StorageType, 2> {};
template<typename StorageType> struct RWTexture3DH 		: RWTextureH_T<RWTexture3D<StorageType>, StorageType, 3> {};
template<typename StorageType> struct RWTexture1DArrayH : RWTextureH_T<RWTexture1DArray<StorageType>, StorageType, 2> {};
template<typename StorageType> struct RWTexture2DArrayH : RWTextureH_T<RWTexture2DArray<StorageType>, StorageType, 3> {};

// Internal: Base class for read-only typed buffers (Structured/Typed)
template<typename ResourceType, typename StorageType>
struct BufferH_T : DescriptorHandleT<ResourceType>
{
    using DescriptorHandleT<ResourceType>::Get;

	StorageType operator[](uint index)
	{
		return Load(index);
	}

	StorageType Load(uint index)
	{
		return Get()[index];
	}
};

// Define all read-only typed buffer types
template<typename StorageType> struct TypedBufferH 		: BufferH_T<Buffer<StorageType>, StorageType> {};
template<typename StorageType> struct StructuredBufferH : BufferH_T<StructuredBuffer<StorageType>, StorageType> {};

// Internal: Base class for read-write typed buffers (Structured/Typed)
template<typename ResourceType, typename StorageType>
struct RWBufferH_T : BufferH_T<ResourceType, StorageType>
{
	using DescriptorHandleT<ResourceType>::Get;

	void Store(uint index, StorageType value)
	{
		Get()[index] = value;
	}
};

// Define all read-write typed buffer types
template<typename StorageType> struct RWTypedBufferH 		: RWBufferH_T<RWBuffer<StorageType>, StorageType> {};
template<typename StorageType> struct RWStructuredBufferH 	: RWBufferH_T<RWStructuredBuffer<StorageType>, StorageType> {};

template<typename ResourceType>
struct ByteBufferH_T : DescriptorHandleT<ResourceType>
{
	using DescriptorHandleT<ResourceType>::Get;

	template<typename T>
	T Load(uint byteOffset)
	{
		return Get().template Load<T>(byteOffset);
	}

	template<typename T>
	T LoadStructure(uint elementIndex, uint startByteOffset)
	{
		return Load<T>(elementIndex * sizeof(T) + startByteOffset);
	}
};

// Read-only ByteBuffer handle
struct ByteBufferH : ByteBufferH_T<ByteAddressBuffer>
{
	using ByteBufferH_T<ByteAddressBuffer>::Get;
};

// Read-write ByteBuffer handle
struct RWByteBufferH : ByteBufferH_T<RWByteAddressBuffer>
{
	using ByteBufferH_T<RWByteAddressBuffer>::Get;

	template<typename T>
	void Store(uint byteOffset, T data)
	{
		Get().Store<T>(byteOffset, data);
	}

	template<typename T>
	void StoreStructure(uint elementIndex, uint startByteOffset, T data)
	{
		Store<T>(elementIndex * sizeof(T) + startByteOffset, data);
	}
};

// Sampler handle
struct SamplerH : DescriptorHandleT<SamplerState>
{
	using DescriptorHandleT<SamplerState>::Get;
};

// ComparisonSampler handle
struct SamplerComparisonH : DescriptorHandleT<SamplerComparisonState>
{
	using DescriptorHandleT<SamplerComparisonState>::Get;
};

// RaytracingAccelerationStructure handle
struct TLASH : DescriptorHandleT<RaytracingAccelerationStructure>
{
	using DescriptorHandleT<RaytracingAccelerationStructure>::Get;
};