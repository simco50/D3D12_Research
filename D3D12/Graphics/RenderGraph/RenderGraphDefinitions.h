#pragma once

#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/Buffer.h"

enum class RGResourceType
{
	Texture,
	Buffer,
};
template<typename T> struct RGResourceTypeTraits { };
template<> struct RGResourceTypeTraits<Texture> { constexpr static RGResourceType Type = RGResourceType::Texture; };
template<> struct RGResourceTypeTraits<Buffer> { constexpr static RGResourceType Type = RGResourceType::Buffer; };

class RGPass;

struct RGResource
{
	RGResource(const char* pName, int id, const TextureDesc& desc, Texture* pResource = nullptr)
		: Name(pName), Id(id), IsImported(!!pResource), Type(RGResourceType::Texture), pResourceReference(pResource), DescTexture(desc), pResource(pResource)
	{}

	RGResource(const char* pName, int id, const BufferDesc& desc, Buffer* pResource = nullptr)
		: Name(pName), Id(id), IsImported(!!pResource), Type(RGResourceType::Buffer), pResourceReference(pResource), DescBuffer(desc), pResource(pResource)
	{}

	void SetResource(RefCountPtr<GraphicsResource> resource)
	{
		pResourceReference = resource;
		pResource = resource;
	}

	void Release()
	{
		pResourceReference = nullptr;
		// pResource keeps a raw reference to use during execution
	}

	const char* Name;
	int Id;
	bool IsImported;
	bool IsExported = false;
	RGResourceType Type;
	RefCountPtr<GraphicsResource> pResourceReference;
	GraphicsResource* pResource = nullptr;

	template<typename T>
	T* GetRHI() const
	{
		checkf(Type == RGResourceTypeTraits<T>::Type, "Provided type does not match resource type");
		check(pResource);
		return static_cast<T*>(pResource);
	}

	union
	{
		TextureDesc DescTexture;
		BufferDesc DescBuffer;
	};

	RGPass* pFirstAccess = nullptr;
	RGPass* pLastAccess = nullptr;
};

template<typename T>
struct RGResourceT : RGResource
{
	RGResourceT(const char* pName, int id, const TextureDesc& desc, Texture* pResource = nullptr)
		: RGResource(pName, id, desc, pResource)
	{}

	RGResourceT(const char* pName, int id, const BufferDesc& desc, Buffer* pResource = nullptr)
		: RGResource(pName, id, desc, pResource)
	{}

	T* Get()
	{
		return GetRHI<T>();
	}
};

using RGTexture = RGResourceT<Texture>;
using RGBuffer = RGResourceT<Buffer>;
