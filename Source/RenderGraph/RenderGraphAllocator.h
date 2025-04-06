#pragma once

#include <imgui_internal.h>
#include "RenderGraphDefinitions.h"
#include "RenderGraph.h"

class RGAllocator
{
private:
	struct RGDeviceResource
	{
		Ref<DeviceResource> pResource;
		D3D12_RESOURCE_DESC Desc;
		uint64				Offset;
		uint64				Size;
		uint32				LastUsedFrame = 0;
	};

	struct Resource
	{
		std::string			Name;
		int					Size;
		int					Alignment;
		URange				Lifetime;
		uint64				Offset = 0xFFFFFFFF;
		int					ID;
		D3D12_RESOURCE_DESC ResourceDesc;
		RGDeviceResource*	pPhysicalResource = nullptr;
		RGResourceType		Type;
		BufferDesc			BufferDesc;
		TextureDesc			TextureDesc;
	};

	struct Heap
	{
		Array<Resource*>				   Allocations;
		uint64							   Size;
		Ref<ID3D12Heap>					   pHeap;
		Array<UniquePtr<RGDeviceResource>> PhysicalResources;
		uint32							   LastUsedFrame = 0;
	};
	Array<Heap> m_Heaps;

	Array<Resource> m_CachedResources;

public:

	void AliasingExperiment(GraphicsDevice* pDevice, Span<RGResource*> graphResources)
	{
		PROFILE_CPU_SCOPE();

		if (m_LiveCapture)
		{
			m_CachedResources = {};
			GetResources(pDevice, graphResources, m_CachedResources);
		}

		for (Heap& heap : m_Heaps)
			heap.Allocations = {};

		if (m_UsePlacedResources)
			ComputeAliasing_PlacedResources_V1(m_CachedResources, m_Heaps);
		else
			ComputeAliasing_CommittedResources(m_CachedResources, m_Heaps);

		ClearUnusedResources(m_Heaps);

		AllocateResources(pDevice, m_Heaps);


		DrawDebugView(m_CachedResources, m_Heaps);
	}

	void ClearAll()
	{
		m_CachedResources.clear();
		m_Heaps.clear();
	}

