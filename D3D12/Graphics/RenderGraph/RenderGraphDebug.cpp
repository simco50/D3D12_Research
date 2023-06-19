#include "stdafx.h"
#include "RenderGraph.h"
#include "imgui_internal.h"
#include "Graphics/ImGuiRenderer.h"

template<typename T>
std::string BitmaskToString(T mask, const char* (*pValueToString)(T))
{
	std::string outString;
	uint32 value = (uint32)mask;

	if (value == 0)
	{
		const char* pStr = pValueToString((T)0);
		return pStr ? pStr : "NONE";
	}

	uint32 bitIndex = 0;
	uint32 valueIndex = 0;
	while (value > 0)
	{
		if (value & 1)
		{
			const char* pStr = pValueToString((T)(1 << bitIndex));
			if (pStr)
			{
				if (valueIndex > 0)
					outString += '/';
				outString += pStr;
				valueIndex++;
			}
		}
		bitIndex++;
		value >>= 1;
	}
	return outString;
}

std::string PassFlagToString(RGPassFlag flags)
{
	return BitmaskToString<RGPassFlag>(flags,
		[](RGPassFlag flag) -> const char*
		{
			switch (flag)
			{
			case RGPassFlag::None: return "None";
			case RGPassFlag::Compute: return "Compute";
			case RGPassFlag::Raster: return "Raster";
			case RGPassFlag::Copy: return "Copy";
			case RGPassFlag::NeverCull: return "Never Cull";
			case RGPassFlag::NoRenderPass: return "No Render Pass";
			default: return nullptr;
			}
		});
}

