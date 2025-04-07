#pragma once

#include <imgui_internal.h>
#include "RenderGraphDefinitions.h"
#include "RenderGraph.h"

class RGAllocator
{
private:
	// Represents a physical resources
	struct RGPhysicalResource
	{
		Ref<DeviceResource> pResource;
		D3D12_RESOURCE_DESC Desc;
		uint64				Offset;
		uint64				Size;
		uint32				LastUsedFrame = 0;

		bool IsCompatible(const D3D12_RESOURCE_DESC& desc)
		{
			D3D::ResourceDescEqual eq;
			return eq(Desc, desc);
		}
	};

	// Represents a render graph resource (to be replaced with RGResource)
	struct Resource
	{
		std::string			Name;
		uint64				Size;
		uint32 				Alignment;
		URange				Lifetime;
		uint64				Offset = 0xFFFFFFFF;
		uint32				ID;
		D3D12_RESOURCE_DESC ResourceDesc;
		RGPhysicalResource*	pPhysicalResource = nullptr;
		RGResourceType		Type;
		BufferDesc			BufferDesc;
		TextureDesc			TextureDesc;
	};

	// Represents a physical resource heap
	struct RGHeap
	{
		uint32							   ID;
		Array<Resource*>				   Allocations;
		uint64							   Size;
		uint32							   LastUsedFrame = 0;

		Array<UniquePtr<RGPhysicalResource>> PhysicalResources;
		Ref<ID3D12Heap>					   pHeap;
	};

public:
	void Shutdown()
	{
		ClearAll();
	}

	void AllocateResources(GraphicsDevice* pDevice, Span<RGResource*> graphResources)
	{
		PROFILE_CPU_SCOPE();

		if (m_LiveCapture)
		{
			m_CachedResources = {};
			GetResources(pDevice, graphResources, m_CachedResources);
		}

		for (RGHeap& heap : m_Heaps)
			heap.Allocations.clear();

		ComputeResourceAliasing(m_CachedResources, m_Heaps);

		ClearUnusedResources(m_Heaps);

		if (m_LiveCapture && m_AllocateResources)
			AllocatePhysicalResources(pDevice, m_Heaps);

		++m_FrameIndex;
	}

	void DrawDebugView()
	{
		DrawDebugView(m_CachedResources, m_Heaps);
	}

