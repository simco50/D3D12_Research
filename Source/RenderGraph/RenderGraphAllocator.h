#pragma once

#include <imgui_internal.h>

#include "IconsFontAwesome4.h"
#include "RenderGraphDefinitions.h"

class RGHeap;

// Represents a physical resources
class RGPhysicalResource
{
public:
	Ref<DeviceResource> pResource;
	uint32				PaddingBegin = 0;
	uint32				Offset;
	uint32				Size;
	uint32				LastUsedFrame = 0;
	URange				LifeTime;
	TextureDesc			ResourceTextureDesc;
	BufferDesc			ResourceBufferDesc;
	RGHeap*				pHeap = nullptr;
	bool				IsAllocated = false;

	bool IsCompatible(const RGResource* pOtherResource) const
	{
		if (pOtherResource->GetType() == RGResourceType::Texture)
			return ResourceTextureDesc == static_cast<const RGTexture*>(pOtherResource)->GetDesc();
		return ResourceBufferDesc == static_cast<const RGBuffer*>(pOtherResource)->GetDesc();
	}
};


// Represents a physical resource heap
class RGHeap
{
public:
	RGHeap(GraphicsDevice* pDevice, uint32 size);
	RGPhysicalResource* AllocateTexture(const char* pName, const TextureDesc& textureDesc);
	RGPhysicalResource* AllocateBuffer(const char* pName, const BufferDesc& bufferDesc);
	void				ReleaseResource(RGPhysicalResource* pResource);

	RGPhysicalResource* Find(const DeviceResource* pDeviceResource)
	{
		for (UniquePtr<RGPhysicalResource>& pResource : PhysicalResources)
		{
			if (pResource->pResource == pDeviceResource)
				return pResource.get();
		}
		return nullptr;
	}

	friend class RGResourceAllocator;

private:
	struct Allocation
	{
		uint32 Size;
		uint32 Offset;
		uint32 PaddingBegin;
	};

	using RangeIndex						  = uint32;
	static constexpr RangeIndex sInvalidRange = ~0u;

	struct ResourceRange
	{
		uint32				Offset;
		uint32				Size;
		RangeIndex			PrevRange = sInvalidRange;
		RangeIndex			NextRange = sInvalidRange;
		RGPhysicalResource* pResource = nullptr;
	};

	void Release(const Allocation& allocation, RGPhysicalResource* pResource);
	bool Allocate(uint32 size, uint32 alignment, Allocation& outAllocation, Array<RGPhysicalResource*>& outOverlaps);

	RangeIndex GetFirstRange() const
	{
		return Ranges[FrontIndex].NextRange;
	}

	RangeIndex InsertRange(RangeIndex prevIndex, RGPhysicalResource* pResource, uint32 offset, uint32 size);
	void	   RemoveRange(RangeIndex index);
	RangeIndex NewRange();
	void	   VerifyState() const;

	GraphicsDevice*						 pDevice;
	Array<ResourceRange>				 Ranges;
	Array<RangeIndex>					 FreeList;
	uint32								 FrameIndex	   = 0;
	RangeIndex							 FrontIndex	   = sInvalidRange;
	uint32								 AllocatedSize = 0;
	uint32								 Size;
	uint32								 LastUsedFrame = 0;
	Array<UniquePtr<RGPhysicalResource>> PhysicalResources;
	Ref<ID3D12Heap>						 pHeap;
};


class RGResourceAllocator
{
public:
	void				Init(GraphicsDevice* pDevice);
	void				Shutdown();

	void				AllocateResource(RGResource* pResource);
	void				ReleaseResource(RGResource* pResource);

	void				Tick();
	void				ClearAll();
	void				DrawDebugView(URange lifetimeRange) const;

private:
	RGPhysicalResource* FindExistingResource(const DeviceResource* pResource);

	GraphicsDevice*			 m_pDevice			 = nullptr;
	bool					 m_LiveCapture		 = true;
	bool					 m_AllocateResources = true;
	uint32					 m_FrameIndex		 = 0;
	Array<UniquePtr<RGHeap>> m_Heaps;

	static constexpr uint32 cHeapCleanupLatency		= 3;
	static constexpr uint32 cResourceCleanupLatency = 120;
	static constexpr uint32 cHeapAlignment			= 128 * Math::MegaBytesToBytes;
};

extern RGResourceAllocator gRenderGraphAllocator;
