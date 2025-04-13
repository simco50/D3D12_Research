#pragma once

#include <imgui_internal.h>

#include "IconsFontAwesome4.h"
#include "RenderGraphDefinitions.h"

class RGAllocator
{
private:
	// Represents a physical resources
	struct RGPhysicalResource
	{
		Ref<DeviceResource> pResource;
		uint32				Offset;
		uint32				Size;
		uint32				LastUsedFrame = 0;
		TextureDesc			ResourceTextureDesc;
		BufferDesc			ResourceBufferDesc;

		bool IsCompatible(const RGResource* pOtherResource) const
		{
			if (pOtherResource->GetType() == RGResourceType::Texture)
				return ResourceTextureDesc == static_cast<const RGTexture*>(pOtherResource)->GetDesc();
			return ResourceBufferDesc == static_cast<const RGBuffer*>(pOtherResource)->GetDesc();
		}
	};

	// Represents a physical resource heap
	struct RGHeap
	{
		uint32							   ID;
		Array<RGResource*>				   Allocations;
		uint32							   Size;
		uint32							   LastUsedFrame = 0;

		Array<RGPhysicalResource*>			ExternalResources;
		Array<UniquePtr<RGPhysicalResource>> PhysicalResources;
		Ref<ID3D12Heap>					   pHeap;

		// Keep track of which memory ranges in the heap for the lifetime of the current resource are free
		struct HeapOffset
		{
			uint32 Offset	   : 31;
			uint32 IsFreeBegin : 1;
		};
		Array<HeapOffset> FreeRanges;
	};

public:
	void				Init(GraphicsDevice* pDevice);
	void				Shutdown();
	void				AllocateResources(Span<RGResource*> graphResources);
	void				Tick();
	void				ClearAll();

private:
	void				ClearUnusedResources(Array<RGHeap>& heaps);
	RGPhysicalResource* FindExistingResource(const DeviceResource* pResource, RGHeap** pOutHeap);
	bool				TryPlaceResourceInHeap(RGHeap& heap, RGResource* pResource) const;
	void				AllocateResources(Array<RGResource*>& resources, Array<RGHeap>& heaps);

	void				DrawDebugView(Span<RGResource*> resources, const Array<RGHeap>& heaps) const;

	GraphicsDevice*		m_pDevice			= nullptr;
	bool				m_LiveCapture		= true;
	bool				m_AllocateResources = true;
	uint32				m_FrameIndex		= 0;
	Array<RGHeap>		m_Heaps;
	uint32				m_HeapID = 0;

	static constexpr uint32 cHeapCleanupLatency		= 3;
	static constexpr uint32 cResourceCleanupLatency = 120;
	static constexpr uint32 cHeapAlignment			= 32 * Math::MegaBytesToBytes;

};

extern RGAllocator gRenderGraphAllocator;
