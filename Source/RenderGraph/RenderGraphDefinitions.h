#pragma once

#include "RHI/Texture.h"
#include "RHI/Buffer.h"

class RGGraph;
class RGPass;
class RGGraph;
class RGResource;
class RGPhysicalResource;

enum class RGResourceType : uint8
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

template<typename ObjectType, typename BackingType>
class RGHandle
{
public:
	RGHandle() = default;

	explicit RGHandle(BackingType id)
		: mID(id)
	{}

	uint16 GetIndex() const { return mID; }
	bool IsValid() const { return mID != Invalid; }

	bool operator==(const RGHandle& rhs) const { return mID == rhs.mID; }
	bool operator!=(const RGHandle& rhs) const { return mID != rhs.mID; }

private:
	BackingType mID = Invalid;

	static constexpr BackingType Invalid = std::numeric_limits<BackingType>::max();
};


using RGPassID = RGHandle<RGPass, uint16>;
using RGResourceID = RGHandle<RGResource, uint16>;

class RGResource
{
public:
	friend class RGGraph;
	friend class RGPass;
	friend class RGResourceAllocator;

	RGResource(const char* pName, RGResourceID id, RGResourceType type, DeviceResource* pPhysicalResource = nullptr)
		: pName(pName), ID(id), Allocated(false), IsImported(!!pPhysicalResource), IsExported(false), Type((uint32)type), pPhysicalResource(nullptr)
	{
		if (pPhysicalResource)
			SetResource(pPhysicalResource);
	}

	~RGResource()
	{
	}

	const char*			GetName() const				{ return pName; }
	DeviceResource*		GetPhysicalUnsafe() const	{ return pPhysicalResource; }
	RGResourceType		GetType() const				{ return (RGResourceType)Type; }
	bool				IsAllocated() const			{ return Allocated; }
	URange				GetLifetime() const			{ return URange(FirstAccess.GetIndex(), LastAccess.GetIndex() + 1); }
	URange				GetLifetimeActual() const
	{
		URange lifetime = GetLifetime();
		if (IsExported)
			lifetime.End = 0xFFFFFFFF;
		if (IsImported)
			lifetime.Begin = 0;
		return lifetime;
	}

protected:
	void SetResource(DeviceResource* resource)
	{
		gAssert(!pPhysicalResource);
		pPhysicalResource = resource;
		Allocated = true;
	}

	void Release()
	{
		gAssert(pPhysicalResource);
		gAssert(Allocated);
		Allocated = false;
	}

	const char*				pName;
	DeviceResource*			pPhysicalResource = nullptr;
	RGPhysicalResource*		pRGPhysicalResource = nullptr;

	RGResourceID			ID;
	uint32					Allocated			: 1;
	uint32					IsImported			: 1;
	uint32					IsExported			: 1;
	uint32					Type				: 1;

	// Compile-time data
	RGPassID				FirstAccess;			///< First non-culled pass that accesses this resource
	RGPassID				LastAccess;				///< Last non-culled pass that accesses this resource
	RGPassID				LastWrite;				///< Last pass that wrote to this resource. Used for pass culling
};

template<typename T>
struct RGResourceT : public RGResource
{
public:
	friend class RGGraph;
	using TDesc = typename RGResourceTypeTraits<T>::TDesc;

	RGResourceT(const char* pName, RGResourceID id, const TDesc& desc, T* pPhysicalResource = nullptr)
		: RGResource(pName, id, RGResourceTypeTraits<T>::Type, pPhysicalResource), Desc(desc)
	{}

	const TDesc& GetDesc() const { return Desc; }

private:
	TDesc Desc;
};

using RGTexture = RGResourceT<Texture>;
using RGBuffer = RGResourceT<Buffer>;
