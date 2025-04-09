#pragma once

#include <imgui_internal.h>
#include "RHI/RHI.h"
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

		bool IsCompatible(const RGResource* pResource) const
		{
			if (pResource->GetType() == RGResourceType::Texture)
				return ResourceTextureDesc == static_cast<const RGTexture*>(pResource)->GetDesc();
			return ResourceBufferDesc == static_cast<const RGBuffer*>(pResource)->GetDesc();
		}
	};

	// Represents a physical resource heap
	struct RGHeap
	{
		uint32							   ID;
		Array<RGResource*>				   Allocations;
		uint64							   Size;
		uint32							   LastUsedFrame = 0;

		Array<UniquePtr<RGPhysicalResource>> PhysicalResources;
		Ref<ID3D12Heap>					   pHeap;
	};

public:
	void Init(GraphicsDevice* pDevice)
	{
		m_pDevice = pDevice;
	}

	void Shutdown()
	{
		ClearAll();
	}

	void AllocateResources(Span<RGResource*> graphResources)
	{
		PROFILE_CPU_SCOPE();

		for (RGResource* pResource : graphResources)
		{
			uint64 size, alignment;
			D3D::GetResourceAllocationInfo(m_pDevice->GetDevice(), sGetResourceDesc(pResource), size, alignment);
			pResource->Size		 = (int)size;
			pResource->Alignment = (int)alignment;
			pResource->Offset	 = 0;
		}

		Array<RGResource*> resources = graphResources.Copy();
		ComputeResourceAliasing(resources, m_Heaps);

		DrawDebugView(resources, m_Heaps);
	}

	void Tick()
	{
		ClearUnusedResources(m_Heaps);
		++m_FrameIndex;
	}

	void DrawDebugView()
	{
		//DrawDebugView(m_Heaps);
	}

	void ClearAll()
	{
		m_Heaps.clear();
	}

private:
	static D3D12_RESOURCE_DESC sGetResourceDesc(const RGResource* pResource)
	{
		if (pResource->GetType() == RGResourceType::Texture)
		{
			const RGTexture* pTexture = (RGTexture*)pResource;
			const TextureDesc& desc = pTexture->GetDesc();

			auto GetResourceDesc = [](const TextureDesc& textureDesc)
				{
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
							//I think this can be a significant optimization on some devices because then the depth buffer can never be (de)compressed
							desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
						}
					}
					return desc;
				};

			return GetResourceDesc(desc);
		}
		else
		{
			const RGBuffer* pBuffer = (RGBuffer*)pResource;
			const BufferDesc bufferDesc = pBuffer->GetDesc();

			D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferDesc.Size, D3D12_RESOURCE_FLAG_NONE);
			if (EnumHasAnyFlags(bufferDesc.Flags, BufferFlag::UnorderedAccess))
				desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			if (EnumHasAnyFlags(bufferDesc.Flags, BufferFlag::AccelerationStructure))
				desc.Flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
			return desc;
		}
	}
