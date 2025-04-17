#pragma once

#include "RenderGraphDefinitions.h"

class RGResourceAllocator
{
private:
	// Represents a physical resources
	struct RGPhysicalResource
	{
		String				Name;
		Ref<DeviceResource> pResource;
		uint32				Offset;
		uint32				Size;
		uint32				LastUsedFrame = 0;
		RGResourceType		Type;
		TextureDesc			ResourceTextureDesc;
		BufferDesc			ResourceBufferDesc;
		URange				Lifetime;
		bool				IsExternal = false;

		URange GetMemoryRange() const
		{
			return URange(Offset, Offset + Size);
		}

		bool IsCompatible(const RGResource* pOtherResource) const
		{
			if (pOtherResource->GetType() != Type)
				return false;

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
		~RGHeap();

		bool						TryAllocate(GraphicsDevice* pDevice, uint32 frameIndex, RGResource* pResource);
		void						FreeUnused(uint32 frameIndex);
		bool						IsUnused(uint32 frameIndex) const;
		void						UpdateExternals();

		uint32						GetUsedSize() const;
		uint32						GetSize() const { return Size; }
		Span<RGPhysicalResource*>	GetAllocations() const { return Allocations; }
		uint32						GetNumResources() const { return (uint32)Allocations.size() + (uint32)ResourceCache.size(); }

	private:
		uint32						LastUsedFrame = 0;
		uint32						Size = 0;
		Ref<ID3D12Heap>				pHeap;

		Array<RGPhysicalResource*>	ResourceCache;
		Array<RGPhysicalResource*>	Allocations;

		// Keep track of which memory ranges in the heap for the lifetime of the current resource are free
		struct HeapOffset
		{
			uint32 Offset	   : 31;
			uint32 IsFreeBegin : 1;
		};
		mutable Array<HeapOffset>	FreeRanges;
	};

public:
	void						Init(GraphicsDevice* pDevice);
	void						Shutdown();
	void						AllocateResources(Span<RGResource*> graphResources);
	void						Tick();

	void						DrawDebugView(bool& enabled) const;

private:
	void						ClearUnusedResources();
	RGPhysicalResource*			FindAllocation(const DeviceResource* pResource, RGHeap** pOutHeap);


	GraphicsDevice*				m_pDevice		= nullptr;
	uint32						m_FrameIndex	= 0;
	Array<UniquePtr<RGHeap>>	m_Heaps;

};

extern RGResourceAllocator gRenderGraphAllocator;
