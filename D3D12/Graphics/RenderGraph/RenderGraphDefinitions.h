#pragma once

#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/Buffer.h"

class RGGraph;

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

	RGResource(const char* pName, int id, RGResourceType type, GraphicsResource* pPhysicalResource = nullptr)
		: pName(pName), ID(id), IsImported(!!pPhysicalResource), Type(type), pPhysicalResource(pPhysicalResource)
	{
	}

	const char* GetName() const { return pName; }
	GraphicsResource* GetPhysical() const { return pPhysicalResource; }

protected:
	void SetResource(RefCountPtr<GraphicsResource> resource)
	{
		pPhysicalResource = resource;

		// Add a reference to tell the resource allocator it can't currently be re-used.
		pPhysicalResource->AddRef();
	}

	void Release()
	{
		// Remove reference to tell the resource allocator it can be returned to the pool.
		pPhysicalResource->Release();
	}

	const char* pName;
	int ID;
	bool IsImported;
	bool IsExported = false;
	RGResourceType Type;
	GraphicsResource* pPhysicalResource = nullptr;
	const RGPass* pFirstAccess = nullptr;
	const RGPass* pLastAccess = nullptr;
};

template<typename T>
struct RGResourceT : public RGResource
{
public:
	friend class RGGraph;
	using TDesc = typename RGResourceTypeTraits<T>::TDesc;

	RGResourceT(const char* pName, int id, const TDesc& desc, T* pPhysicalResource = nullptr)
		: RGResource(pName, id, RGResourceTypeTraits<T>::Type, pPhysicalResource), Desc(desc)
	{}

	T* Get() const
	{
		check(pPhysicalResource);
		return static_cast<T*>(pPhysicalResource);
	}

	const TDesc& GetDesc() const { return Desc; }

private:
	TDesc Desc;
};

using RGTexture = RGResourceT<Texture>;
using RGBuffer = RGResourceT<Buffer>;