	void Shutdown()
	{
		ClearAll();
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
			const BufferDesc desc = pBuffer->GetDesc();
			return CD3DX12_RESOURCE_DESC::Buffer(desc.Size);
		}
	}

	static void GetResources(GraphicsDevice* pDevice, Span<RGResource*> graphResources, Array<Resource>& outResources)
	{
		PROFILE_CPU_SCOPE();

		for (RGResource* pResource : graphResources)
		{
			if (pResource->IsImported || pResource->IsExported || !pResource->GetPhysicalUnsafe())
				continue;

			Resource& resource = outResources.emplace_back();
			resource.Lifetime = pResource->GetLifetime();
			resource.ID = pResource->ID.GetIndex();
			resource.Name = pResource->GetName();
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
	void ComputeAliasing_CommittedResources(Array<Resource>& resources, Array<Heap>& heaps)
	{
		PROFILE_CPU_SCOPE();

		for (Resource& resource : resources)
		{
			Heap* pHeap = nullptr;
			for (Heap& heap : heaps)
			{
				const Resource* pAllocation = heap.Allocations.front();
				D3D::ResourceDescEqual eq;
				if (eq.operator()(pAllocation->ResourceDesc, resource.ResourceDesc))
				{
					bool anyOverlap = false;
					for (const Resource* pExisting : heap.Allocations)
					{
						if (pExisting->Lifetime.Overlaps(resource.Lifetime))
						{
							anyOverlap = true;
							break;
						}
					}

					if (!anyOverlap)
					{
						pHeap = &heap;
					}
				}
			}

			if (!pHeap)
			{
				Heap& heap = heaps.emplace_back();
				heap.Size = resource.Size;
				pHeap = &heap;
			}

			resource.Offset = 0;
			pHeap->Allocations.push_back(&resource);
		}
	}

	void ClearUnusedResources(Array<Heap>& heaps)
	{
		PROFILE_CPU_SCOPE();

		if (0)
		{
			for (Heap& heap : heaps)
			{
				uint32 i = 0;
				while (i < (uint32)heap.PhysicalResources.size())
				{
					RGDeviceResource* pResource = heap.PhysicalResources[i].get();
					if (pResource->LastUsedFrame + 100 < m_FrameIndex)
					{
						std::swap(heap.PhysicalResources[i], heap.PhysicalResources[heap.PhysicalResources.size() - 1]);
						heap.PhysicalResources.pop_back();
					}
					else
					{
						++i;
					}
				}
			}
		}
		{
			uint32 i = 0;
			while (i < (uint32)heaps.size())
			{
				Heap& heap = heaps[i];
				if (heap.LastUsedFrame + 100 < m_FrameIndex)
				{
					heaps.erase(heaps.begin() + i);
				}
				else
				{
					++i;
				}
			}
		}
	}

	void ComputeAliasing_PlacedResources_V1(Array<Resource>& resources, Array<Heap>& heaps)
	{
		PROFILE_CPU_SCOPE();

		// Sort resources largest to smallest, then largest alignment to smallest.
		std::sort(resources.begin(), resources.end(), [](const Resource& a, const Resource& b)
			{
				if (a.Size == b.Size)
					return a.Alignment > b.Alignment;
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
			if (resource.Size == 0)
				break;

			Heap* pHeap = nullptr;
			for (Heap& heap : heaps)
			{
				if (resource.Size > heap.Size)
					continue;

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
								auto it = std::find_if(heap.PhysicalResources.begin(), heap.PhysicalResources.end(), [&](const std::unique_ptr<RGDeviceResource>& pPhysicalResource) {
									if (pPhysicalResource->LastUsedFrame != m_FrameIndex && pPhysicalResource->Offset >= alignedOffset && pPhysicalResource->Offset + pPhysicalResource->Size <= endOffset)
									{
										D3D::ResourceDescEqual eq;
										return eq(pPhysicalResource->Desc, resource.ResourceDesc);
									}
									return false;
								});

								if (it != heap.PhysicalResources.end())
								{
									resource.pPhysicalResource = it->get();
									resource.Offset			   = resource.pPhysicalResource->Offset;
									pHeap					   = &heap;
									break;
								}

								uint64 rangeSize = endOffset - alignedOffset;
								if (rangeSize < smallestRange)
								{
									smallestRange = rangeSize;
									resource.Offset = alignedOffset;
									pHeap = &heap;

									// If we're not looking for the best fit, we're done. Otherwise keep looking
									if (!m_BestFit)
										break;
								}
							}
						}
					}
				}
				if (pHeap)
					break;
			}

			if (!pHeap)
			{
				if (m_PreferHeaps)
				{
					Heap& heap = heaps.emplace_back();
					heap.Size = Math::AlignUp<uint64>(resource.Size, 4ull*Math::MegaBytesToBytes);
					resource.Offset = 0;
					pHeap = &heap;
				}
				else
				{
					if (heaps.empty())
						heaps.push_back({ {}, 0 });
					Heap& heap = heaps.back();
					uint64 alignedOffset = Math::AlignUp<uint64>(heap.Size, resource.Alignment);
					resource.Offset = alignedOffset;
					heap.Size = alignedOffset + resource.Size;
					pHeap = &heap;
				}
			}

			pHeap->Allocations.push_back(&resource);
			pHeap->LastUsedFrame = m_FrameIndex;
		}

		++m_FrameIndex;
	}

	void AllocateResources(GraphicsDevice* pDevice, Array<Heap>& heaps)
	{
		PROFILE_CPU_SCOPE();

		for (Heap& heap : heaps)
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
					RGDeviceResource* pPhysical	 = new RGDeviceResource();
					pPhysical->Desc				 = pResource->ResourceDesc;
					pPhysical->Offset			 = pResource->Offset;
					pPhysical->Size				 = pResource->Size;
					pResource->pPhysicalResource = pPhysical;
					heap.PhysicalResources.emplace_back(std::move(pPhysical));

					if (pResource->Type == RGResourceType::Texture)
						pDevice->CreateTexture(pResource->TextureDesc, heap.pHeap, pResource->Offset, pResource->Name.c_str());
					else
						pDevice->CreateBuffer(pResource->BufferDesc, heap.pHeap, pResource->Offset, pResource->Name.c_str());
				}
				pResource->pPhysicalResource->LastUsedFrame = m_FrameIndex;
			}
		}
	}

	bool   m_LiveCapture		= false;
	bool   m_PreferHeaps		= true;
	bool   m_BestFit			= false;
	bool   m_UsePlacedResources = true;
	uint32 m_FrameIndex			= 0;


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

	void DrawDebugView(Array<Resource>& resources, Array<Heap>& heaps)
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
			for (Heap& heap : heaps)
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
			for (Heap& heap : heaps)
			{
				ImGui::Text("Heap (Size: %s - Allocations: %d - Resources: %d)", Math::PrettyPrintDataSize(heap.Size).c_str(), heap.Allocations.size(), heap.PhysicalResources.size());
				ImDrawList* pDraw = ImGui::GetWindowDrawList();

				ImVec2 cursor = ImGui::GetCursorScreenPos();

				float heapHeight = m_PreferHeaps ? log2f((float)heap.Size + 1) : ImGui::GetContentRegionAvail().y;
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
			ImGui::Checkbox("Prefer heaps", &m_PreferHeaps);
			ImGui::SameLine();
			ImGui::Checkbox("Best Fit", &m_BestFit);
			ImGui::SameLine();
			ImGui::Checkbox("Placed Resources", &m_UsePlacedResources);
			if (ImGui::Button("Clear All"))
			{
				ClearAll();
			}

			if (ImGui::BeginTable("TestTable", 5))
			{
				ImGui::TableHeader("Header");
				ImGui::TableSetupColumn("Color");
				ImGui::TableSetupColumn("Size");
				ImGui::TableSetupColumn("Alignment");
				ImGui::TableSetupColumn("Lifetime");
				ImGui::TableSetupColumn("Remove?");
				ImGui::TableHeadersRow();
				int idx = 0;
				for (Resource& resource : resources)
				{
					ImGui::PushID(resource.ID);
					ImGui::TableNextColumn();
					ImGui::ColorButton("##color", sGetResourceColor(resource));
					ImGui::SameLine();
					ImGui::Text("%s", resource.Name.c_str());
					ImGui::TableNextColumn();
					ImGui::SliderInt("##size", &resource.Size, 0, Math::MegaBytesToBytes * 10);
					ImGui::TableNextColumn();
					ImGui::SliderInt("##alignment", &resource.Alignment, 16, Math::KilobytesToBytes * 64);
					ImGui::TableNextColumn();
					IRange lifetime(resource.Lifetime.Begin, resource.Lifetime.End);
					ImGui::DragIntRange2("##lifetime", &lifetime.Begin, &lifetime.End, 1.0f, 0, lastPassID);
					resource.Lifetime = URange(lifetime.Begin, lifetime.End);
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

			static int ID = 0xFFFF;
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