void RGGraph::DrawResourceTracker(bool& enabled) const
{
	if (!enabled)
		return;

	// Hacks to rotate text in ImGui. From https://github.com/ocornut/imgui/issues/1286#issue-251214314
	int rotation_start_index;
	auto ImRotateStart = [&]() {	rotation_start_index = ImGui::GetWindowDrawList()->VtxBuffer.Size; };
	auto ImRotateEnd = [&](float rad, ImVec2 center)
	{
		float s = sin(rad), c = cos(rad);
		center = ImRotate(center, s, c) - center;

		auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
		for (int i = rotation_start_index; i < buf.Size; i++)
			buf[i].pos = ImRotate(buf[i].pos, s, c) - center;
	};

	if(ImGui::Begin("Resource usage", &enabled, ImGuiWindowFlags_HorizontalScrollbar))
	{
		int32 passIndex = 0;
		int32 resourceIndex = 0;

		float passNameHeight = 300.0f;
		float resourceNameWidth = 300.0f;
		ImVec2 boxSize = ImVec2(20.0f, ImGui::GetTextLineHeightWithSpacing());
		float width = (int)m_RenderPasses.size() * boxSize.x + resourceNameWidth;
		float height = 1200;

		ImGui::BeginChild("Table", ImVec2(width, height));
		ImDrawList* pCmd = ImGui::GetWindowDrawList();

		ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImVec2 passNamePos = cursor + ImVec2(resourceNameWidth, 0);

		const RGPass* pActivePass = nullptr;
		for (const RGPass* pPass : m_RenderPasses)
		{
			ImRect itemRect(passNamePos + ImVec2(passIndex * boxSize.x, 0.0f), passNamePos + ImVec2((passIndex + 1) * boxSize.x, passNameHeight));
			pCmd->AddLine(itemRect.Max, itemRect.Max + ImVec2(0, height), ImColor(1.0f, 1.0f, 1.0f, 0.2f));
			ImRotateStart();
			ImVec2 size = ImGui::CalcTextSize(pPass->GetName());
			pCmd->AddText(itemRect.Max - ImVec2(size.x, 0), ImColor(1.0f, 1.0f, 1.0f), pPass->GetName());
			ImRotateEnd(Math::PI * 2.2f, itemRect.Max + ImVec2(boxSize.x, 0));
			ImGui::ItemAdd(itemRect, passIndex);
			bool passActive = ImGui::IsItemHovered();
			if (passActive)
			{
				ImGui::BeginTooltip();
				ImGui::Text("%s", pPass->GetName());
				ImGui::Text("Flags: %s", PassFlagToString(pPass->Flags).c_str());
				ImGui::Text("Index: %d", passIndex);
				ImGui::EndTooltip();

				pActivePass = pPass;
			}
			++passIndex;
		}

		cursor += ImVec2(0.0f, passNameHeight);
		ImVec2 resourceAccessPos = cursor + ImVec2(resourceNameWidth, 0.0f);

		std::unordered_map<GraphicsResource*, int> resourceToIndex;
		for (const RGResource* pResource : m_Resources)
		{
			if (pResource->GetPhysical() == nullptr)
				continue;
			if (pResource->IsImported)
				continue;

			if (resourceToIndex.find(pResource->GetPhysical()) == resourceToIndex.end())
				resourceToIndex[pResource->GetPhysical()] = resourceIndex++;
			int physicalResourceIndex = resourceToIndex[pResource->GetPhysical()];

			const RGPass* pFirstPass = pResource->pFirstAccess;
			const RGPass* pLastPass = pResource->pLastAccess;
			if (pFirstPass == nullptr || pLastPass == nullptr)
				continue;

			uint32 firstPassOffset = pFirstPass->ID;
			uint32 lastPassOffset = pResource->IsExported ? (int)m_RenderPasses.size() - 1 : pLastPass->ID;

			ImRect itemRect(resourceAccessPos + ImVec2(firstPassOffset * boxSize.x + 1, physicalResourceIndex * boxSize.y + 1), resourceAccessPos + ImVec2((lastPassOffset + 1) * boxSize.x - 1, (physicalResourceIndex + 1) * boxSize.y - 1));
			ImGui::ItemAdd(itemRect, pResource->ID);
			bool isHovered = ImGui::IsItemHovered();

			if (isHovered)
			{
				ImGui::BeginTooltip();
				ImGui::Text("%s", pResource->GetName());

				if (pResource->Type == RGResourceType::Texture)
				{
					const TextureDesc& desc = static_cast<const RGTexture*>(pResource)->Desc;
					ImGui::Text("Res: %dx%dx%d", desc.Width, desc.Height, desc.DepthOrArraySize);
					ImGui::Text("Fmt: %s", RHI::GetFormatInfo(desc.Format).pName);
					ImGui::Text("Mips: %d", desc.Mips);
					ImGui::Text("Size: %s", Math::PrettyPrintDataSize(RHI::GetTextureByteSize(desc.Format, desc.Width, desc.Height, desc.DepthOrArraySize)).c_str());
				}
				else if (pResource->Type == RGResourceType::Buffer)
				{
					const BufferDesc& desc = static_cast<const RGBuffer*>(pResource)->Desc;
					ImGui::Text("Size: %s", Math::PrettyPrintDataSize(desc.Size).c_str());
					ImGui::Text("Fmt: %s", RHI::GetFormatInfo(desc.Format).pName);
					ImGui::Text("Stride: %d", desc.ElementSize);
					ImGui::Text("Elements: %d", desc.NumElements());
				}
				ImGui::EndTooltip();
			}

			pCmd->AddRectFilled(itemRect.Min, itemRect.Max, pResource->Type == RGResourceType::Texture ? ImColor(1.0f, 0.7f, 0.9f) : ImColor(0.7f, 0.8f, 1.0f));

			ImColor boxColor = ImColor(1.0f, 1.0f, 1.0f, 0.5f);

			bool isActivePass = false;
			if (pActivePass)
			{
				auto it = std::find_if(pActivePass->Accesses.begin(), pActivePass->Accesses.end(), [pResource](const RGPass::ResourceAccess& access)
					{
						return access.pResource == pResource;
					});

				if (it != pActivePass->Accesses.end())
				{
					isActivePass = true;
					const RGPass::ResourceAccess& access = *it;
					if (ResourceState::HasWriteResourceState(access.Access))
						boxColor = ImColor(1.0f, 0.5f, 0.1f, 0.8f);
					else
						boxColor = ImColor(0.0f, 0.9f, 0.3f, 0.8f);
				}
			}

			if(isActivePass || isHovered)
				pCmd->AddRectFilled(itemRect.Min, itemRect.Max, boxColor);
		}

		for (auto& resource : resourceToIndex)
			pCmd->AddText(ImVec2(cursor.x, cursor.y + resource.second * boxSize.y), ImColor(1.0f, 1.0f, 1.0f), resource.first->GetName());

		ImGui::EndChild();
	}
	ImGui::End();
}

