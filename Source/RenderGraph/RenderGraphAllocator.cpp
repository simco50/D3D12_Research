#include "stdafx.h"
#include "RenderGraphAllocator.h"
#include "Core/Profiler.h"
#include "RHI/Device.h"


RGResourceAllocator gRenderGraphAllocator;


RGHeap::RGHeap(GraphicsDevice* pDevice, uint32 size)
	: pDevice(pDevice)
{
	FrontIndex = NewRange();
	InsertRange(FrontIndex, nullptr, 0, size);
	Size = size;

	D3D12_HEAP_DESC heapDesc{
		.SizeInBytes = size,
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


RGPhysicalResource* RGHeap::AllocateTexture(const char* pName, const TextureDesc& textureDesc)
{
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

	D3D12_RESOURCE_DESC resourceDesc = GetResourceDesc(textureDesc);

	uint64 size, alignment;
	D3D::GetResourceAllocationInfo(pDevice->GetDevice(), resourceDesc, size, alignment);
	Allocation				   alloc;
	Array<RGPhysicalResource*> overlaps;
	if (Allocate((uint32)size, (uint32)alignment, alloc, overlaps))
	{
		RGPhysicalResource* pResource = nullptr;
		for (UniquePtr<RGPhysicalResource>& pExistingResource : PhysicalResources)
		{
			if (pExistingResource->Offset == alloc.Offset &&
				pExistingResource->ResourceTextureDesc == textureDesc &&
				pExistingResource->LastUsedFrame != FrameIndex)
			{
				pResource = pExistingResource.get();
				gAssert(pResource->pResource->GetNumRefs() == 1);
			}
		}

		if (!pResource)
		{
			PhysicalResources.emplace_back(std::make_unique<RGPhysicalResource>());
			pResource					   = PhysicalResources.back().get();
			pResource->ResourceTextureDesc = textureDesc;
			pResource->Offset			   = alloc.Offset;
			pResource->Size				   = alloc.Size;
			pResource->PaddingBegin		   = alloc.PaddingBegin;
			pResource->pResource		   = pDevice->CreateTexture(textureDesc, pHeap, alloc.Offset, pName);
			pResource->pHeap			   = this;
		}

		pResource->pResource->SetName(pName);
		pResource->PaddingBegin = alloc.PaddingBegin;
		pResource->IsAllocated	= true;

		return pResource;
	}
	return nullptr;
}


RGPhysicalResource* RGHeap::AllocateBuffer(const char* pName, const BufferDesc& bufferDesc)
{
	auto GetResourceDesc = [](const BufferDesc& bufferDesc) {
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferDesc.Size, D3D12_RESOURCE_FLAG_NONE);
		if (EnumHasAnyFlags(bufferDesc.Flags, BufferFlag::UnorderedAccess))
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		if (EnumHasAnyFlags(bufferDesc.Flags, BufferFlag::AccelerationStructure))
			desc.Flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
		return desc;
	};

	D3D12_RESOURCE_DESC resourceDesc = GetResourceDesc(bufferDesc);

	uint64 size, alignment;
	D3D::GetResourceAllocationInfo(pDevice->GetDevice(), resourceDesc, size, alignment);
	Allocation				   alloc;
	Array<RGPhysicalResource*> overlaps;
	if (Allocate((uint32)size, (uint32)alignment, alloc, overlaps))
	{
		RGPhysicalResource* pResource = nullptr;
		for (UniquePtr<RGPhysicalResource>& pExistingResource : PhysicalResources)
		{
			if (pExistingResource->Offset == alloc.Offset &&
				pExistingResource->ResourceBufferDesc == bufferDesc &&
				pExistingResource->LastUsedFrame != FrameIndex)
			{
				pResource = pExistingResource.get();
				gAssert(pResource->pResource->GetNumRefs() == 1);
			}
		}

		if (!pResource)
		{
			PhysicalResources.emplace_back(std::make_unique<RGPhysicalResource>());
			pResource					  = PhysicalResources.back().get();
			pResource->ResourceBufferDesc = bufferDesc;
			pResource->Offset			  = alloc.Offset;
			pResource->Size				  = alloc.Size;
			pResource->pResource		  = pDevice->CreateBuffer(bufferDesc, pHeap, alloc.Offset, pName);
			pResource->PaddingBegin		  = alloc.PaddingBegin;
			pResource->pHeap			  = this;
		}

		pResource->pResource->SetName(pName);
		pResource->PaddingBegin = alloc.PaddingBegin;
		pResource->IsAllocated	= true;

		return pResource;
	}
	return nullptr;
}


void RGHeap::ReleaseResource(RGPhysicalResource* pResource)
{
	Allocation alloc;
	alloc.Offset	   = pResource->Offset;
	alloc.PaddingBegin = pResource->PaddingBegin;
	alloc.Size		   = pResource->Size;

	pResource->IsAllocated = false;

	Release(alloc, pResource);
}


bool RGHeap::Allocate(uint32 size, uint32 alignment, Allocation& outAllocation, Array<RGPhysicalResource*>& outOverlaps)
{
	gAssert(alignment == 64 * Math::KilobytesToBytes);
	outOverlaps.clear();

	RangeIndex		  index							  = GetFirstRange();
	uint32			  consumedRangeOffset			  = 0;
	RangeIndex		  firstConsumedRangeIndex		  = sInvalidRange;
	uint32			  firstConsumedRangeAlignedOffset = 0;
	Array<RangeIndex> consumedRanges;

	while (index != sInvalidRange)
	{
		ResourceRange& range = Ranges[index];

		// If this is the first potential range, record the start of the potential allocation
		if (consumedRanges.empty())
		{
			firstConsumedRangeIndex			= index;
			consumedRangeOffset				= range.Offset;
			firstConsumedRangeAlignedOffset = Math::AlignUp(range.Offset, alignment);

			// Skip this range if the alignment requirement already exceeds the size of the range
			if (firstConsumedRangeAlignedOffset > range.Offset + range.Size)
			{
				index = range.NextRange;
				continue;
			}
		}

		// The next potential range must be continguous with the previous range
		if (consumedRangeOffset != range.Offset)
		{
			// Not continguous. Start over from this range
			consumedRanges.clear();
			continue;
		}

		consumedRangeOffset += range.Size;
		consumedRanges.push_back(index);

		uint32 freeSize = range.Offset + range.Size - firstConsumedRangeAlignedOffset;
		if (size <= freeSize)
		{
			uint32 padding = firstConsumedRangeAlignedOffset - Ranges[firstConsumedRangeIndex].Offset;

			for (uint32 i = 0; i < consumedRanges.size(); ++i)
			{
				RangeIndex	  idx			 = consumedRanges[i];
				ResourceRange allocatedRange = Ranges[idx];
				if (allocatedRange.pResource)
					outOverlaps.push_back(allocatedRange.pResource);

				if (i < consumedRanges.size() - 1)
					RemoveRange(idx);
			}

			uint32 remainder = freeSize - size;
			if (remainder == 0)
			{
				RemoveRange(consumedRanges.back());
			}
			else
			{
				ResourceRange& allocatedRange = Ranges[consumedRanges.back()];
				allocatedRange.Size			  = remainder;
				allocatedRange.Offset		  = firstConsumedRangeAlignedOffset + size;
			}

			outAllocation.Offset	   = firstConsumedRangeAlignedOffset;
			outAllocation.Size		   = size;
			outAllocation.PaddingBegin = padding;

			AllocatedSize += outAllocation.Size + outAllocation.PaddingBegin;

			VerifyState();
			return true;
		}

		index = range.NextRange;
	}
	return false;
}



void RGHeap::Release(const Allocation& allocation, RGPhysicalResource* pResource)
{
	RangeIndex prevIndex = FrontIndex;
	RangeIndex index	 = GetFirstRange();

	// Compute the region to free including padding
	uint32 rangeStart = allocation.Offset - allocation.PaddingBegin;
	uint32 rangeEnd	  = allocation.Offset + allocation.Size;
	uint32 rangeSize  = rangeEnd - rangeStart;

	// Find the range after which this range should be inserted
	while (index != sInvalidRange)
	{
		ResourceRange& range = Ranges[index];
		if (range.Offset > rangeStart)
		{
			break;
		}
		prevIndex = index;
		index	  = range.NextRange;
	}

	// Mark the range as freed, keeping track of which resource was placed here
	InsertRange(prevIndex, pResource, rangeStart, rangeSize);

	AllocatedSize -= rangeSize;

	VerifyState();
}


RGHeap::RangeIndex RGHeap::InsertRange(RangeIndex prevIndex, RGPhysicalResource* pResource, uint32 offset, uint32 size)
{
	// Create and fill a new range
	// New range comes after the prevIndex and before the pervIndex->Next
	RangeIndex	   index	= NewRange();
	ResourceRange& prevRange = Ranges[prevIndex];
	ResourceRange& newRange = Ranges[index];
	newRange				= {
					   .Offset	  = offset,
					   .Size	  = size,
					   .PrevRange = prevIndex,
					   .NextRange = prevRange.NextRange,
					   .pResource = pResource,
	};

	// Update the previous range of the next range to 'this'
	if (prevRange.NextRange != sInvalidRange)
		Ranges[newRange.NextRange].PrevRange = index;

	// Update the next range of the previous range to 'this'
	prevRange.NextRange = index;
	return index;
}


void RGHeap::RemoveRange(RangeIndex index)
{
	// Take out a range
	ResourceRange& range	 = Ranges[index];
	ResourceRange& prevRange = Ranges[range.PrevRange];

	prevRange.NextRange = range.NextRange;
	if (range.NextRange != sInvalidRange)
		Ranges[range.NextRange].PrevRange = range.PrevRange;

	// Free the range
	range.NextRange = sInvalidRange;
	range.PrevRange = sInvalidRange;
	range.pResource = nullptr;
	FreeList.push_back(index);
}


RGHeap::RangeIndex RGHeap::NewRange()
{
	// Get a free range from the free list or create a new one
	if (!FreeList.empty())
	{
		RangeIndex index = FreeList.back();
		FreeList.pop_back();
		return index;
	}
	Ranges.emplace_back();
	return RangeIndex((uint32)Ranges.size() - 1);
}


void RGHeap::VerifyState() const
{
	RangeIndex previousIndex = FrontIndex;
	RangeIndex index		 = GetFirstRange();

	uint32 freeSpace = 0;

	while (index != sInvalidRange)
	{
		const ResourceRange& range = Ranges[index];

		if (previousIndex != FrontIndex)
		{
			const ResourceRange& prevRange = Ranges[previousIndex];
			gAssert(prevRange.Offset + prevRange.Size <= range.Offset);
		}

		freeSpace += range.Size;

		previousIndex = index;
		index		  = Ranges[index].NextRange;
	}

	gAssert(freeSpace + AllocatedSize == Size);
}


void RGResourceAllocator::Init(GraphicsDevice* pDevice)
{
	m_pDevice = pDevice;
}


void RGResourceAllocator::Shutdown()
{
	ClearAll();
}

void RGResourceAllocator::Tick()
{
	Utils::gSwapRemoveIf(m_Heaps, [this](UniquePtr<RGHeap>& pHeap) {
		if (pHeap->LastUsedFrame + cHeapCleanupLatency < m_FrameIndex)
		{
			// Can't delete a heap if it has resources inside it that are still references
			for (const UniquePtr<RGPhysicalResource>& pRes : pHeap->PhysicalResources)
			{
				if (pRes->pResource->GetNumRefs() > 1)
					return false;
			}
			return true;
		}
		return false;
	});

	for (UniquePtr<RGHeap>& heap : m_Heaps)
	{
		Utils::gSwapRemoveIf(heap->PhysicalResources, [this, &heap](UniquePtr<RGPhysicalResource>& pResource) {
			// If it has external references, it has to be kept
			if (pResource->pResource->GetNumRefs() > 1)
				return false;

			// If it is still allocated (can happen when an external ref is released), the allocation must be released
			if (pResource->IsAllocated)
				heap->ReleaseResource(pResource.get());

			// If the resource hasn't been used for more than N frames, remove it
			return pResource->LastUsedFrame + 128 < m_FrameIndex;
		});
	}

	++m_FrameIndex;

	for (UniquePtr<RGHeap>& heap : m_Heaps)
	{
		heap->FrameIndex = m_FrameIndex;
	}
}


void RGResourceAllocator::ClearAll()
{
	m_Heaps.clear();
}


RGPhysicalResource* RGResourceAllocator::FindExistingResource(const DeviceResource* pResource)
{
	for (UniquePtr<RGHeap>& pHeap : m_Heaps)
	{
		RGPhysicalResource* pPhysical = pHeap->Find(pResource);
		return pPhysical;
	}
	return nullptr;
}


void RGResourceAllocator::AllocateResource(RGResource* pResource)
{
	if (pResource->IsImported)
	{
		pResource->pRGPhysicalResource = FindExistingResource(pResource->GetPhysicalUnsafe());
		if (pResource->pRGPhysicalResource)
		{
			pResource->pRGPhysicalResource->LastUsedFrame = m_FrameIndex;
			pResource->pRGPhysicalResource->LifeTime	  = pResource->GetLifetimeActual();
		}
		return;
	}

	for (UniquePtr<RGHeap>& heap : m_Heaps)
	{
		if (pResource->GetType() == RGResourceType::Texture)
		{
			pResource->pRGPhysicalResource = heap->AllocateTexture(pResource->pName, static_cast<RGTexture*>(pResource)->GetDesc());
			if (pResource->pRGPhysicalResource)
			{
				heap->LastUsedFrame							  = m_FrameIndex;
				pResource->pPhysicalResource				  = pResource->pRGPhysicalResource->pResource;
				pResource->pRGPhysicalResource->LastUsedFrame = m_FrameIndex;
				pResource->pRGPhysicalResource->LifeTime	  = pResource->GetLifetimeActual();
				return;
			}
		}
		else if (pResource->GetType() == RGResourceType::Buffer)
		{
			pResource->pRGPhysicalResource = heap->AllocateBuffer(pResource->pName, static_cast<RGBuffer*>(pResource)->GetDesc());
			if (pResource->pRGPhysicalResource)
			{
				heap->LastUsedFrame							  = m_FrameIndex;
				pResource->pPhysicalResource				  = pResource->pRGPhysicalResource->pResource;
				pResource->pRGPhysicalResource->LastUsedFrame = m_FrameIndex;
				pResource->pRGPhysicalResource->LifeTime	  = pResource->GetLifetimeActual();
				return;
			}
		}
	}

	m_Heaps.emplace_back(std::make_unique<RGHeap>(m_pDevice, cHeapAlignment));
	RGHeap* pHeap = m_Heaps.back().get();

	if (pResource->GetType() == RGResourceType::Texture)
	{
		pResource->pRGPhysicalResource = pHeap->AllocateTexture(pResource->pName, static_cast<RGTexture*>(pResource)->GetDesc());
		if (pResource->pRGPhysicalResource)
		{
			pHeap->LastUsedFrame						  = m_FrameIndex;
			pResource->pPhysicalResource				  = pResource->pRGPhysicalResource->pResource;
			pResource->pRGPhysicalResource->LastUsedFrame = m_FrameIndex;
			pResource->pRGPhysicalResource->LifeTime	  = pResource->GetLifetimeActual();
			return;
		}
	}
	else if (pResource->GetType() == RGResourceType::Buffer)
	{
		pResource->pRGPhysicalResource = pHeap->AllocateBuffer(pResource->pName, static_cast<RGBuffer*>(pResource)->GetDesc());
		if (pResource->pRGPhysicalResource)
		{
			pHeap->LastUsedFrame						  = m_FrameIndex;
			pResource->pPhysicalResource				  = pResource->pRGPhysicalResource->pResource;
			pResource->pRGPhysicalResource->LastUsedFrame = m_FrameIndex;
			pResource->pRGPhysicalResource->LifeTime	  = pResource->GetLifetimeActual();
			return;
		}
	}
}


void RGResourceAllocator::ReleaseResource(RGResource* pResource)
{
	if (!pResource->pRGPhysicalResource || pResource->IsExported)
		return;

	RGPhysicalResource* pPhysical = pResource->pRGPhysicalResource;
	if (pPhysical->pResource->GetNumRefs() > 1)
		return;

	pPhysical->pHeap->ReleaseResource(pPhysical);
}


static ImColor sGetResourceColor(const RGPhysicalResource* pResource)
{
	float		hueMin	   = 0.0f;
	float		hueMax	   = 1.0f;
	const float saturation = 0.5f;
	const float value	   = 0.6f;
	float		hue		   = (float)std::hash<std::string>{}(pResource->pResource->GetName()) / std::numeric_limits<size_t>::max();
	hue					   = hueMin + hue * (hueMax - hueMin);
	float R				   = std::max(std::min(fabs(hue * 6 - 3) - 1, 1.0f), 0.0f);
	float G				   = std::max(std::min(2 - fabs(hue * 6 - 2), 1.0f), 0.0f);
	float B				   = std::max(std::min(2 - fabs(hue * 6 - 4), 1.0f), 0.0f);

	R = ((R - 1) * saturation + 1) * value;
	G = ((G - 1) * saturation + 1) * value;
	B = ((B - 1) * saturation + 1) * value;

	return ImColor(R, G, B, 1.0f);
}


void RGResourceAllocator::DrawDebugView(URange lifetimeRange) const
{
	PROFILE_CPU_SCOPE();

	// Show allocation sizes and heap layout
	if (ImGui::Begin("Heap Layout"))
	{
		uint64 totalResourcesSize = 0;
		uint64 totalHeapSize = 0;
		for (const UniquePtr<RGHeap>& pHeap : m_Heaps)
		{
			totalHeapSize += pHeap->Size;

			for (const UniquePtr<RGPhysicalResource>& pResource : pHeap->PhysicalResources)
			{
				if (pResource->LastUsedFrame == m_FrameIndex || pResource->pResource->GetNumRefs() > 1)
					totalResourcesSize += pResource->Size;
			}
		}

		if (ImGui::BeginTable("Size Stats", 4))
		{
			ImGui::TableHeader("Header");
			ImGui::TableSetupColumn("Mode");
			ImGui::TableSetupColumn("Unaliased");
			ImGui::TableSetupColumn("Aliased");
			ImGui::TableSetupColumn("Difference");
			ImGui::TableHeadersRow();

			ImGui::TableNextColumn();
			ImGui::Text("No Aliasing");
			ImGui::TableNextColumn();
			ImGui::Text(Math::PrettyPrintDataSize(totalResourcesSize).c_str());
			ImGui::TableNextColumn();
			ImGui::Text(Math::PrettyPrintDataSize(totalResourcesSize).c_str());
			ImGui::TableNextColumn();
			ImGui::Text(Math::PrettyPrintDataSize(0).c_str());

			ImGui::TableNextColumn();
			ImGui::Text("Aliasing");
			ImGui::TableNextColumn();
			ImGui::Text(Math::PrettyPrintDataSize(totalResourcesSize).c_str());
			ImGui::TableNextColumn();
			ImGui::Text(Math::PrettyPrintDataSize(totalHeapSize).c_str());
			ImGui::TableNextColumn();
			if (totalResourcesSize > totalHeapSize)
				ImGui::Text("%s", Math::PrettyPrintDataSize(totalResourcesSize - totalHeapSize).c_str());
			else
				ImGui::Text("+%s", Math::PrettyPrintDataSize(totalHeapSize - totalResourcesSize).c_str());
			ImGui::EndTable();
		}

		float width		 = ImGui::GetContentRegionAvail().x;
		float widthScale = width / (float)lifetimeRange.End;
		for (const UniquePtr<RGHeap>& pHeap : m_Heaps)
		{
			ImGui::Text("Heap (Size: %s - Resources: %d)", Math::PrettyPrintDataSize(pHeap->Size).c_str(), pHeap->PhysicalResources.size());
			ImDrawList* pDraw = ImGui::GetWindowDrawList();

			ImVec2 cursor = ImGui::GetCursorScreenPos();

			float heapHeight   = 10*log2f((float)pHeap->Size + 1);
			auto  GetBarHeight = [&](uint64 size) { return (float)size / pHeap->Size * heapHeight; };

			pDraw->AddRectFilled(cursor, cursor + ImVec2(widthScale * (lifetimeRange.End + 1), heapHeight), ImColor(1.0f, 1.0f, 1.0f, 0.2f));
			for (const UniquePtr<RGPhysicalResource>& pResource : pHeap->PhysicalResources)
			{
				if (pResource->LastUsedFrame != m_FrameIndex && pResource->pResource->GetNumRefs() <= 1)
					continue;

				URange lifetime = pResource->LifeTime;
				if (lifetime.End == 0xFFFFFFFF)
					lifetime.End = lifetimeRange.End;

				if (pResource->pResource->GetNumRefs() > 1)
					lifetime.Begin = 0;

				ImRect barRect(
					cursor + ImVec2(widthScale * lifetime.Begin, GetBarHeight(pResource->Offset)),
					cursor + ImVec2(widthScale * lifetime.End, GetBarHeight(pResource->Size + pResource->Offset)));

				if (ImGui::ItemAdd(barRect, ImGui::GetID(pResource.get())))
				{
					ImColor color = sGetResourceColor(pResource.get());
					if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
					{
						color.Value.x *= 1.5f;
						color.Value.y *= 1.5f;
						color.Value.z *= 1.5f;
						ImGui::Text("Name: %s", pResource->pResource->GetName().c_str());
						ImGui::Text("Size: %s", Math::PrettyPrintDataSize(pResource->Size).c_str());
						ImGui::Text("Address: %p", pResource->pResource.Get());

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
