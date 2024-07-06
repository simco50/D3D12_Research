#include "stdafx.h"
#include "RenderGraph.h"
#include "Core/Stream.h"
#include "Graphics/ImGuiRenderer.h"
#include "Core/Profiler.h"

#include <imgui_internal.h>
#include <IconsFontAwesome4.h>

template<typename T>
String BitmaskToString(T mask, const char* (*pValueToString)(T))
{
	String outString;
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

static String PassFlagToString(RGPassFlag flags)
{
	return BitmaskToString<RGPassFlag>(flags,
		[](RGPassFlag flag) -> const char*
		{
			switch (flag)
			{
			case RGPassFlag::None:		return "None";
			case RGPassFlag::Compute:	return "Compute";
			case RGPassFlag::Raster:	return "Raster";
			case RGPassFlag::Copy:		return "Copy";
			case RGPassFlag::NeverCull: return "Never Cull";
			default: return nullptr;
			}
		});
}

// Generate a color from a string. Used to color bars
static ImU32 ColorFromString(const char* pName)
{
	uint32 hash = StringHash(pName);
	float hashF = (float)hash / UINT32_MAX;
	return Math::Pack_RGBA8_UNORM(Math::HSVtoRGB(hashF, 0.5f, 0.6f));
}


void RGGraph::DrawResourceTracker(bool& enabled) const
{
	check(m_IsCompiled);

	if (!enabled)
		return;

	PROFILE_CPU_SCOPE();

	if(ImGui::Begin("Resource usage", &enabled))
	{
		Array<RGResource*> sortedResources = m_Resources;
		std::sort(sortedResources.begin(), sortedResources.end(), [](const RGResource* pA, const RGResource* pB) {
			return pA->FirstAccess.GetIndex() < pB->FirstAccess.GetIndex();
			});

		Array<const DeviceResource*> physicalResources;
		HashMap<const DeviceResource*, Array<const RGResource*>> physicalResourceMap;
		for (const RGResource* pResource : sortedResources)
		{
			if (pResource->GetPhysicalUnsafe() == nullptr || pResource->IsImported)
				continue;

			if (std::find(physicalResources.begin(), physicalResources.end(), pResource->GetPhysicalUnsafe()) == physicalResources.end())
				physicalResources.push_back(pResource->GetPhysicalUnsafe());

			physicalResourceMap[pResource->GetPhysicalUnsafe()].push_back(pResource);
		}

		struct Event
		{
			RGEventID ID;
			RGPassID Begin;
			RGPassID End;
			uint16 Depth;
		};
		Array<Event> events;
		Array<uint16> eventStack;
		int eventDepth = 0;

		for (RGPass* pPass : m_Passes)
		{
			if (pPass->IsCulled)
				continue;

			for (RGEventID eventID : pPass->EventsToStart)
			{
				events.push_back(Event{
						.ID = eventID,
						.Begin = pPass->ID,
						.Depth = (uint16)eventStack.size(),
					});
				eventStack.push_back((uint16)(events.size() - 1));
				eventDepth = ImMax(eventDepth, (int)eventStack.size());
			}

			for (uint32 i = 0; i < pPass->NumEventsToEnd; ++i)
			{
				events[eventStack.back()].End = pPass->ID;
				eventStack.pop_back();
			}
		}

		static char resourceFilter[128] = "";

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(1, 1));

		if (ImGui::BeginTable("Resource Tracker", (int)m_Passes.size() + 1, ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders, ImGui::GetContentRegionAvail()))
		{
			float column_width = 20.0f;
			ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 250.0f);
			for (const RGPass* pPass : m_Passes)
			{
				ImGui::TableSetupColumn(pPass->GetName(), ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoClip, column_width);
			}
			ImGui::TableSetupScrollFreeze(1, 3);

			ImGui::TableAngledHeadersRowEx(40.0f * Math::DegreesToRadians, 180);

			// Event bars
			const float row_height = ImGui::GetTextLineHeight();
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers, row_height * eventDepth);
			float clipRectX = ImGui::GetCursorScreenPos().x + ImGui::GetColumnWidth(0);
			ImGui::TableSetColumnIndex(1);

			// Find actual column width
			ImVec2 c = ImGui::GetCursorScreenPos();
			ImGui::TableSetColumnIndex(2);
			float colWidth = ImGui::GetCursorScreenPos().x - c.x;

			ImDrawList* pDrawList = ImGui::GetWindowDrawList();
			ImGui::PushClipRect(ImVec2(clipRectX, GImGui->CurrentTable->BgClipRect.Min.y), GImGui->CurrentTable->BgClipRect.Max, false); // Span all columns

			for (const Event& e : events)
			{
				RGEvent ev = m_Events[e.ID.GetIndex()];
				ImRect rect(e.Begin.GetIndex() * colWidth, e.Depth * row_height, (e.End.GetIndex() + 1) * colWidth, (e.Depth + 1) * row_height);
				rect.Expand(-1.0f);
				rect.Translate(c);
				if (ImGui::ItemAdd(rect, ImGui::GetID(&ev)))
				{
					pDrawList->AddRectFilled(rect.Min, rect.Max, ColorFromString(ev.pName));
					ImGui::RenderTextEllipsis(pDrawList, rect.Min, rect.Max, rect.Max.x, rect.Max.x, ev.pName, nullptr, nullptr);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("%s", ev.pName);
					ImGui::Text("FilePath: %s", Paths::GetFileName(ev.pFilePath).c_str());
					ImGui::Text("Line: %d", ev.LineNumber);
					ImGui::EndTooltip();
				}
			}
			ImGui::PopClipRect();

			// Passes row
			const RGPass* pActivePass = nullptr;

			// Open row
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers, ImGui::TableGetHeaderRowHeight());

			const int columns_count = ImGui::TableGetColumnCount();
			for (int column_n = 0; column_n < columns_count; column_n++)
			{
				if (!ImGui::TableSetColumnIndex(column_n))
					continue;

				ImGui::PushID(column_n);

				if (column_n == 0)
				{
					ImGui::TableHeader("##name");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
					ImGui::InputTextWithHint("##search", "Filter...", resourceFilter, ARRAYSIZE(resourceFilter));
				}
				else
				{
					const RGPass* pPass = m_Passes[column_n - 1];
					ImColor clr = pPass->IsCulled ? ImColor(1.0f, 1.0f, 1.0f, 0.4f) : ImColor(1.0f, 1.0f, 1.0f, 0.8f);
					c = ImGui::GetCursorScreenPos();
					ImRect rect(0, 0, colWidth, row_height);
					float dilation = 2;
					rect.Expand(dilation);
					rect.Translate(c);
					if (ImGui::ItemAdd(rect, ImGui::GetID("##pass")))
					{
						rect.Expand(-dilation - 1);
						pDrawList->AddRectFilled(rect.Min, rect.Max, clr);
					}

					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("%s", pPass->GetName());
						ImGui::Text("Flags: %s", PassFlagToString(pPass->Flags).c_str());
						ImGui::Text("Index: %d", pPass->ID.GetIndex());
						ImGui::EndTooltip();

						pActivePass = pPass;
					}
				}

				ImGui::PopID();
			}

			int rowIndex = 0;
			for (const DeviceResource* pPhysical : physicalResources)
			{
				bool filterMatch = false;
				const Array<const RGResource*>& resources = physicalResourceMap[pPhysical];
				for (const RGResource* pResource : resources)
				{
					if (strstr(pResource->GetName(), resourceFilter))
					{
						filterMatch = true;
						break;
					}
				}

				if (!filterMatch)
					continue;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				bool isOpen = ImGui::TreeNodeEx(Sprintf("%d", rowIndex).c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, pPhysical->GetName());
				rowIndex++;

				auto DrawResourceTooltip = [&](const RGResource* pResource, const RGPass::ResourceAccess* pAccess)
					{
						ImGui::BeginTooltip();
						ImGui::TextColored(ImVec4(0.922f, 0.71f, 0.082f, 1.0f), "%s", pResource->GetName());
						ImGui::Separator();

						if (pAccess)
						{
							ImColor rectColor = D3D::HasWriteResourceState(pAccess->Access) ? ImColor(1.0f, 0.5f, 0.1f, 1.0f) : ImColor(0.0f, 0.9f, 0.3f, 1.0f);
							ImGui::TextColored(rectColor, "%s", D3D::ResourceStateToString(pAccess->Access).c_str());
						}

						float width = 90;
						auto AddRow = [width](const char* pCol1, const char* pCol2, ...)
							{
								ImGui::Text(pCol1);
								ImGui::SameLine(width);
								va_list args;
								va_start(args, pCol2);
								ImGui::TextV(pCol2, args);
								va_end(args);
							};

						if (pResource->GetType() == RGResourceType::Texture)
						{
							const TextureDesc& desc = static_cast<const RGTexture*>(pResource)->Desc;
							AddRow("Resolution:", "%dx%dx%d", desc.Width, desc.Height, desc.DepthOrArraySize);
							AddRow("Format:", "%s", RHI::GetFormatInfo(desc.Format).pName);
							AddRow("Mips:", "%d", desc.Mips);
							AddRow("Size:", "%s", Math::PrettyPrintDataSize(RHI::GetTextureByteSize(desc.Format, desc.Width, desc.Height, desc.DepthOrArraySize)).c_str());
						}
						else if (pResource->GetType() == RGResourceType::Buffer)
						{
							const BufferDesc& desc = static_cast<const RGBuffer*>(pResource)->Desc;
							AddRow("Size:", "%s", Math::PrettyPrintDataSize(desc.Size).c_str());
							if (desc.Format != ResourceFormat::Unknown)
								AddRow("Format:", "%s", RHI::GetFormatInfo(desc.Format).pName);
							else
								AddRow("Stride:", "%d", desc.ElementSize);
							AddRow("Elements:", "%s", Utils::AddThousandsSeperator(desc.NumElements()).c_str());
						}

						ImGui::Text("Export:");
						ImGui::SameLine();
						ImGui::TextColored(pResource->IsExported ? ImColor(0.0f, 1.0f, 0.0f, 1.0f) : ImColor(1.0f, 0.0f, 0.0f, 1.0f), pResource->IsExported ? ICON_FA_CHECK : ICON_FA_TIMES);
						ImGui::SameLine();
						ImGui::Text("Import:");
						ImGui::SameLine();
						ImGui::TextColored(pResource->IsImported ? ImColor(0.0f, 1.0f, 0.0f, 1.0f) : ImColor(1.0f, 0.0f, 0.0f, 1.0f), pResource->IsImported ? ICON_FA_CHECK : ICON_FA_TIMES);

						ImGui::EndTooltip();
					};

				auto DrawResourceRow = [&](const RGResource* pResource, bool isInner)
					{
						RGPassID firstPass = pResource->FirstAccess;
						RGPassID lastPass = pResource->LastAccess;
						uint32 firstPassOffset = pResource->IsImported ? 0 : firstPass.GetIndex();
						uint32 lastPassOffset = pResource->IsExported ? (int)m_Passes.size() - 1 : lastPass.GetIndex();

						ImDrawList* pDraw = ImGui::GetWindowDrawList();
						ImGui::TableSetColumnIndex(1);
						ImGui::PushClipRect(ImVec2(clipRectX, GImGui->CurrentTable->BgClipRect.Min.y), GImGui->CurrentTable->BgClipRect.Max, false); // Span all columns
						c = ImGui::GetCursorScreenPos();
						ImRect itemRect(firstPassOffset * colWidth, 0, (lastPassOffset + 1) * colWidth, row_height);
						itemRect.Expand(-1);
						itemRect.Translate(c);
						if (ImGui::ItemAdd(itemRect, ImGui::GetID(pResource)))
							pDraw->AddRectFilled(itemRect.Min, itemRect.Max, ImColor(0.3f, 0.3f, 0.3f, 1.0f));

						bool mainBarHovered = ImGui::IsItemHovered();

						// If there is a hovered pass, limit the highlighed read/writes to the current pass
						if (pActivePass)
						{
							firstPassOffset = pActivePass->ID.GetIndex();
							lastPassOffset = pActivePass->ID.GetIndex();
						}

						const RGPass::ResourceAccess* pResourceAccess = nullptr;
						for (uint32 i = firstPassOffset; i <= lastPassOffset; ++i)
						{
							const RGPass* pPass = m_Passes[i];
							auto it = std::find_if(pPass->Accesses.begin(), pPass->Accesses.end(), [pResource](const RGPass::ResourceAccess& access) { return access.pResource == pResource; });
							if (it != pPass->Accesses.end())
							{
								ImColor rectColor = D3D::HasWriteResourceState(it->Access) ? ImColor(1.0f, 0.5f, 0.1f, 0.6f) : ImColor(0.0f, 0.9f, 0.3f, 0.6f);
								ImRect squareRect(i * colWidth, 0, (i + 1) * colWidth, row_height);
								squareRect.Expand(-1);
								squareRect.Translate(c);
								if (ImGui::ItemAdd(squareRect, ImGui::GetID(&*it)))
									pDraw->AddRectFilled(squareRect.Min, squareRect.Max, rectColor);

								if (ImGui::IsItemHovered())
									pResourceAccess = &*it;
							}
						}

						if (mainBarHovered)
						{
							pDraw->AddRectFilled(itemRect.Min, itemRect.Max, ImColor(1.0f, 1.0f, 1.0f, 0.2f));
							DrawResourceTooltip(pResource, pResourceAccess);
						}


						ImGui::PopClipRect();
					};

				for (const RGResource* pResource : resources)
				{
					RGPassID firstPass = pResource->FirstAccess;
					RGPassID lastPass = pResource->LastAccess;
					if (!firstPass.IsValid() || !lastPass.IsValid())
						continue;

					DrawResourceRow(pResource, false);
				}

				if (isOpen)
				{
					for (const RGResource* pResource : resources)
					{
						ImGui::TableNextRow();
						ImGui::TableNextColumn();

						ImGui::TreeNodeEx(pResource->GetName(), ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);

						if (ImGui::IsItemHovered())
							DrawResourceTooltip(pResource, nullptr);

						DrawResourceRow(pResource, true);
						ImGui::TableNextColumn();
					}
					ImGui::TreePop();
				}
			}
		}

		ImGui::EndTable();
		ImGui::PopStyleVar();
	}
	ImGui::End();
}


