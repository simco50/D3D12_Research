#include "stdafx.h"
#include "RenderGraph.h"
#include <sstream>
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

void RGGraph::DrawDebug(bool& enabled) const
{
	if (!enabled)
		return;

	if(ImGui::Begin("Resource usage", &enabled, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImDrawList* pCmd = ImGui::GetWindowDrawList();
		int32 passIndex = 0;
		int32 resourceIndex = 0;

		auto GetResourceFirstUse = [this](const RGResource* pResource) -> const RGPass* {
			for (RGPass* pPass : m_RenderPasses)
			{
				for (const RGPass::ResourceAccess& access : pPass->Accesses)
				{
					if (access.pResource == pResource)
						return pPass;
				}
			}
			return nullptr;
		};

		ImVec2 cursor = ImGui::GetCursorScreenPos();
		float passNameHeight = 300.0f;
		float resourceNameWidth = 300.0f;
		ImVec2 boxSize = ImVec2(12.0f, ImGui::GetTextLineHeightWithSpacing());

		ImVec2 passNamePos = cursor + ImVec2(resourceNameWidth, passNameHeight);

		float width = (int)m_RenderPasses.size() * boxSize.x + resourceNameWidth;
		float height = 1200;

		ImGui::BeginChild("Table", ImVec2(width, height));

		const RGPass* pActivePass = nullptr;
		for (const RGPass* pPass : m_RenderPasses)
		{
			ImGui::AddTextVertical(pCmd, pPass->Name, passNamePos + ImVec2(passIndex * boxSize.x, 0.0f), ImColor(1.0f, 1.0f, 1.0f));
			ImGui::ItemAdd(ImRect(passNamePos + ImVec2(passIndex * boxSize.x, -passNameHeight), passNamePos + ImVec2((passIndex + 1) * boxSize.x, 0.0f)), passIndex);
			bool passActive = ImGui::IsItemHovered();
			if (passActive)
			{
				ImGui::BeginTooltip();
				ImGui::Text("%s", pPass->Name);
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
			if (pResource->GetRaw() == nullptr)
				continue;
			if (pResource->IsImported)
				continue;

			if (resourceToIndex.find(pResource->GetRaw()) == resourceToIndex.end())
				resourceToIndex[pResource->GetRaw()] = resourceIndex++;
			int physicalResourceIndex = resourceToIndex[pResource->GetRaw()];

			const RGPass* pFirstPass = GetResourceFirstUse(pResource);
			const RGPass* pLastPass = pResource->pLastAccess;
			if (pFirstPass == nullptr || pLastPass == nullptr)
				continue;

			uint32 firstPassOffset = pFirstPass->ID;
			uint32 lastPassOffset = pResource->IsExported ? (int)m_RenderPasses.size() - 1 : pLastPass->ID;

			ImRect itemRect(resourceAccessPos + ImVec2(firstPassOffset * boxSize.x + 1, physicalResourceIndex * boxSize.y + 1), resourceAccessPos + ImVec2((lastPassOffset + 1) * boxSize.x - 1, (physicalResourceIndex + 1) * boxSize.y - 1));
			ImGui::ItemAdd(itemRect, pResource->ID);
			bool resourceActive = ImGui::IsItemHovered();

			if (resourceActive)
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

			ImColor boxColor = resourceActive ? ImColor(1.0f, 1.0f, 1.0f) : ImColor(1.0f, 1.0f, 1.0f, 0.8f);

			if (pActivePass)
			{
				auto it = std::find_if(pActivePass->Accesses.begin(), pActivePass->Accesses.end(), [pResource](const RGPass::ResourceAccess& access)
					{
						return access.pResource == pResource;
					});

				if (it != pActivePass->Accesses.end())
				{
					const RGPass::ResourceAccess& access = *it;
					if (ResourceState::HasWriteResourceState(access.Access))
						boxColor = ImColor(1.0f, 0.5f, 0.1f);
					else
						boxColor = ImColor(0.0f, 0.9f, 0.3f);
				}
			}

			pCmd->AddRectFilled(itemRect.Min, itemRect.Max, boxColor);
		}

		for (auto& resource : resourceToIndex)
			pCmd->AddText(ImVec2(cursor.x, cursor.y + resource.second * boxSize.y), ImColor(1.0f, 1.0f, 1.0f), resource.first->GetName().c_str());

		ImGui::EndChild();
	}
	ImGui::End();
}

void RGGraph::DumpGraph(const char* pPath) const
{
	std::stringstream stream;

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
						mermaid.initialize({ startOnLoad: true, flowchart: { useMaxWidth: false, htmlLabels: true }});
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
		std::string& s = resourceMap[pResource->GetRaw()];
		s = Sprintf("|%-60s| ", pResource->GetRaw()->GetName().c_str());
	}
	for (auto& resource : resourceMap)
	{
		GraphicsResource* pResource = resource.first;
		for (int passIndex = 0; passIndex < (int)m_RenderPasses.size(); ++passIndex)
		{
			RGPass* pPass = m_RenderPasses[passIndex];
			auto access = std::find_if(pPass->Accesses.begin(), pPass->Accesses.end(), [pResource](const RGPass::ResourceAccess& access) { return access.pResource->GetRaw() == pResource; });
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
		stream << "\"" << pPass->Name << "\"<br/>";
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
			stream << "\"" << pResource->Name << "\"<br/>";

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

	std::string output = Sprintf(pMermaidTemplate, stream.str().c_str());
	Paths::CreateDirectoryTree(pPath);
	FILE* pFile = nullptr;
	fopen_s(&pFile, pPath, "w");
	if (pFile)
	{
		fwrite(output.c_str(), sizeof(char), output.length(), pFile);
		fclose(pFile);
	}
}