	void ClearAll()
	{
		m_CachedResources.clear();
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

	static void GetResources(GraphicsDevice* pDevice, Span<RGResource*> graphResources, Array<Resource>& outResources)
	{
		PROFILE_CPU_SCOPE();

		for (RGResource* pResource : graphResources)
		{
			if (pResource->IsImported || !pResource->GetPhysicalUnsafe())
				continue;

			Resource& resource = outResources.emplace_back();
			resource.Lifetime  = pResource->GetLifetime();
			resource.ID		   = pResource->ID.GetIndex();
			resource.Name	   = pResource->GetName();
			resource.Type	   = pResource->GetType();
			if (pResource->GetType() == RGResourceType::Texture)
				resource.TextureDesc = static_cast<RGTexture*>(pResource)->GetDesc();
			else
				resource.BufferDesc = static_cast<RGBuffer*>(pResource)->GetDesc();

			resource.ResourceDesc = sGetResourceDesc(pResource);

			uint64 size, alignment;
			D3D::GetResourceAllocationInfo(pDevice->GetDevice(), resource.ResourceDesc, size, alignment);

			resource.Size = (int)size;
			resource.Alignment = (int)alignment;
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

	void ComputeResourceAliasing(Array<Resource>& resources, Array<RGHeap>& heaps)
	{
		PROFILE_CPU_SCOPE();

		// Sort resources largest to smallest, then largest alignment to smallest.
		std::sort(resources.begin(), resources.end(), [](const Resource& a, const Resource& b) {
			if (a.Size == b.Size)
			{
				if (a.Alignment == b.Alignment)
					return a.ID < b.ID;
				return a.Alignment > b.Alignment;
			}
			return a.Size > b.Size;
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

		for (Resource& resource : resources)
		{
			if (resource.pPhysicalResource)
				continue;

			if (resource.Size == 0)
				continue;

			RGHeap* pHeap = nullptr;
			for (RGHeap& heap : heaps)
			{
				// If the resource is larger than the heap, bail out. Heaps are sorted by size so if this one doesn't fit, none will
				if (resource.Size > heap.Size)
					break;

				// Keep track of which memory ranges in the heap for the lifetime of the current resource are free
				freeRanges.clear();

				// Mark the start as free
				freeRanges.push_back({ 0, true });

				// For each allocated resource, if the lifetime overlaps, mark it as "not free"
				for (Resource* existing : heap.Allocations)
				{
					if (existing->Lifetime.Overlaps(resource.Lifetime))
					{
						freeRanges.push_back({ existing->Offset, false });
						freeRanges.push_back({ existing->Offset + existing->Size, true });
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
				uint64 smallestRange = ~0ull;
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
							uint64 alignedOffset = Math::AlignUp<uint64>(lastBeginOffset, resource.Alignment);
							uint64 endOffset = heapOffset.Offset;
							if (alignedOffset + resource.Size <= heapOffset.Offset)
							{
								// Try to find an already existing physical resource that fits the space and description
								gAssert(heap.pHeap || heap.PhysicalResources.empty(), "Heap can't have physical resources without an allocated heap");
								auto it = std::find_if(heap.PhysicalResources.begin(), heap.PhysicalResources.end(), [&](const std::unique_ptr<RGPhysicalResource>& pPhysicalResource) {
									return
										pPhysicalResource->LastUsedFrame != m_FrameIndex &&						// Must not already be used by another resource
										pPhysicalResource->Offset >= alignedOffset &&							// The physical resource must be within the region of the free range
										pPhysicalResource->Offset + pPhysicalResource->Size <= endOffset &&		// The physical resource must be within the region of the free range
										pPhysicalResource->IsCompatible(resource.ResourceDesc);					// The physical resource description must match the vritual resource
								});

								// Found an existing physical resource that matches the description. Finished
								if (it != heap.PhysicalResources.end())
								{
									pHeap			  = &heap;
									pExistingResource = it->get();
									break;
								}

								// No unused existing resource found, allocate resource in a new region
								uint64 rangeSize = endOffset - alignedOffset;
								if (rangeSize < smallestRange)
								{
									smallestRange = rangeSize;
									resource.Offset = alignedOffset;
									pHeap = &heap;

									// We're done
									break;
								}
							}
						}
					}
				}

				if (pHeap)
				{
					// Shrinking: If the found heap has no allocations and it's very large for the resource, don't allocate into it and instead continue searching so this heap has the chance to be released
					if (pHeap->Allocations.empty() && Math::AlignUp<uint64>(resource.Size, cHeapAlignment) < pHeap->Size)
					{
						pHeap = nullptr;
						continue;
					}

					// If an existing resource was found, assign it to the resource and mark it as used
					if (pExistingResource)
					{
						resource.pPhysicalResource		 = pExistingResource;
						resource.Offset					 = pExistingResource->Offset;
						pExistingResource->LastUsedFrame = m_FrameIndex;
					}

					break;
				}
			}

			// If no heap was found, that means the resource wasn't placed and a new heap is needed
			if (!pHeap)
			{
				RGHeap& heap	= heaps.emplace_back();
				heap.ID			= m_HeapID++;
				heap.Size		= Math::AlignUp<uint64>(resource.Size, cHeapAlignment);
				resource.Offset = 0;
				pHeap			= &heap;
			}

			// Assign the resource and mark the heap as used this frame
			pHeap->Allocations.push_back(&resource);
			pHeap->LastUsedFrame = m_FrameIndex;
		}
	}

	void AllocatePhysicalResources(GraphicsDevice* pDevice, Array<RGHeap>& heaps)
	{
		PROFILE_CPU_SCOPE();

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
				VERIFY_HR(pDevice->GetDevice()->CreateHeap(&heapDesc, IID_PPV_ARGS(heap.pHeap.GetAddressOf())));
			}

			for (Resource* pResource : heap.Allocations)
			{
				if (!pResource->pPhysicalResource)
				{
					heap.PhysicalResources.emplace_back(new RGPhysicalResource);
					RGPhysicalResource* pPhysical	 = heap.PhysicalResources.back().get();
					pPhysical->Desc				 = pResource->ResourceDesc;
					pPhysical->Offset			 = pResource->Offset;
					pPhysical->Size				 = pResource->Size;
					pPhysical->LastUsedFrame	 = m_FrameIndex;
					pResource->pPhysicalResource = pPhysical;

					if (pResource->Type == RGResourceType::Texture)
						pDevice->CreateTexture(pResource->TextureDesc, heap.pHeap, pResource->Offset, pResource->Name.c_str());
					else
						pDevice->CreateBuffer(pResource->BufferDesc, heap.pHeap, pResource->Offset, pResource->Name.c_str());
				}
			}
		}
	}

	bool			m_LiveCapture = true;
	bool			m_AllocateResources = true;
	uint32			m_FrameIndex		 = 0;
	Array<RGHeap>	m_Heaps;
	Array<Resource> m_CachedResources;
	uint32			m_HeapID = 0;

	static constexpr uint32 cHeapCleanupLatency		= 3;
	static constexpr uint32 cResourceCleanupLatency = 1024;
	static constexpr uint64 cHeapAlignment			= 2 * Math::MegaBytesToBytes;

	/****************/
	/*	 Debug UI	*/
	/****************/

	static ImColor sGetResourceColor(const Resource& resource)
	{
		float		hueMin	   = 0.0f;
		float		hueMax	   = 1.0f;
		const float saturation = 0.5f;
		const float value	   = 0.6f;
		float		hue		   = (float)std::hash<std::string>{}(resource.Name) / std::numeric_limits<size_t>::max();
		hue					   = hueMin + hue * (hueMax - hueMin);
		float R				   = std::max(std::min(fabs(hue * 6 - 3) - 1, 1.0f), 0.0f);
		float G				   = std::max(std::min(2 - fabs(hue * 6 - 2), 1.0f), 0.0f);
		float B				   = std::max(std::min(2 - fabs(hue * 6 - 4), 1.0f), 0.0f);

		R = ((R - 1) * saturation + 1) * value;
		G = ((G - 1) * saturation + 1) * value;
		B = ((B - 1) * saturation + 1) * value;

		return ImColor(R, G, B, 1.0f);
	}

	void DrawDebugView(Array<Resource>& resources, Array<RGHeap>& heaps)
	{
		PROFILE_CPU_SCOPE();

		uint32 lastPassID = 0;
		for (Resource& resource : resources)
			lastPassID = Math::Max(resource.Lifetime.End, lastPassID);

		// Show allocation sizes and heap layout
		if (ImGui::Begin("Heap Layout"))
		{
			uint64 totalResourcesSize = 0;
			for (Resource& resource : resources)
				totalResourcesSize += resource.Size;

			uint64 totalHeapSize = 0;
			for (RGHeap& heap : heaps)
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
			for (RGHeap& heap : heaps)
			{
				ImGui::Text("Heap (Size: %s - Allocations: %d - Resources: %d)", Math::PrettyPrintDataSize(heap.Size).c_str(), heap.Allocations.size(), heap.PhysicalResources.size());
				ImDrawList* pDraw = ImGui::GetWindowDrawList();

				ImVec2 cursor = ImGui::GetCursorScreenPos();

				float heapHeight = log2f((float)heap.Size + 1);
				auto GetBarHeight = [&](uint64 size) { return (float)size / heap.Size * heapHeight; };

				pDraw->AddRectFilled(cursor, cursor + ImVec2(widthScale * (lastPassID + 1), heapHeight), ImColor(1.0f, 1.0f, 1.0f, 0.2f));
				for (Resource* resource : heap.Allocations)
				{
					ImRect barRect(
						cursor + ImVec2(widthScale * resource->Lifetime.Begin, GetBarHeight(resource->Offset)),
						cursor + ImVec2(widthScale * (resource->Lifetime.End + 1), GetBarHeight(resource->Size + resource->Offset))
					);

					if (ImGui::ItemAdd(barRect, resource->ID))
					{
						ImColor color = sGetResourceColor(*resource);
						if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
						{
							color.Value.x *= 1.5f;
							color.Value.y *= 1.5f;
							color.Value.z *= 1.5f;
							ImGui::Text("Name: %s", resource->Name.c_str());
							ImGui::Text("Size: %s", Math::PrettyPrintDataSize(resource->Size).c_str());
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
			int remove = -1;
			int duplicate = -1;
			ImGui::Checkbox("Live Capture", &m_LiveCapture);
			ImGui::SameLine();
			if (ImGui::Button("Clear All"))
			{
				ClearAll();
			}

			if (ImGui::BeginTable("TestTable", 6, ImGuiTableFlags_Resizable))
			{
				ImGui::TableHeader("Header");
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Size");
				ImGui::TableSetupColumn("Offset");
				ImGui::TableSetupColumn("Alignment");
				ImGui::TableSetupColumn("Lifetime");
				ImGui::TableSetupColumn("Remove?");
				ImGui::TableHeadersRow();
				int idx = 0;

				std::vector<Resource*> sortedResources(resources.size());
				for (Resource& resource : resources)
				{
					sortedResources.resize(Math::Max(resource.ID + 1, (uint32)sortedResources.size()));
					sortedResources[resource.ID] = &resource;
				}

				for (Resource* pResource : sortedResources)
				{
					if (!pResource)
						continue;

					ImGui::PushID(pResource->ID);
					ImGui::TableNextColumn();
					ImGui::ColorButton("##color", sGetResourceColor(*pResource));
					ImGui::SameLine();
					ImGui::Text("%s", pResource->Name.c_str());
					ImGui::TableNextColumn();
					int size = (int)pResource->Size;
					if (ImGui::SliderInt("##size", &size, 0, Math::MegaBytesToBytes * 10))
						pResource->Size = size;
					ImGui::TableNextColumn();
					ImGui::Text("%d", pResource->Offset);
					ImGui::TableNextColumn();
					int alignment = (int)pResource->Alignment;
					if (ImGui::SliderInt("##alignment", &alignment, 16, Math::KilobytesToBytes * 64))
						pResource->Alignment = alignment;
					ImGui::TableNextColumn();
					IRange lifetime(pResource->Lifetime.Begin, pResource->Lifetime.End);
					ImGui::DragIntRange2("##lifetime", &lifetime.Begin, &lifetime.End, 1.0f, 0, lastPassID);
					pResource->Lifetime = URange(lifetime.Begin, lifetime.End);
					ImGui::TableNextColumn();
					if (ImGui::Button("Remove"))
						remove = idx;
					ImGui::SameLine();
					if (ImGui::Button("Duplicate"))
						duplicate = idx;
					++idx;
					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			static uint32 ID = 0xFFFF;
			if (ImGui::Button("Add resource"))
			{
				resources.push_back({ "New Resource", 100000028, 1, {0,100}, 0, ID++ });
			}

			if (remove >= 0)
			{
				std::swap(resources[remove], resources[resources.size() - 1]);
				resources.erase(resources.begin() + remove);
			}
			if (duplicate >= 0)
			{
				Resource newResource = resources[duplicate];
				newResource.ID = ID++;
				resources.insert(resources.begin() + duplicate + 1, newResource);
			}
		}
		ImGui::End();
	}
};

extern RGAllocator gRenderGraphAllocator;