void RGGraph::DumpDebugGraph(const char* pPath) const
{
	struct StringStream
	{
		StringStream& operator<<(const char* pText)
		{
			if(String.size() + strlen(pText) > String.capacity())
				String.reserve(String.capacity() * 2);

			String += pText;
			return *this;
		}

		StringStream& operator<<(const std::string& text)
		{
			return operator<<(text.c_str());
		}

		StringStream& operator<<(int v)
		{
			return operator<<(Sprintf("%d", v));
		}

		std::string String;
	};

	StringStream stream;

	constexpr const char* pMermaidTemplate = R"(
			<!DOCTYPE html>
				<html lang="en">
				<head>
					<meta charset="utf-8">
					<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.1.1/css/all.min.css"
						integrity="sha512-KfkfwYDsLkIlwQp6LFnl8zNdLGxu9YAA1QvwINks4PhcElQSvqcyVLLD9aMhXd13uQjoXtEKNosOWaZqXgel0g=="
						crossorigin="anonymous" referrerpolicy="no-referrer" />
				</head>
				<body>
					<script src="https://cdn.jsdelivr.net/npm/mermaid/dist/mermaid.min.js"></script>
					<script>
						mermaid.initialize({ startOnLoad: true, maxTextSize: 90000, flowchart: { useMaxWidth: false, htmlLabels: true }});
					</script>
					<div class="mermaid">
						%s
					</div>
				</body>
			</html>
		)";

	stream << "graph TD;\n";

	stream << "classDef neverCullPass fill:#ff5e00,stroke:#333,stroke-width:4px;\n";
	stream << "classDef referencedPass fill:#fa0,stroke:#333,stroke-width:4px;\n";
	stream << "classDef unreferenced stroke:#fee,stroke-width:1px;\n";
	stream << "classDef referencedResource fill:#bef,stroke:#333,stroke-width:2px;\n";
	stream << "classDef importedResource fill:#9bd,stroke:#333,stroke-width:2px;\n";

	const char* writeLinkStyle = "stroke:#f82,stroke-width:2px;";
	const char* readLinkStyle = "stroke:#9c9,stroke-width:2px;";
	int linkIndex = 0;

	std::unordered_map<RGResource*, uint32> resourceVersions;

#define PRINT_RESOURCE_REUSE 0
#if PRINT_RESOURCE_REUSE
	std::unordered_map<GraphicsResource*, std::string> resourceMap;
	for (RGResource* pResource : m_Resources)
	{
		std::string& s = resourceMap[pResource->GetPhysical()];
		s = Sprintf("|%-60s| ", pResource->GetPhysical()->GetName().c_str());
	}
	for (auto& resource : resourceMap)
	{
		GraphicsResource* pResource = resource.first;
		for (int passIndex = 0; passIndex < (int)m_RenderPasses.size(); ++passIndex)
		{
			RGPass* pPass = m_RenderPasses[passIndex];
			auto access = std::find_if(pPass->Accesses.begin(), pPass->Accesses.end(), [pResource](const RGPass::ResourceAccess& access) { return access.pResource->GetPhysical() == pResource; });
			resource.second += access != pPass->Accesses.end() ? "#" : " ";
		}
	}
	for (auto& resource : resourceMap)
		E_LOG(Info, "%s", resource.second.c_str());