private:
	void ClearUnusedResources(Array<RGHeap>& heaps)
	{
		PROFILE_CPU_SCOPE();

		uint32 heapIndex = 0;
		while (heapIndex < (uint32)heaps.size())
		{
			// Retire heap if it hasn't been used for a while
			RGHeap& heap = heaps[heapIndex];
			if (heap.LastUsedFrame + cHeapCleanupLatency < m_FrameIndex)
			{
				std::swap(heaps[heapIndex], heaps[heaps.size() - 1]);
				heaps.pop_back();
				continue;
			}

			++heapIndex;

			// Retire physical resource if it hasn't been used for a while
			uint32 resourceIndex = 0;
			while (resourceIndex < (uint32)heap.PhysicalResources.size())
			{
				RGPhysicalResource* pResource = heap.PhysicalResources[resourceIndex].get();
				if (pResource->LastUsedFrame + cResourceCleanupLatency < m_FrameIndex)
				{
					std::swap(heap.PhysicalResources[resourceIndex], heap.PhysicalResources[heap.PhysicalResources.size() - 1]);
					heap.PhysicalResources.pop_back();
					continue;
				}
				++resourceIndex;
			}
		}
	}

	RGPhysicalResource* FindExistingResource(const DeviceResource* pResource, RGHeap** pOutHeap)
	{
		for (RGHeap& heap : m_Heaps)
		{
			for (UniquePtr<RGPhysicalResource>& resource : heap.PhysicalResources)
			{
				if (resource->pResource == pResource)
				{
					*pOutHeap = &heap;
					return resource.get();
				}
			}
		}
		return nullptr;
	}


	void ComputeResourceAliasing(Array<RGResource*>& resources, Array<RGHeap>& heaps)
	{
		PROFILE_CPU_SCOPE();

		// Clear all allocations and mark imported resources as allocations
		for (RGHeap& heap : m_Heaps)
			heap.Allocations.clear();

		for (RGResource* pResource : resources)
		{
			if (pResource->IsAllocated())
			{
				gAssert(pResource->IsImported && pResource->pPhysicalResource);

				RGHeap* pHeap = nullptr;
				RGPhysicalResource* pPhysicalResource = FindExistingResource(pResource->GetPhysicalUnsafe(), &pHeap);
				if (pPhysicalResource)
				{
					pResource->Offset = pPhysicalResource->Offset;
					pHeap->Allocations.push_back(pResource);
				}
			}
		}

		// Sort resources largest to smallest, then largest alignment to smallest.
		std::sort(resources.begin(), resources.end(), [](const RGResource* a, const RGResource* b) {
			if (a->Size == b->Size)
			{
				if (a->Alignment == b->Alignment)
					return a->ID.GetIndex() < b->ID.GetIndex();
				return a->Alignment > b->Alignment;
			}
			return a->Size > b->Size;
		});

		// Sort heaps largest to smallest, so smaller heaps can be removed.
		std::sort(heaps.begin(), heaps.end(), [](const RGHeap& a, const RGHeap& b) {
			if (a.Size == b.Size)
				return a.ID < b.ID;
			return a.Size > b.Size;
		});

		struct HeapOffset
		{
			uint64 Offset : 63;
			uint64 IsFreeBegin : 1;
		};
		Array<HeapOffset> freeRanges;

		for (RGResource* pResource : resources)
		{
			if (pResource->IsAllocated())
				continue;

			gAssert(pResource->Size != 0);

			RGHeap* pHeap = nullptr;
			for (RGHeap& heap : heaps)
			{
				// If the resource is larger than the heap, bail out. Heaps are sorted by size so if this one doesn't fit, none will
				if (pResource->Size > heap.Size)
					break;

				// Shrinking: If the found heap has no allocations and it's very large for the resource, don't allocate into it and instead continue searching so this heap has the chance to be released
				if (heap.Allocations.empty() && Math::AlignUp<uint64>(pResource->Size, cHeapAlignment) < heap.Size)
					continue;

				// Keep track of which memory ranges in the heap for the lifetime of the current resource are free
				freeRanges.clear();

				// Mark the start as free
				freeRanges.push_back({ 0, true });

				// For each allocated resource, if the lifetime overlaps, mark it as "not free"
				for (RGResource* pAllocatedResource : heap.Allocations)
				{
					if (pAllocatedResource->GetLifetime().Overlaps(pResource->GetLifetime()))
					{
						freeRanges.push_back({ pAllocatedResource->Offset, false });
						freeRanges.push_back({ pAllocatedResource->Offset + pAllocatedResource->Size, true });
					}
				}

				// Close the open free range
				freeRanges.push_back({ (uint64)heap.Size, false });

				// Sort the markup by offset.
				std::sort(freeRanges.begin(), freeRanges.end(), [](const HeapOffset& a, const HeapOffset& b) { return a.Offset < b.Offset; });

				RGPhysicalResource* pExistingResource = nullptr;

				// Algorithm: Keep track of how many "free" marks we've come across
				// If we hit the end of a free mark and the range is closed (== 0), then this range is free
				uint32 freeRangeCounter = 0;
				uint64 lastBeginOffset = 0;
				for (HeapOffset& heapOffset : freeRanges)
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
							uint64 alignedOffset = Math::AlignUp<uint64>(lastBeginOffset, pResource->Alignment);
							uint64 endOffset = heapOffset.Offset;
							if (alignedOffset + pResource->Size <= heapOffset.Offset)
							{
								// Try to find an already existing physical resource that fits the space and description
								gAssert(heap.pHeap || heap.PhysicalResources.empty(), "Heap can't have physical resources without an allocated heap");
								for (const std::unique_ptr<RGPhysicalResource>& pPhysicalResource : heap.PhysicalResources)
								{
									if (pPhysicalResource->LastUsedFrame != m_FrameIndex &&						// Must not already be used by another resource
										pPhysicalResource->Offset >= alignedOffset &&							// The physical resource must be within the region of the free range
										pPhysicalResource->Offset + pPhysicalResource->Size <= endOffset &&		// The physical resource must be within the region of the free range
										pPhysicalResource->IsCompatible(pResource))								// The physical resource description must match the vritual resource
									{
										pExistingResource = pPhysicalResource.get();
										pHeap			  = &heap;
										break;
									}
								}

								// No unused existing resource found, allocate resource in a new region
								pResource->Offset = alignedOffset;
								pHeap = &heap;
								break;
							}
						}
					}
				}

				if (pHeap)
				{
					// If an existing resource was found, assign it to the resource and mark it as used
					if (pExistingResource)
					{
						pResource->SetResource(pExistingResource->pResource);
						pResource->Offset					 = pExistingResource->Offset;
						pExistingResource->LastUsedFrame = m_FrameIndex;
					}

					break;
				}
			}

			// If no heap was found, that means the resource wasn't placed and a new heap is needed
			if (!pHeap)
			{
				RGHeap& heap = heaps.emplace_back();
				heap.ID		 = m_HeapID++;
				heap.Size	 = Math::AlignUp<uint64>(pResource->Size, cHeapAlignment);
				pHeap		 = &heap;
			}

			gAssert(pResource->Offset + pResource->Size <= pHeap->Size);

			// Assign the resource and mark the heap as used this frame
			pHeap->Allocations.push_back(pResource);
			pHeap->LastUsedFrame = m_FrameIndex;
		}

		// Validation
		for (RGHeap& heap : heaps)
		{
			for (const RGResource* pResource : heap.Allocations)
			{
				auto it = std::find_if(heap.Allocations.begin(), heap.Allocations.end(), [pResource](RGResource* pOther) {
					if (pOther == pResource)
						return false;
					return pResource->GetLifetime().Overlaps(pOther->GetLifetime()) && pResource->GetMemoryRange().Overlaps(pOther->GetMemoryRange());
				});
				RGResource* pOverlappingResource = it == heap.Allocations.end() ? nullptr : *it;
				gAssert(pOverlappingResource == nullptr,
					"Resource '%s' (Lifetime: [%d, %d], Memory: [%llu, %llu]) overlaps with Resource '%s' (Lifetime: [%d, %d], Memory: [%llu, %llu])",
					pResource->pName, pResource->GetLifetime().Begin, pResource->GetLifetime().End, pResource->GetMemoryRange().Begin, pResource->GetMemoryRange().End,
					pOverlappingResource->pName, pOverlappingResource->GetLifetime().Begin, pOverlappingResource->GetLifetime().End, pOverlappingResource->GetMemoryRange().Begin, pOverlappingResource->GetMemoryRange().End);
			}
		}



		for (RGHeap& heap : heaps)
		{
			if (heap.pHeap == nullptr)
			{
				D3D12_HEAP_DESC heapDesc{
					.SizeInBytes = heap.Size,
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
				VERIFY_HR(m_pDevice->GetDevice()->CreateHeap(&heapDesc, IID_PPV_ARGS(heap.pHeap.GetAddressOf())));
			}

			for (RGResource* pResource : heap.Allocations)
			{
				if (!pResource->IsAllocated())
				{
					heap.PhysicalResources.emplace_back(std::make_unique<RGPhysicalResource>());
					RGPhysicalResource* pPhysical = heap.PhysicalResources.back().get();
					pPhysical->Offset			  = pResource->Offset;
					pPhysical->Size				  = pResource->Size;
					pPhysical->LastUsedFrame	  = m_FrameIndex;

					if (pResource->GetType() == RGResourceType::Texture)
					{
						RGTexture* pTexture			   = static_cast<RGTexture*>(pResource);
						pPhysical->ResourceTextureDesc = pTexture->GetDesc();
						pPhysical->pResource		   = m_pDevice->CreateTexture(pTexture->GetDesc(), heap.pHeap, pResource->Offset, pResource->GetName());
					}
					else
					{
						RGBuffer* pBuffer			  = static_cast<RGBuffer*>(pResource);
						pPhysical->ResourceBufferDesc = pBuffer->GetDesc();
						pPhysical->pResource		  = m_pDevice->CreateBuffer(pBuffer->GetDesc(), heap.pHeap, pResource->Offset, pResource->GetName());
					}
					pResource->SetResource(pPhysical->pResource);
				}
			}
		}
	}


	GraphicsDevice* m_pDevice			 = nullptr;
	bool			m_LiveCapture = true;
	bool			m_AllocateResources = true;
	uint32			m_FrameIndex		 = 0;
	Array<RGHeap>	m_Heaps;
	uint32			m_HeapID = 0;

	static constexpr uint32 cHeapCleanupLatency		= 3;
	static constexpr uint32 cResourceCleanupLatency = 1024;
	static constexpr uint64 cHeapAlignment			= 2 * Math::MegaBytesToBytes;

	/****************/
	/*	 Debug UI	*/
	/****************/

	static ImColor sGetResourceColor(const RGResource* pResource)
	{
		float		hueMin	   = 0.0f;
		float		hueMax	   = 1.0f;
		const float saturation = 0.5f;
		const float value	   = 0.6f;
		float		hue		   = (float)std::hash<std::string>{}(pResource->GetName()) / std::numeric_limits<size_t>::max();
		hue					   = hueMin + hue * (hueMax - hueMin);
		float R				   = std::max(std::min(fabs(hue * 6 - 3) - 1, 1.0f), 0.0f);
		float G				   = std::max(std::min(2 - fabs(hue * 6 - 2), 1.0f), 0.0f);
		float B				   = std::max(std::min(2 - fabs(hue * 6 - 4), 1.0f), 0.0f);

		R = ((R - 1) * saturation + 1) * value;
		G = ((G - 1) * saturation + 1) * value;
		B = ((B - 1) * saturation + 1) * value;

		return ImColor(R, G, B, 1.0f);
	}

	void DrawDebugView(Span<RGResource*> resources, const Array<RGHeap>& heaps)
	{
		PROFILE_CPU_SCOPE();

		uint32 lastPassID = 0;
		for (const RGResource* pResource : resources)
			lastPassID = Math::Max(pResource->GetLifetime().End, lastPassID);

		// Show allocation sizes and heap layout
		if (ImGui::Begin("Heap Layout"))
		{
			uint64 totalResourcesSize = 0;
			for (const RGResource* pResource : resources)
				totalResourcesSize += pResource->Size;

			uint64 totalHeapSize = 0;
			for (const RGHeap& heap : heaps)
				totalHeapSize += heap.Size;

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

			float width = ImGui::GetContentRegionAvail().x;
			float widthScale = width / lastPassID;
			for (const RGHeap& heap : heaps)
			{
				ImGui::Text("Heap (Size: %s - Allocations: %d - Resources: %d)", Math::PrettyPrintDataSize(heap.Size).c_str(), heap.Allocations.size(), heap.PhysicalResources.size());
				ImDrawList* pDraw = ImGui::GetWindowDrawList();

				ImVec2 cursor = ImGui::GetCursorScreenPos();

				float heapHeight = log2f((float)heap.Size + 1);
				auto GetBarHeight = [&](uint64 size) { return (float)size / heap.Size * heapHeight; };

				pDraw->AddRectFilled(cursor, cursor + ImVec2(widthScale * (lastPassID + 1), heapHeight), ImColor(1.0f, 1.0f, 1.0f, 0.2f));
				for (const RGResource* pResource : heap.Allocations)
				{
					ImRect barRect(
						cursor + ImVec2(widthScale * pResource->GetLifetime().Begin, GetBarHeight(pResource->Offset)),
						cursor + ImVec2(widthScale * pResource->GetLifetime().End, GetBarHeight(pResource->Size + pResource->Offset))
					);

					if (ImGui::ItemAdd(barRect, pResource->ID.GetIndex()))
					{
						ImColor color = sGetResourceColor(pResource);
						if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
						{
							color.Value.x *= 1.5f;
							color.Value.y *= 1.5f;
							color.Value.z *= 1.5f;
							ImGui::Text("Name: %s", pResource->pName);
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

		// Show data of all captured resources
		if (ImGui::Begin("Resource Table"))
		{
			if (ImGui::BeginTable("TestTable", 5, ImGuiTableFlags_Resizable))
			{
				ImGui::TableHeader("Header");
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Size");
				ImGui::TableSetupColumn("Offset");
				ImGui::TableSetupColumn("Alignment");
				ImGui::TableSetupColumn("Lifetime");
				ImGui::TableHeadersRow();

				std::vector<const RGResource*> sortedResources(resources.GetSize());
				for (const RGResource* resource : resources)
				{
					sortedResources.resize(Math::Max<uint32>(resource->ID.GetIndex() + 1, (uint32)sortedResources.size()));
					sortedResources[resource->ID.GetIndex()] = resource;
				}

				for (const RGResource* pResource : sortedResources)
				{
					if (!pResource)
						continue;

					ImGui::PushID(pResource->ID.GetIndex());
					ImGui::TableNextColumn();
					ImGui::ColorButton("##color", sGetResourceColor(pResource));
					ImGui::SameLine();
					ImGui::Text("%s", pResource->pName);
					ImGui::TableNextColumn();
					ImGui::Text("%s", Math::PrettyPrintDataSize(pResource->Size).c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%s", Math::PrettyPrintDataSize(pResource->Offset).c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%s", Math::PrettyPrintDataSize(pResource->Alignment).c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%d -> %d", pResource->GetLifetime().Begin, pResource->GetLifetime().End);
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}
		ImGui::End();
	}
};

extern RGAllocator gRenderGraphAllocator;