void RGGraph::DrawPassView(bool& enabled) const
{
	if (!enabled)
		return;

	struct TreeNode
	{
		const char* pName;
		RGPassID			Pass;
		Array<int>	Children;

		void DrawNode(Span<const TreeNode> nodes, const RGGraph& graph, int depth = 0) const
		{
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns;

			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			if (Pass.IsValid())
			{
				ImGui::PushID(Pass.GetIndex());
				const RGPass* pPass = graph.m_Passes[Pass.GetIndex()];
				bool open = ImGui::TreeNodeEx(pPass->GetName(), flags);

				ImGui::TableNextColumn();
				ImGui::TextDisabled(PassFlagToString(pPass->Flags).c_str());
				ImGui::TableNextColumn();

				if (open)
				{
					for (const RGPass::ResourceAccess& access : pPass->Accesses)
					{
						ImGui::TableNextRow();
						ImGui::TableNextColumn();

						ImGui::TreeNodeEx(access.pResource->GetName(), flags | ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
						ImGui::TableNextColumn();
						ImGui::Text(D3D::ResourceStateToString(access.Access).c_str());
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			else
			{
				if (depth == 0)
					flags |= ImGuiTreeNodeFlags_DefaultOpen;

				bool open = ImGui::TreeNodeEx(pName, flags, ICON_FA_FOLDER " %s", pName);
				ImGui::TableNextColumn();
				ImGui::TextDisabled("--");
				ImGui::TableNextColumn();

				if (open)
				{
					for (int i : Children)
					{
						nodes[i].DrawNode(nodes, graph, depth + 1);
					}
					ImGui::TreePop();
				}
			}
		}
	};

	Array<TreeNode> nodes(1);
	Array<int> nodeStack;
	nodeStack.push_back(0);

	for (RGPass* pPass : m_Passes)
	{
		if (pPass->IsCulled)
			continue;

		for (RGEventID eventID : pPass->EventsToStart)
		{
			uint32 newIndex = (uint32)nodes.size();
			nodes[nodeStack.back()].Children.push_back(newIndex);
			TreeNode& newNode = nodes.emplace_back();
			newNode.pName = m_Events[eventID.GetIndex()].pName;
			nodeStack.push_back(newIndex);
		}

		uint32 newIndex = (uint32)nodes.size();
		TreeNode& newNode = nodes.emplace_back();
		nodes[nodeStack.back()].Children.push_back(newIndex);
		newNode.Pass = pPass->ID;

		for (uint32 i = 0; i < pPass->NumEventsToEnd; ++i)
		{
			nodeStack.pop_back();
		}
	}

	check(nodeStack.size() == 1);

	Span<int> rootNodes = nodes[0].Children;

	if (ImGui::Begin("Passes", &enabled))
	{
		if (ImGui::BeginTable("Passes", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("Resources");
			ImGui::TableHeadersRow();

			for (int node : rootNodes)
			{
				nodes[node].DrawNode(nodes, *this);
			}

			ImGui::EndTable();
		}
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
			if (String.size() + strlen(pText) > String.capacity())
				String.reserve(String.capacity() * 2);

			String += pText;
			return *this;
		}

		StringStream& operator<<(const String& text)
		{
			return operator<<(text.c_str());
		}

		StringStream& operator<<(int v)
		{
			return operator<<(Sprintf("%d", v));
		}

		String String;
	};

	uint32 neverCullColor = 0xFF5E00FF;
	uint32 referencedPassColor = 0xFFAA00FF;
	uint32 unreferedPassColor = 0xFFEEEEFF;
	uint32 referencedResourceColor = 0xBBEEFFFF;
	uint32 importedResourceColor = 0x99BBDDFF;

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

		HashMap<RGResource*, uint32> resourceVersions;

		//Pass declaration
		int passIndex = 0;
		for (RGPass* pPass : m_Passes)
		{
			stream << "Pass" << pPass->ID.GetIndex();
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
				stream << "Resource" << pResource->ID.GetIndex() << "_" << version;
				stream << (pResource->IsImported ? "[(" : "([");
				stream << "\"" << pResource->GetName() << "\"<br/>";

				if (pResource->GetType() == RGResourceType::Texture)
				{
					const TextureDesc& desc = static_cast<RGTexture*>(pResource)->Desc;
					stream << "Res: " << desc.Width << "x" << desc.Height << "x" << desc.DepthOrArraySize << "<br/>";
					stream << "Fmt: " << RHI::GetFormatInfo(desc.Format).pName << "<br/>";
					stream << "Mips: " << desc.Mips << "<br/>";
					stream << "Size: " << Math::PrettyPrintDataSize(RHI::GetTextureByteSize(desc.Format, desc.Width, desc.Height, desc.DepthOrArraySize)) << "</br>";
				}
				else if (pResource->GetType() == RGResourceType::Buffer)
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
					stream << "Resource" << pResource->ID.GetIndex() << "_" << resourceVersion << " -- " << D3D::ResourceStateToString(access.Access) << " --> Pass" << pPass->ID.GetIndex() << "\n";
					stream << "linkStyle " << linkIndex++ << " " << readLinkStyle << "\n";
				}

				if (D3D::HasWriteResourceState(access.Access))
				{
					++resourceVersions[pResource];
					resourceVersion++;
					PrintResource(pResource, resourceVersion);

					stream << "Pass" << pPass->ID.GetIndex() << " -- " << D3D::ResourceStateToString(access.Access) << " --> " << "Resource" << pResource->ID.GetIndex() << "_" << resourceVersion;
					stream << "\nlinkStyle " << linkIndex++ << " " << writeLinkStyle << "\n";
				}
			}

			++passIndex;
		}

		String output = Sprintf(pMermaidTemplate, stream.String.c_str());

		String fullPath = Paths::MakeAbsolute(Sprintf("%s.html", pPath).c_str());
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

		HashMap<RGResource*, uint32> resourceVersions;

		StringStream stream;

		auto PrintResource = [&](RGResource* pResource, uint32 version) {
			stream << "Resource" << pResource->ID.GetIndex() << "_" << version;
			stream << "[ label = \"" << pResource->GetName() << "\\n";

			if (pResource->GetType() == RGResourceType::Texture)
			{
				const TextureDesc& desc = static_cast<RGTexture*>(pResource)->Desc;
				stream << "Res: " << desc.Width << "x" << desc.Height << "x" << desc.DepthOrArraySize << "\\n";
				stream << "Fmt: " << RHI::GetFormatInfo(desc.Format).pName << "\\n";
				stream << "Mips: " << desc.Mips << "\\n";
				stream << "Size: " << Math::PrettyPrintDataSize(RHI::GetTextureByteSize(desc.Format, desc.Width, desc.Height, desc.DepthOrArraySize));
			}
			else if (pResource->GetType() == RGResourceType::Buffer)
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
		for (RGPass* pPass : m_Passes)
		{
			uint32 passColor = referencedPassColor;
			if (EnumHasAnyFlags(pPass->Flags, RGPassFlag::NeverCull))
				passColor = neverCullColor;
			else if (pPass->IsCulled)
				passColor = unreferedPassColor;

			stream << "Pass" << pPass->ID.GetIndex() << " ";
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
					stream << "Resource" << pResource->ID.GetIndex() << "_" << resourceVersion << " -> " << "Pass" << pPass->ID.GetIndex() << "\n";
				}

				if (D3D::HasWriteResourceState(access.Access))
				{
					++resourceVersions[pResource];
					resourceVersion++;
					PrintResource(pResource, resourceVersion);

					stream << "Pass" << pPass->ID.GetIndex() << " -> " << "Resource" << pResource->ID.GetIndex() << "_" << resourceVersion << "\n";
				}
			}

			++passIndex;
		}

		stream << "}\n";

		String fullPath = Paths::MakeAbsolute(Sprintf("%s_GraphViz.html", pPath).c_str());
		Paths::CreateDirectoryTree(fullPath);

		String output = Sprintf(pGraphVizTemplate, stream.String);
		FileStream file;
		if (file.Open(fullPath.c_str(), FileMode::Write))
			file.Write(output.c_str(), (uint32)output.length());

		ShellExecuteA(nullptr, "open", fullPath.c_str(), nullptr, nullptr, SW_SHOW);
	}
}
