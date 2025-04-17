#include "stdafx.h"
#include "RenderGraphAllocator.h"
#include "Core/Profiler.h"
#include "RHI/Device.h"

#include <imgui_internal.h>

RGResourceAllocator gRenderGraphAllocator;

static constexpr uint32 cHeapCleanupLatency		= 3;
static constexpr uint32 cResourceCleanupLatency = 120;
static constexpr uint32 cHeapAlignment			= 32 * Math::MegaBytesToBytes;

static D3D12_RESOURCE_DESC sGetResourceDesc(const RGResource* pResource)
{
	if (pResource->GetType() == RGResourceType::Texture)
	{
		const RGTexture*   pTexture = (RGTexture*)pResource;
		const TextureDesc& desc		= pTexture->GetDesc();

		auto GetResourceDesc = [](const TextureDesc& textureDesc) {
			DXGI_FORMAT format = D3D::ConvertFormat(textureDesc.Format);

			D3D12_RESOURCE_DESC desc{};
			switch (textureDesc.Type)
			{
			case TextureType::Texture1D:
			case TextureType::Texture1DArray:
				desc = CD3DX12_RESOURCE_DESC::Tex1D(format, textureDesc.Width, (uint16)textureDesc.ArraySize, (uint16)textureDesc.Mips, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
				break;
			case TextureType::Texture2D:
			case TextureType::Texture2DArray:
				desc = CD3DX12_RESOURCE_DESC::Tex2D(format, textureDesc.Width, textureDesc.Height, (uint16)textureDesc.ArraySize, (uint16)textureDesc.Mips, textureDesc.SampleCount, 0, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
				break;
			case TextureType::TextureCube:
			case TextureType::TextureCubeArray:
				desc = CD3DX12_RESOURCE_DESC::Tex2D(format, textureDesc.Width, textureDesc.Height, (uint16)textureDesc.ArraySize * 6, (uint16)textureDesc.Mips, textureDesc.SampleCount, 0, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
				break;
			case TextureType::Texture3D:
				desc = CD3DX12_RESOURCE_DESC::Tex3D(format, textureDesc.Width, textureDesc.Height, (uint16)textureDesc.Depth, (uint16)textureDesc.Mips, D3D12_RESOURCE_FLAG_NONE, D3D12_TEXTURE_LAYOUT_UNKNOWN);
				break;
			default:
				gUnreachable();
				break;
			}

			if (EnumHasAnyFlags(textureDesc.Flags, TextureFlag::UnorderedAccess))
			{
				desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			}
			if (EnumHasAnyFlags(textureDesc.Flags, TextureFlag::RenderTarget))
			{
				desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			}
			if (EnumHasAnyFlags(textureDesc.Flags, TextureFlag::DepthStencil))
			{
				desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
				if (!EnumHasAnyFlags(textureDesc.Flags, TextureFlag::ShaderResource))
				{
					// I think this can be a significant optimization on some devices because then the depth buffer can never be (de)compressed
					desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
				}
			}
			return desc;
		};

		return GetResourceDesc(desc);
	}
	else
	{
		const RGBuffer*	 pBuffer	= (RGBuffer*)pResource;
		const BufferDesc bufferDesc = pBuffer->GetDesc();

		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferDesc.Size, D3D12_RESOURCE_FLAG_NONE);
		if (EnumHasAnyFlags(bufferDesc.Flags, BufferFlag::UnorderedAccess))
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		if (EnumHasAnyFlags(bufferDesc.Flags, BufferFlag::AccelerationStructure))
			desc.Flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
		return desc;
	}
}


RGResourceAllocator::RGHeap::RGHeap(GraphicsDevice* pDevice, uint32 size)
	: Size(Math::AlignUp(size, cHeapAlignment))
{
	D3D12_HEAP_DESC heapDesc{
		.SizeInBytes = Size,
		.Properties{
			.Type				  = D3D12_HEAP_TYPE_DEFAULT,
			.CPUPageProperty	  = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
			.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
			.CreationNodeMask	  = 0,
			.VisibleNodeMask	  = 0,
		},
		.Alignment = 0,
		.Flags	   = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES,
	};
	VERIFY_HR(pDevice->GetDevice()->CreateHeap(&heapDesc, IID_PPV_ARGS(pHeap.GetAddressOf())));
}


RGResourceAllocator::RGHeap::~RGHeap()
{
	for (RGPhysicalResource* pResource : Allocations)
		delete pResource;
	for (RGPhysicalResource* pResource : ResourceCache)
		delete pResource;
}


uint32 RGResourceAllocator::RGHeap::GetUsedSize() const
{
	PROFILE_CPU_SCOPE();

	FreeRanges.clear();

	// Mark the start as free
	FreeRanges.push_back({ 0, true });

	// Mark all resources as used
	for (const RGPhysicalResource* pAllocatedResource : Allocations)
	{
		FreeRanges.push_back({ pAllocatedResource->Offset, false });
		FreeRanges.push_back({ pAllocatedResource->Offset + pAllocatedResource->Size, true });
	}

	// Close the open free range
	FreeRanges.push_back({ Size, false });

	// Sort the markup by offset.
	std::sort(FreeRanges.begin(), FreeRanges.end(), [](const RGHeap::HeapOffset& a, const RGHeap::HeapOffset& b) { return a.Offset < b.Offset; });

	uint32 freeSpace = 0;

	uint32 freeRangeCounter = 0;
	uint32 lastBeginOffset	= 0;
	for (const RGHeap::HeapOffset& heapOffset : FreeRanges)
	{
		if (heapOffset.IsFreeBegin)
		{
			// Keep track of how many open ranges there are and where it started
			lastBeginOffset = heapOffset.Offset;
			++freeRangeCounter;
		}
		else
		{
			--freeRangeCounter;
			if (freeRangeCounter == 0)
			{
				freeSpace += heapOffset.Offset - lastBeginOffset;
			}
		}
	}
	return Size - freeSpace;
}


bool RGResourceAllocator::RGHeap::TryAllocate(GraphicsDevice* pDevice, uint32 frameIndex, RGResource* pResource)
{
	if (pResource->Size > Size)
		return false;

	// Shrinking: If the found heap has no allocations and it's very large for the resource, don't allocate into it and instead continue searching so this heap has the chance to be released
	if (Allocations.empty() && Math::AlignUp<uint64>(pResource->Size, cHeapAlignment) < Size)
		return false;

	FreeRanges.clear();

	// Mark the start as free
	FreeRanges.push_back({ 0, true });

	// For each allocated resource, if the lifetime overlaps, mark it as "not free"
	for (RGPhysicalResource* pAllocatedResource : Allocations)
	{
		if (pAllocatedResource->Lifetime.Overlaps(pResource->GetLifetime()) || pAllocatedResource->IsExternal)
		{
			FreeRanges.push_back({ pAllocatedResource->Offset, false });
			FreeRanges.push_back({ pAllocatedResource->Offset + pAllocatedResource->Size, true });
		}
	}

	// Close the open free range
	FreeRanges.push_back({ Size, false });

	// Sort the markup by offset.
	std::sort(FreeRanges.begin(), FreeRanges.end(), [](const RGHeap::HeapOffset& a, const RGHeap::HeapOffset& b) { return a.Offset < b.Offset; });

	// Algorithm: Keep track of how many "free" marks we've come across
	// If we hit the end of a free mark and the range is closed (== 0), then this range is free
	uint32 freeRangeCounter = 0;
	uint32 lastBeginOffset	= 0;
	for (const RGHeap::HeapOffset& heapOffset : FreeRanges)
	{
		if (heapOffset.IsFreeBegin)
		{
			// Keep track of how many open ranges there are and where it started
			lastBeginOffset = heapOffset.Offset;
			++freeRangeCounter;
		}
		else
		{
			--freeRangeCounter;
			if (freeRangeCounter == 0)
			{
				// Test whether the free region is large enough
				uint32 alignedOffset = Math::AlignUp(lastBeginOffset, pResource->Alignment);
				if (alignedOffset + pResource->Size <= heapOffset.Offset)
				{
					LastUsedFrame = frameIndex;

					// Sanity check
					gAssert(alignedOffset + pResource->Size <= Size);
					gAssert(Math::IsAligned(alignedOffset, pResource->Alignment));

					RGPhysicalResource* pPhysicalResource = nullptr;

					// Try to find an already existing physical resource that fits the space and description
					gAssert(pHeap || Allocations.empty(), "Heap can't have physical resources without an allocated heap");
					for (uint32 i = 0; i < (uint32)ResourceCache.size(); ++i)
					{
						RGPhysicalResource* pCachedResource = ResourceCache[i];
						if (pCachedResource->Offset == alignedOffset && // The physical resource must be within the region of the free range
							pCachedResource->IsCompatible(pResource))	// The physical resource description must match the vritual resource
						{
							pPhysicalResource = pCachedResource;
							Utils::gSwapRemove(ResourceCache, i);
							break;
						}
					}

					if (!pPhysicalResource)
					{
						pPhysicalResource		  = new RGPhysicalResource();
						pPhysicalResource->Offset = alignedOffset;
						pPhysicalResource->Size	  = pResource->Size;
						pPhysicalResource->Type	  = pResource->GetType();

						if (pResource->GetType() == RGResourceType::Texture)
						{
							RGTexture* pTexture					   = static_cast<RGTexture*>(pResource);
							pPhysicalResource->ResourceTextureDesc = pTexture->GetDesc();
							pPhysicalResource->pResource		   = pDevice->CreateTexture(pTexture->GetDesc(), pHeap, alignedOffset, "");
						}
						else if (pResource->GetType() == RGResourceType::Buffer)
						{
							RGBuffer* pBuffer					  = static_cast<RGBuffer*>(pResource);
							pPhysicalResource->ResourceBufferDesc = pBuffer->GetDesc();
							pPhysicalResource->pResource		  = pDevice->CreateBuffer(pBuffer->GetDesc(), pHeap, alignedOffset, "");
						}
						else
						{
							gAssert(false);
						}
					}

					pPhysicalResource->LastUsedFrame = frameIndex;
					pPhysicalResource->Lifetime		 = pResource->GetLifetime();
					Allocations.push_back(pPhysicalResource);
					pResource->SetResource(pPhysicalResource->pResource);
					if (pPhysicalResource->Name != pResource->GetName())
					{
						pPhysicalResource->Name = pResource->GetName();
						pResource->GetPhysicalUnsafe()->SetName(pResource->GetName());
					}

					gAssert(pResource->IsAllocated());

					// E_LOG(Warning, "[%s] Placed Resource in Heap %d - Lifetime [%d, %d] - Memory [%llu, %llu]", pResource->pName, heap.ID, pResource->GetLifetime().Begin, pResource->GetLifetime().End, pResource->GetMemoryRange().Begin, pResource->GetMemoryRange().End);

					return true;
				}
			}
		}
	}
	return false;
}


bool RGResourceAllocator::RGHeap::IsUnused(uint32 frameIndex) const
{
	if (LastUsedFrame + cHeapCleanupLatency < frameIndex)
	{
		// Can't delete a heap if it has resources inside it that are still references
		for (const RGPhysicalResource* pRes : Allocations)
		{
			if (pRes->pResource->GetNumRefs() > 1)
				return false;
		}
		return true;
	}
	return false;
}


void RGResourceAllocator::RGHeap::FreeUnused(uint32 frameIndex)
{
	PROFILE_CPU_SCOPE();

	for (uint32 i = 0; i < (uint32)Allocations.size(); ++i)
	{
		// If an allocation has no external refs, it can be forfeited and moved back into the cache
		RGPhysicalResource* pResource = Allocations[i];
		pResource->IsExternal = pResource->pResource->GetNumRefs() > 1;
		if (!pResource->IsExternal)
		{
			Utils::gSwapRemove(Allocations, i);
			i--;
			ResourceCache.push_back(pResource);
		}
	}

	Utils::gSwapRemoveIf(ResourceCache, [this, frameIndex](const RGPhysicalResource* pResource) {
		if (pResource->LastUsedFrame + cResourceCleanupLatency < frameIndex)
		{
			delete pResource;
			return true;
		}
		return false;
	});
}


void RGResourceAllocator::Init(GraphicsDevice* pDevice)
{
	m_pDevice = pDevice;
}


void RGResourceAllocator::Shutdown()
{
	m_Heaps.clear();
}


void RGResourceAllocator::AllocateResources(Span<RGResource*> graphResources)
{
	PROFILE_CPU_SCOPE();

	Array<RGResource*> resources = graphResources.Copy();

	// Compute Size/Alignment requirement of each resource
	for (RGResource* pResource : resources)
	{
		uint64 size, alignment;
		D3D::GetResourceAllocationInfo(m_pDevice->GetDevice(), sGetResourceDesc(pResource), size, alignment);
		pResource->Size		 = (int)size;
		pResource->Alignment = (int)alignment;
	}

	// If the resource is imported, find whether the physical resource was allocated by this allocated to mark it as used
	// Also record the lifetime so that other resources may alias with it
	for (RGResource* pResource : resources)
	{
		if (pResource->IsImported)
		{
			gAssert(pResource->pPhysicalResource);

			RGHeap*				pHeap			  = nullptr;
			RGPhysicalResource* pPhysicalResource = FindAllocation(pResource->GetPhysicalUnsafe(), &pHeap);
			if (pPhysicalResource)
			{
				pPhysicalResource->Lifetime		 = pResource->GetLifetime();
				pPhysicalResource->LastUsedFrame = m_FrameIndex;
				// E_LOG(Warning, "[%s] Existing Imported Resource. Heap %d - Lifetime [%d, %d] - Memory [%llu, %llu]", pResource->pName, pHeap->ID, pResource->GetLifetime().Begin, pResource->GetLifetime().End, pResource->GetMemoryRange().Begin, pResource->GetMemoryRange().End);
			}
		}
	}

	// Sort resources largest to smallest, then largest alignment to smallest.
	// Exported resources always come first so that they don't cause fragmentation
	std::sort(resources.begin(), resources.end(), [](const RGResource* a, const RGResource* b) {

		if (a->IsExported	!= b->IsExported)	return a->IsExported > b->IsExported;
		if (a->Size			!= b->Size)			return a->Size > b->Size;
		if (a->Alignment	!= b->Alignment)	return a->Alignment > b->Alignment;
		return a->ID.GetIndex() < b->ID.GetIndex();
	});

	// Sort heaps largest to smallest, so smaller heaps can be removed.
	std::sort(m_Heaps.begin(), m_Heaps.end(), [](const UniquePtr<RGHeap>& a, const UniquePtr<RGHeap>& b) {
		return a->GetSize() > b->GetSize();
	});

	for (RGResource* pResource : resources)
	{
		if (pResource->IsAllocated())
			continue;

		gAssert(pResource->Size != 0);

		bool success = false;
		for (const UniquePtr<RGHeap>& pHeap : m_Heaps)
		{
			// If the resource is larger than the heap, bail out. Heaps are sorted by size so if this one doesn't fit, none will
			if (pResource->Size > pHeap->GetSize())
				break;

			if (pHeap->TryAllocate(m_pDevice, m_FrameIndex, pResource))
			{
				success = true;
				break;
			}
		}

		// If no heap was found, that means the resource wasn't placed and a new heap is needed
		if (!success)
		{
			m_Heaps.push_back(std::make_unique<RGHeap>(m_pDevice, pResource->Size));
			RGHeap* pHeap = m_Heaps.back().get();
			// E_LOG(Warning, "New Heap %d - Size: %s", heap.ID, Math::PrettyPrintDataSize(heap.Size));

			gVerify(pHeap->TryAllocate(m_pDevice, m_FrameIndex, pResource), == true);
		}
	}

#ifdef _DEBUG
	{
		PROFILE_CPU_SCOPE("Validate");

		// Validation
		for (const UniquePtr<RGHeap>& pHeap : m_Heaps)
		{
			for (const RGPhysicalResource* pResource : pHeap->GetAllocations())
			{
				// Validate whether all allocated resources don't overlap both memory range AND lifetime.
				// If that happens, something in the placement must've gone wrong.
				auto it = std::find_if(pHeap->GetAllocations().begin(), pHeap->GetAllocations().end(), [pResource](const RGPhysicalResource* pOther) {
					if (pOther == pResource)
						return false;
					return pResource->Lifetime.Overlaps(pOther->Lifetime) && pResource->GetMemoryRange().Overlaps(pOther->GetMemoryRange());
				});

				RGPhysicalResource* pOverlappingResource = it == pHeap->GetAllocations().end() ? nullptr : *it;
				gAssert(pOverlappingResource == nullptr,
						"Resource '%s' (Lifetime: [%d, %d], Memory: [%llu, %llu]) overlaps with Resource '%s' (Lifetime: [%d, %d], Memory: [%llu, %llu])",
						pResource->Name, pResource->Lifetime.Begin, pResource->Lifetime.End, pResource->GetMemoryRange().Begin, pResource->GetMemoryRange().End,
						pOverlappingResource->Name, pOverlappingResource->Lifetime.Begin, pOverlappingResource->Lifetime.End, pOverlappingResource->GetMemoryRange().Begin, pOverlappingResource->GetMemoryRange().End);
			}
		}
	}
#endif
}


void RGResourceAllocator::Tick()
{
	ClearUnusedResources();
	++m_FrameIndex;
}



void RGResourceAllocator::ClearUnusedResources()
{
	PROFILE_CPU_SCOPE();

	Utils::gSwapRemoveIf(m_Heaps, [this](const UniquePtr<RGHeap>& pHeap) {
		return pHeap->IsUnused(m_FrameIndex);
	});

	for (const UniquePtr<RGHeap>& pHeap : m_Heaps)
	{
		pHeap->FreeUnused(m_FrameIndex);
	}
}


RGResourceAllocator::RGPhysicalResource* RGResourceAllocator::FindAllocation(const DeviceResource* pResource, RGHeap** pOutHeap)
{
	for (const UniquePtr<RGHeap>& pHeap : m_Heaps)
	{
		for (RGPhysicalResource* pAllocatedResource : pHeap->GetAllocations())
		{
			if (pAllocatedResource->pResource == pResource)
			{
				*pOutHeap = pHeap.get();
				return pAllocatedResource;
			}
		}
	}
	return nullptr;
}


void RGResourceAllocator::DrawDebugView(bool& enabled) const
{
	if (!enabled)
		return;

	// Show allocation sizes and heap layout
	if (ImGui::Begin("Heap Layout", &enabled))
	{
		PROFILE_CPU_SCOPE();

		auto GetResourceColor = [](const RGPhysicalResource* pResource) {
			float		hueMin	   = 0.0f;
			float		hueMax	   = 1.0f;
			const float saturation = 0.5f;
			const float value	   = 0.6f;
			float		hue		   = (float)std::hash<std::string>{}(pResource->Name) / std::numeric_limits<size_t>::max();
			hue					   = hueMin + hue * (hueMax - hueMin);
			float R				   = std::max(std::min(fabs(hue * 6 - 3) - 1, 1.0f), 0.0f);
			float G				   = std::max(std::min(2 - fabs(hue * 6 - 2), 1.0f), 0.0f);
			float B				   = std::max(std::min(2 - fabs(hue * 6 - 4), 1.0f), 0.0f);

			R = ((R - 1) * saturation + 1) * value;
			G = ((G - 1) * saturation + 1) * value;
			B = ((B - 1) * saturation + 1) * value;

			return ImColor(R, G, B, 1.0f);
		};

		uint32 lastPassID				 = 0;
		uint32 totalHeapSize			 = 0;
		uint32 totalResourcesSize		 = 0;
		uint32 totalAliasedResourcesSize = 0;

		for (const UniquePtr<RGHeap>& pHeap : m_Heaps)
		{
			totalHeapSize += pHeap->GetSize();
			totalAliasedResourcesSize += pHeap->GetUsedSize();
			for (RGPhysicalResource* pResource : pHeap->GetAllocations())
			{
				lastPassID = Math::Max(pResource->Lifetime.End, lastPassID);
				totalResourcesSize += pResource->Size;
			}
		}

		if (ImGui::BeginTable("Size Stats", 4))
		{
			ImGui::TableHeader("Header");
			ImGui::TableSetupColumn("Heap Size");
			ImGui::TableSetupColumn("Resources Size");
			ImGui::TableSetupColumn("Aliased Resources Size");
			ImGui::TableSetupColumn("Difference");
			ImGui::TableHeadersRow();

			ImGui::TableNextColumn();
			ImGui::Text(Math::PrettyPrintDataSize(totalHeapSize).c_str());
			ImGui::TableNextColumn();
			ImGui::Text(Math::PrettyPrintDataSize(totalResourcesSize).c_str());
			ImGui::TableNextColumn();
			ImGui::Text(Math::PrettyPrintDataSize(totalAliasedResourcesSize).c_str());
			ImGui::TableNextColumn();
			if (totalResourcesSize > totalHeapSize)
				ImGui::Text("%s", Math::PrettyPrintDataSize(totalResourcesSize - totalHeapSize).c_str());
			else
				ImGui::Text("+%s", Math::PrettyPrintDataSize(totalHeapSize - totalResourcesSize).c_str());
			ImGui::EndTable();
		}

		float width		 = ImGui::GetContentRegionAvail().x;
		float widthScale = width / (float)lastPassID;
		for (const UniquePtr<RGHeap>& pHeap : m_Heaps)
		{
			ImGui::Text("Heap (Size: %s - Allocations: %d - Resources: %d)", Math::PrettyPrintDataSize(pHeap->GetSize()).c_str(), pHeap->GetAllocations().GetSize(), pHeap->GetNumResources());
			ImDrawList* pDraw = ImGui::GetWindowDrawList();

			ImVec2 cursor = ImGui::GetCursorScreenPos();

			float heapHeight   = 5 * log2f((float)pHeap->GetSize() + 1);
			auto  GetBarHeight = [&](uint64 size) { return (float)size / pHeap->GetSize() * heapHeight; };

			pDraw->AddRectFilled(cursor, cursor + ImVec2(widthScale * (lastPassID + 1), heapHeight), ImColor(1.0f, 1.0f, 1.0f, 0.2f));
			for (const RGPhysicalResource* pResource : pHeap->GetAllocations())
			{
				URange lifetime = pResource->Lifetime;
				if (pResource->IsExternal)
					lifetime = URange(0, lastPassID);

				ImRect barRect(
					cursor + ImVec2(widthScale * lifetime.Begin, GetBarHeight(pResource->Offset)),
					cursor + ImVec2(widthScale * lifetime.End, GetBarHeight(pResource->Size + pResource->Offset)));

				if (ImGui::ItemAdd(barRect, ImGui::GetID(pResource)))
				{
					ImColor color = GetResourceColor(pResource);
					if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
					{
						color.Value.x *= 1.5f;
						color.Value.y *= 1.5f;
						color.Value.z *= 1.5f;
						ImGui::Text("Name: %s", pResource->pResource->GetName().c_str());
						ImGui::Text("Size: %s", Math::PrettyPrintDataSize(pResource->Size).c_str());

						ImGui::EndTooltip();
					}

					pDraw->AddRectFilled(barRect.Min, barRect.Max, ImColor(0.5f, 0.5f, 0.5f));
					pDraw->AddRectFilled(barRect.Min + ImVec2(1, 1), barRect.Max - ImVec2(1, 1), color);
				}
			}
			ImGui::Dummy(ImVec2(0, heapHeight));
		}
	}

	ImGui::End();
}
