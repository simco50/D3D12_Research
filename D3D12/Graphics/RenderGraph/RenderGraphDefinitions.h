#pragma once

#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/Buffer.h"

enum class RGResourceType
{
	Texture,
	Buffer,
};
template<typename T> struct RGResourceTypeTraits { };

template<>
struct RGResourceTypeTraits<Texture>
{
	constexpr static RGResourceType Type = RGResourceType::Texture;
	using TDesc = TextureDesc;
};

template<>
struct RGResourceTypeTraits<Buffer>
{
	constexpr static RGResourceType Type = RGResourceType::Buffer;
	using TDesc = BufferDesc;
};

class RGPass;
class RGGraph;

class RGResource
{
public:
	friend class RGGraph;
	friend class RGPass;

	RGResource(const char* pName, int id, RGResourceType type, GraphicsResource* pResource = nullptr)
		: Name(pName), ID(id), IsImported(!!pResource), Type(type), pResourceReference(pResource), pResource(pResource)
	{}

	const char* GetName() const { return Name; }
	GraphicsResource* GetRaw() const { return pResource; }

protected:
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
	int ID;
	bool IsImported;
	bool IsExported = false;
	RGResourceType Type;
	RefCountPtr<GraphicsResource> pResourceReference;
	GraphicsResource* pResource = nullptr;
	const RGPass* pLastAccess = nullptr;
};

template<typename T>
struct RGResourceT : public RGResource
{
public:
	friend class RGGraph;
	using TDesc = typename RGResourceTypeTraits<T>::TDesc;

	RGResourceT(const char* pName, int id, const TDesc& desc, T* pResource = nullptr)
		: RGResource(pName, id, RGResourceTypeTraits<T>::Type, pResource), Desc(desc)
	{}

	T* Get() const
	{
		check(pResource);
		return static_cast<T*>(pResource);
	}

	const TDesc& GetDesc() const { return Desc; }

private:
	TDesc Desc;
};

using RGTexture = RGResourceT<Texture>;
using RGBuffer = RGResourceT<Buffer>;