#endif

	//Pass declaration
	int passIndex = 0;
	for (RGPass* pPass : m_RenderPasses)
	{
		stream << "Pass" << pPass->ID;
		stream << "[";
		stream << "\"" << pPass->GetName() << "\"<br/>";
		stream << "Flags: " << PassFlagToString(pPass->Flags) << "<br/>";
		stream << "Index: " << passIndex << "<br/>";
		stream << "Culled: " << (pPass->IsCulled ? "Yes" : "No") << "<br/>";
		stream << "]:::";

		if (EnumHasAnyFlags(pPass->Flags, RGPassFlag::NeverCull))
		{
			stream << "neverCullPass";
		}
		else
		{
			stream << (pPass->IsCulled ? "unreferenced" : "referencedPass");
		}

		stream << "\n";

		auto PrintResource = [&](RGResource* pResource, uint32 version) {
			stream << "Resource" << pResource->ID << "_" << version;
			stream << (pResource->IsImported ? "[(" : "([");
			stream << "\"" << pResource->GetName() << "\"<br/>";

			if (pResource->Type == RGResourceType::Texture)
			{
				const TextureDesc& desc = static_cast<RGTexture*>(pResource)->Desc;
				stream << "Res: " << desc.Width << "x" << desc.Height << "x" << desc.DepthOrArraySize << "<br/>";
				stream << "Fmt: " << RHI::GetFormatInfo(desc.Format).pName << "<br/>";
				stream << "Mips: " << desc.Mips << "<br/>";
				stream << "Size: " << Math::PrettyPrintDataSize(RHI::GetTextureByteSize(desc.Format, desc.Width, desc.Height, desc.DepthOrArraySize)) << "</br>";
			}
			else if (pResource->Type == RGResourceType::Buffer)
			{
				const BufferDesc& desc = static_cast<RGBuffer*>(pResource)->Desc;
				stream << "Stride: " << desc.ElementSize << "<br/>";
				stream << "Fmt: " << RHI::GetFormatInfo(desc.Format).pName << "<br/>";
				stream << "Size: " << Math::PrettyPrintDataSize(desc.Size) << "<br/>";
				stream << "Elements: " << desc.NumElements() << "<br/>";
			}

			stream << (pResource->IsImported ? ")]" : "])");
			if (pResource->IsImported)
			{
				stream << ":::importedResource";
			}
			else
			{
				stream << ":::referencedResource";
			}
			stream << "\n";
		};

		for (RGPass::ResourceAccess& access : pPass->Accesses)
		{
			RGResource* pResource = access.pResource;
			uint32 resourceVersion = 0;
			auto it = resourceVersions.find(pResource);
			if (it == resourceVersions.end())
			{
				resourceVersions[pResource] = resourceVersion;

				if(pResource->IsImported)
					PrintResource(pResource, resourceVersion);

			}
			resourceVersion = resourceVersions[pResource];

			if (resourceVersion > 0 || pResource->IsImported)
			{
				stream << "Resource" << pResource->ID << "_" << resourceVersion << " -- " << D3D::ResourceStateToString(access.Access) << " --> Pass" << pPass->ID << "\n";
				stream << "linkStyle " << linkIndex++ << " " << readLinkStyle << "\n";
			}

			if (ResourceState::HasWriteResourceState(access.Access))
			{
				++resourceVersions[pResource];
				resourceVersion++;
				PrintResource(pResource, resourceVersion);

				stream << "Pass" << pPass->ID << " -- " << D3D::ResourceStateToString(access.Access) << " --> " << "Resource" << pResource->ID << "_" << resourceVersion;
				stream << "\nlinkStyle " << linkIndex++ << " " << writeLinkStyle << "\n";
			}
		}

		++passIndex;
	}

	std::string output = Sprintf(pMermaidTemplate, stream.String.c_str());
	Paths::CreateDirectoryTree(pPath);
	FILE* pFile = nullptr;
	fopen_s(&pFile, pPath, "w");
	if (pFile)
	{
		fwrite(output.c_str(), sizeof(char), output.length(), pFile);
		fclose(pFile);
	}
}
