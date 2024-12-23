#pragma once

#include <imgui_internal.h>
#include "RenderGraphDefinitions.h"
#include "RenderGraph.h"

class RGAllocator
{
public:
	inline static bool liveCapture = false;
	inline static bool preferHeaps = true;
	inline static bool bestFit = false;

	struct Resource
	{
		std::string		Name;
		int				Size;
		int				Alignment;
		URange			Lifetime;
		uint64			Offset = 0xFFFFFFFF;
		int				ID;

		ImColor GetColor()
		{
			float hueMin = 0.0f;
			float hueMax = 1.0f;
			const float saturation = 0.5f;
			const float value = 0.6f;
			float hue = (float)std::hash<std::string>{}(Name) / std::numeric_limits<size_t>::max();
			hue = hueMin + hue * (hueMax - hueMin);
			float R = std::max(std::min(fabs(hue * 6 - 3) - 1, 1.0f), 0.0f);
			float G = std::max(std::min(2 - fabs(hue * 6 - 2), 1.0f), 0.0f);
			float B = std::max(std::min(2 - fabs(hue * 6 - 4), 1.0f), 0.0f);

			R = ((R - 1) * saturation + 1) * value;
			G = ((G - 1) * saturation + 1) * value;
			B = ((B - 1) * saturation + 1) * value;

			return ImColor(R, G, B, 1.0f);
		}

	};

	struct Heap
	{
		Array<Resource*> Allocations;
		uint64 Size;
	};

	static D3D12_RESOURCE_DESC GetResourceDesc(const RGResource* pResource)
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

	static Array<Resource>& GetResources(GraphicsDevice* pDevice, Span<RGResource*> graphResources, bool clear)
	{
		PROFILE_CPU_SCOPE();

		static Array<Resource> resources;
		if (!clear)
			return resources;

		resources.clear();
		for (RGResource* pResource : graphResources)
		{
			if (pResource->IsImported || pResource->IsExported || !pResource->GetPhysicalUnsafe())
				continue;

			Resource& resource = resources.emplace_back();
			resource.Lifetime = pResource->GetLifetime();
			resource.ID = pResource->ID.GetIndex();
			resource.Name = pResource->GetName();

			D3D12_RESOURCE_DESC resourceDesc = GetResourceDesc(pResource);

			uint64 size, alignment;
			D3D::GetResourceAllocationInfo(pDevice->GetDevice(), resourceDesc, size, alignment);

			resource.Size = (int)size;
			resource.Alignment = (int)alignment;
		}
		return resources;
	}

	static void AliasingExperiment(GraphicsDevice* pDevice, Span<RGResource*> graphResources)
	{
		PROFILE_CPU_SCOPE();
		Array<Resource>& resources = GetResources(pDevice, graphResources, liveCapture);

		Array<Heap> heaps;
		ComputeAliasing_V1(resources, heaps);
		DrawDebugView(resources, heaps);
	}

	static void ComputeAliasing_V1(Array<Resource>& resources, Array<Heap>& heaps)
	{
		PROFILE_CPU_SCOPE();

		if (!preferHeaps)
			heaps.push_back({ {}, 0 });

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
							uint64 alignedOffset = Math::AlignUp<uint64>(lastBeginOffset, resource.Alignment);
							uint64 endOffset = heapOffset.Offset;
							if (alignedOffset + resource.Size <= heapOffset.Offset)
							{
								uint64 rangeSize = endOffset - alignedOffset;
								if (rangeSize < smallestRange)
								{
									smallestRange = rangeSize;
									resource.Offset = alignedOffset;
									pHeap = &heap;

									// If we're not looking for the best fit, we're done. Otherwise keep looking
									if (!bestFit)
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
				if (preferHeaps)
				{
					Heap& heap = heaps.emplace_back();
					heap.Size = resource.Size;
					resource.Offset = 0;
					pHeap = &heap;
				}
				else
				{
					Heap& heap = heaps.back();
					uint64 alignedOffset = Math::AlignUp<uint64>(heap.Size, resource.Alignment);
					resource.Offset = alignedOffset;
					heap.Size = alignedOffset + resource.Size;
					pHeap = &heap;
				}
			}

			pHeap->Allocations.push_back(&resource);
		}
	}

	static void DrawDebugView(Array<Resource>& resources, Array<Heap>& heaps)
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
				ImGui::Text("Heap (Size: %s - Allocations: %d)", Math::PrettyPrintDataSize(heap.Size).c_str(), heap.Allocations.size());
				ImDrawList* pDraw = ImGui::GetWindowDrawList();

				ImVec2 cursor = ImGui::GetCursorScreenPos();

				float heapHeight = preferHeaps ? log2f((float)heap.Size + 1) : ImGui::GetContentRegionAvail().y;
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
						ImColor color = resource->GetColor();
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
			ImGui::Checkbox("Live Capture", &liveCapture);
			ImGui::SameLine();
			ImGui::Checkbox("Prefer heaps", &preferHeaps);
			ImGui::SameLine();
			ImGui::Checkbox("Best Fit", &bestFit);
			if (ImGui::Button("Clear All"))
				resources.clear();

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
					ImGui::ColorButton("##color", resource.GetColor());
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
