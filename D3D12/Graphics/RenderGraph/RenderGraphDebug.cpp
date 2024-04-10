#include "stdafx.h"
#include "RenderGraph.h"
#include "Core/Stream.h"
#include "Graphics/ImGuiRenderer.h"

#include <External/Imgui/imgui_internal.h>

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
			default: return nullptr;
			}
		});
}

void RGGraph::DrawResourceTracker(bool& enabled) const
{
	check(m_IsCompiled);

	if (!enabled)
		return;

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
			ImGui::AddText(pCmd, pPass->GetName(), itemRect.Max - ImVec2(0.0f, 12.0f), ImColor(1.0f, 1.0f, 1.0f), -Math::PI_DIV_4);

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

		std::unordered_map<DeviceResource*, int> resourceToIndex;
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
					if (D3D::HasWriteResourceState(access.Access))
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
	check(m_IsCompiled);

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

	uint32 neverCullColor			= 0xFF5E00FF;
	uint32 referencedPassColor		= 0xFFAA00FF;
	uint32 unreferedPassColor		= 0xFFEEEEFF;
	uint32 referencedResourceColor	= 0xBBEEFFFF;
	uint32 importedResourceColor	= 0x99BBDDFF;

	// Mermaid
	{
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

		stream << Sprintf("classDef neverCullPass fill:#%x,stroke:#333,stroke-width:4px;\n", neverCullColor);
		stream << Sprintf("classDef referencedPass fill:#%x,stroke:#333,stroke-width:4px;\n", referencedPassColor);
		stream << Sprintf("classDef unreferenced stroke:#fee,stroke-width:1px;\n");
		stream << Sprintf("classDef referencedResource fill:#%x,stroke:#333,stroke-width:2px;\n", referencedResourceColor);
		stream << Sprintf("classDef importedResource fill:#%x,stroke:#333,stroke-width:2px;\n", importedResourceColor);

		const char* writeLinkStyle = "stroke:#f82,stroke-width:2px;";
		const char* readLinkStyle = "stroke:#9c9,stroke-width:2px;";
		int linkIndex = 0;

		std::unordered_map<RGResource*, uint32> resourceVersions;

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

					if (pResource->IsImported)
						PrintResource(pResource, resourceVersion);

				}
				resourceVersion = resourceVersions[pResource];

				if (resourceVersion > 0 || pResource->IsImported)
				{
					stream << "Resource" << pResource->ID << "_" << resourceVersion << " -- " << D3D::ResourceStateToString(access.Access) << " --> Pass" << pPass->ID << "\n";
					stream << "linkStyle " << linkIndex++ << " " << readLinkStyle << "\n";
				}

				if (D3D::HasWriteResourceState(access.Access))
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

		std::string fullPath = Paths::MakeAbsolute(Sprintf("%s.html", pPath).c_str());
		Paths::CreateDirectoryTree(fullPath);

		FileStream file;
		if (file.Open(fullPath.c_str(), FileMode::Write))
			file.Write(output.c_str(), (uint32)output.length());
	}

	// GraphViz
	{
		const char* pGraphVizTemplate = R"(<div id="graph"></div>
			<script src="https://cdn.jsdelivr.net/npm/@viz-js/viz@3.4.0/lib/viz-standalone.js"></script>
			<script>
			  Viz.instance().then(function(viz) {
			    var svg = viz.renderSVGElement(`%s`);

			    document.getElementById("graph").appendChild(svg);
			  });
			</script>)";

		std::unordered_map<RGResource*, uint32> resourceVersions;

		StringStream stream;

		auto PrintResource = [&](RGResource* pResource, uint32 version) {
			stream << "Resource" << pResource->ID << "_" << version;
			stream << "[ label = \"" << pResource->GetName() << "\\n";

			if (pResource->Type == RGResourceType::Texture)
			{
				const TextureDesc& desc = static_cast<RGTexture*>(pResource)->Desc;
				stream << "Res: " << desc.Width << "x" << desc.Height << "x" << desc.DepthOrArraySize << "\\n";
				stream << "Fmt: " << RHI::GetFormatInfo(desc.Format).pName << "\\n";
				stream << "Mips: " << desc.Mips << "\\n";
				stream << "Size: " << Math::PrettyPrintDataSize(RHI::GetTextureByteSize(desc.Format, desc.Width, desc.Height, desc.DepthOrArraySize));
			}
			else if (pResource->Type == RGResourceType::Buffer)
			{
				const BufferDesc& desc = static_cast<RGBuffer*>(pResource)->Desc;
				stream << "Stride: " << desc.ElementSize << "\\n";
				stream << "Fmt: " << RHI::GetFormatInfo(desc.Format).pName << "\\n";
				stream << "Size: " << Math::PrettyPrintDataSize(desc.Size) << "\\n";
				stream << "Elements: " << desc.NumElements();
			}

			uint32 color = pResource->IsImported ? importedResourceColor : referencedResourceColor;
			const char* pShape = pResource->IsImported ? "cylinder" : "oval";
			stream << Sprintf("\" penwidth=2 shape=%s style=filled fillcolor=\"#%x\" ];\n", pShape, color);
			};

		stream << "digraph {\n";

		stream << "splines=ortho;\n";

		int passIndex = 0;
		for (RGPass* pPass : m_RenderPasses)
		{
			uint32 passColor = referencedPassColor;
			if (EnumHasAnyFlags(pPass->Flags, RGPassFlag::NeverCull))
				passColor = neverCullColor;
			else if (pPass->IsCulled)
				passColor = unreferedPassColor;

			stream << "Pass" << pPass->ID << " ";
			stream << "[ label = ";
			stream << "\"" << pPass->GetName() << "\\n";
			stream << "Flags: " << PassFlagToString(pPass->Flags) << "\\n";
			stream << "Index: " << passIndex << "\\n";
			stream << "Culled: " << (pPass->IsCulled ? "Yes" : "No");
			stream << Sprintf("\" penwidth=4 shape=rectangle style=filled fillcolor=\"#%x\"];", passColor);

			stream << "\n";

			for (RGPass::ResourceAccess& access : pPass->Accesses)
			{
				RGResource* pResource = access.pResource;
				uint32 resourceVersion = 0;
				auto it = resourceVersions.find(pResource);
				if (it == resourceVersions.end())
				{
					resourceVersions[pResource] = resourceVersion;

					if (pResource->IsImported)
						PrintResource(pResource, resourceVersion);
				}
				resourceVersion = resourceVersions[pResource];

				if (resourceVersion > 0 || pResource->IsImported)
				{
					stream << "Resource" << pResource->ID << "_" << resourceVersion << " -> " << "Pass" << pPass->ID << "\n";
				}

				if(D3D::HasWriteResourceState(access.Access))
				{
					++resourceVersions[pResource];
					resourceVersion++;
					PrintResource(pResource, resourceVersion);

					stream << "Pass" << pPass->ID << " -> " << "Resource" << pResource->ID << "_" << resourceVersion << "\n";
				}
			}

			++passIndex;
		}

		stream << "}\n";

		std::string fullPath = Paths::MakeAbsolute(Sprintf("%s_GraphViz.html", pPath).c_str());
		Paths::CreateDirectoryTree(fullPath);

		std::string output = Sprintf(pGraphVizTemplate, stream.String);
		FileStream file;
		if (file.Open(fullPath.c_str(), FileMode::Write))
			file.Write(output.c_str(), (uint32)output.length());
	}
}
