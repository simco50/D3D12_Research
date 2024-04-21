#include "stdafx.h"
#include "VisualizeTexture.h"
#include "Graphics/RHI/Device.h"
#include "Graphics/RHI/RHI.h"
#include "Graphics/RHI/PipelineState.h"
#include "Graphics/RHI/RootSignature.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/ImGuiRenderer.h"
#include "Graphics/SceneView.h"
#include "Content/Image.h"

#include <External/Imgui/imgui_internal.h>
#include <External/FontAwesome/IconsFontAwesome4.h>

struct PickingData
{
	Vector4 DataFloat;
	Vector4u DataUInt;
};

CaptureTextureSystem::CaptureTextureSystem(GraphicsDevice* pDevice)
{
	m_pVisualizeRS = new RootSignature(pDevice);
	m_pVisualizeRS->AddRootCBV(0);
	m_pVisualizeRS->AddDescriptorTable(0, 1, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
	m_pVisualizeRS->Finalize("Common");
	m_pVisualizePSO = pDevice->CreateComputePipeline(m_pVisualizeRS, "ImageVisualize.hlsl", "CSMain");
}


void CaptureTextureSystem::Capture(RGGraph& graph, CaptureTextureContext& captureContext, RGTexture* pSource)
{
	if (!pSource)
		return;

	captureContext.SourceName = pSource->GetName();
	captureContext.SourceDesc = pSource->GetDesc();

	RGBuffer* pReadbackTarget = RGUtils::CreatePersistent(graph, "TextureCapture.ReadbackTarget", BufferDesc::CreateReadback(sizeof(CaptureTextureContext::PickData) * 3), &captureContext.pReadbackBuffer, true);

	const TextureDesc& desc = pSource->GetDesc();
	Vector2u mipSize(desc.Width >> captureContext.MipLevel, desc.Height >> captureContext.MipLevel);
	RGTexture* pTarget = RGUtils::CreatePersistent(graph, "TextureCapture.Target", TextureDesc::Create2D(mipSize.x, mipSize.y, ResourceFormat::RGBA8_UNORM, 1, TextureFlag::ShaderResource), &captureContext.pTextureTarget, true);
	RGBuffer* pPickingBuffer = graph.Create("TextureCapture.Picking", BufferDesc::CreateStructured(1, sizeof(CaptureTextureContext::PickData)));

	graph.AddPass("CaptureTexture.Process", RGPassFlag::Compute)
		.Read({ pSource })
		.Write({ pTarget, pPickingBuffer })
		.Bind([=](CommandContext& context)
			{
				context.SetComputeRootSignature(m_pVisualizeRS);
				context.SetPipelineState(m_pVisualizePSO);

				struct ConstantsData
				{
					Vector2u HoveredPixel;
					Vector2u Dimensions;
					Vector2 ValueRange;
					uint32 TextureSource;
					uint32 TextureTarget;
					uint32 TextureType;
					uint32 ChannelMask;
					uint32 MipLevel;
					uint32 Slice;
					uint32 IsIntegerFormat;
				} constants;

				const FormatInfo& formatInfo = RHI::GetFormatInfo(desc.Format);

				constants.HoveredPixel = captureContext.HoveredPixel;
				constants.TextureSource = pSource->Get()->GetSRV()->GetHeapIndex();
				constants.TextureTarget = pTarget->Get()->GetUAV()->GetHeapIndex();
				constants.Dimensions = mipSize;
				constants.TextureType = (uint32)pSource->GetDesc().Type;
				constants.ValueRange = Vector2(captureContext.RangeMin, captureContext.RangeMax);
				constants.ChannelMask =
					(captureContext.VisibleChannels[0] ? 1 : 0) << 0 |
					(captureContext.VisibleChannels[1] ? 1 : 0) << 1 |
					(captureContext.VisibleChannels[2] ? 1 : 0) << 2 |
					(captureContext.VisibleChannels[3] ? 1 : 0) << 3;
				constants.ChannelMask &= ((1u << formatInfo.NumComponents) - 1u);
				constants.MipLevel = (uint32)captureContext.MipLevel;
				constants.Slice = (uint32)captureContext.Slice;
				constants.IsIntegerFormat = formatInfo.Type == FormatType::Integer;

				context.BindRootCBV(0, constants);
				context.BindResources(1, pPickingBuffer->Get()->GetUAV());

				context.Dispatch(ComputeUtils::GetNumThreadGroups(desc.Width, 8, desc.Height, 8));
			});

	graph.AddPass("CaptureTexture.CopyPickData", RGPassFlag::Copy)
		.Read(pPickingBuffer)
		.Write(pReadbackTarget)
		.Bind([=](CommandContext& context)
			{
				context.CopyBuffer(pPickingBuffer->Get(), pReadbackTarget->Get(), sizeof(CaptureTextureContext::PickData), 0, sizeof(CaptureTextureContext::PickData) * captureContext.ReadbackIndex);
			});

	if(captureContext.pReadbackBuffer)
		captureContext.Pick = static_cast<CaptureTextureContext::PickData*>(captureContext.pReadbackBuffer->GetMappedData())[captureContext.ReadbackIndex];
	captureContext.ReadbackIndex = (captureContext.ReadbackIndex + 1) % 3;
}

void CaptureTextureSystem::RenderUI(CaptureTextureContext& captureContext, const ImVec2& viewportOrigin, const ImVec2& viewportSize)
{
	if (captureContext.pTextureTarget)
	{
		struct Group
		{
			Group()
			{
				ImGui::BeginGroup();
				ImGui::Dummy(ImVec2(1, 3));
				ImGui::Dummy(ImVec2(0, 2));
				ImGui::SameLine();
			}

			~Group()
			{
				ImGui::SameLine();
				ImGui::Dummy(ImVec2(0, 0));
				ImGui::Dummy(ImVec2(1, 3));
				ImGui::EndGroup();
				ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0.3f, 0.3f, 0.3f, 1.0f), 2.5f);
			}
		};

		if (ImGui::Begin("Visualize Texture", 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImGui::PushID("VisualizeTexture");
			TextureDesc& desc = captureContext.SourceDesc;
			const FormatInfo& formatInfo = RHI::GetFormatInfo(desc.Format);
			Vector2u mipSize(desc.Width >> captureContext.MipLevel, desc.Height >> captureContext.MipLevel);

			{
				Group group;

				// Channel visibility switches
				{
					auto ChannelButton = [](const char* pName, bool* pValue, bool enabled, const ImVec2& size)
					{
						ImGui::BeginDisabled(!enabled);
						ImGui::ToggleButton(pName, pValue, size);
						ImGui::EndDisabled();
					};

					ImVec2 buttonSize = ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing());

					ChannelButton("R", &captureContext.VisibleChannels[0], formatInfo.NumComponents >= 1, buttonSize);
					ImGui::SameLine();
					ChannelButton("G", &captureContext.VisibleChannels[1], formatInfo.NumComponents >= 2, buttonSize);
					ImGui::SameLine();
					ChannelButton("B", &captureContext.VisibleChannels[2], formatInfo.NumComponents >= 3, buttonSize);
					ImGui::SameLine();
					ChannelButton("A", &captureContext.VisibleChannels[3], formatInfo.NumComponents >= 4, buttonSize);
				}

				ImGui::SameLine();

				// X-Ray mode
				{
					ImVec2 buttonSize = ImVec2(ImGui::GetTextLineHeightWithSpacing(), ImGui::GetTextLineHeightWithSpacing());
					ImGui::ToggleButton(ICON_FA_SEARCH_PLUS, &captureContext.XRay, buttonSize);
				}
			}

			ImGui::SameLine();

			// Mip Selection
			{
				Group group;

				ImGui::BeginDisabled(desc.Mips <= 1);
				std::vector<std::string> mipTexts(desc.Mips);
				for (uint32 i = 0; i < desc.Mips; ++i)
				{
					mipTexts[i] = Sprintf("%d - %dx%d", i, Math::Max(1u, desc.Width >> i), Math::Max(1u, desc.Height >> i));
				}
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Mip");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(170);

				ImGui::Combo("##Mip", &captureContext.MipLevel, [](void* pData, int idx)
					{
						std::string* pStrings = (std::string*)pData;
						return pStrings[idx].c_str();
					}, mipTexts.data(), (int)mipTexts.size());
				ImGui::EndDisabled();
			}

			ImGui::SameLine();

			// Slice/Face Selection
			{
				Group group;

				ImGui::BeginDisabled(desc.Type != TextureType::Texture3D);
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Slice");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(100);
				ImGui::SliderFloat("##SliceNr", &captureContext.Slice, 0, (float)desc.DepthOrArraySize - 1, "%.2f");
				ImGui::EndDisabled();
			}

			{
				Group group;

				ImGui::AlignTextToFramePadding();
				ImGui::Text("Zoom");
				ImGui::SameLine();
				if (ImGui::Button("1:1"))
				{
					captureContext.Scale = 1.0f;
				}
				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_ARROWS_ALT " Fit"))
				{
					ImVec2 ratio = ImGui::GetWindowSize() / ImVec2((float)mipSize.x, (float)mipSize.y);
					captureContext.Scale = ImMin(ratio.x, ratio.y);
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(60);
				float scalePercent = captureContext.Scale * 100.0f;
				if (ImGui::DragFloat("##Scale", &scalePercent, 4.0f, 1.0f, 50000.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic))
				{
					captureContext.Scale = scalePercent / 100.0f;
				}
			}

			ImGui::SameLine();

			// Value Range Slider
			{
				Group group;

				float minValue = 0.0f;
				float maxValue = 1.0f;
				float stepSize = 0.01f;
				float* pMinRange = &captureContext.RangeMin;
				float* pMaxRange = &captureContext.RangeMax;

				constexpr float triangleSize = 5;

				ImGuiWindow* window = ImGui::GetCurrentWindow();
				ImGuiContext& g = *GImGui;
				const ImGuiStyle& style = g.Style;

				ImGui::AlignTextToFramePadding();
				ImGui::Text("Range");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(60);
				ImGui::DragFloat("##RangeMin", pMinRange, stepSize, minValue, *pMaxRange, "%.2f");
				ImGui::SameLine();

				ImGui::SetNextItemWidth(200);
				ImGuiID id = ImGui::GetID("##RangeSlider");
				const float w = ImGui::CalcItemWidth();
				const ImVec2 label_size = ImGui::CalcTextSize("", nullptr, true);
				const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, label_size.y + style.FramePadding.y * 2.0f));
				const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));
				ImGui::ItemSize(total_bb);
				ImGui::ItemAdd(frame_bb, id);

				ImGui::RenderNavHighlight(frame_bb, id);
				ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, ImGuiCol_FrameBgActive, true, g.Style.FrameRounding);

				ImRect item_bb = ImRect(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
				float minRangePosX = Math::RemapRange(*pMinRange, minValue, maxValue, item_bb.Min.x, item_bb.Max.x);
				float maxRangePosX = Math::RemapRange(*pMaxRange, minValue, maxValue, item_bb.Min.x, item_bb.Max.x);

				{
					ImGuiID minRangeHandleID = ImGui::GetID("##SliderMin");
					ImRect minRangeHandleBB(ImVec2(minRangePosX - triangleSize, item_bb.Min.y), ImVec2(minRangePosX + triangleSize, item_bb.Min.y + triangleSize * 2));
					ImGui::ItemAdd(minRangeHandleBB, minRangeHandleID);
					const bool hovered = ImGui::ItemHoverable(minRangeHandleBB, minRangeHandleID, ImGuiItemFlags_None);
					const bool clicked = hovered && ImGui::IsMouseClicked(0, minRangeHandleID);
					if (clicked || g.NavActivateId == minRangeHandleID)
					{
						if (clicked)
							ImGui::SetKeyOwner(ImGuiKey_MouseLeft, minRangeHandleID);
						ImGui::SetActiveID(minRangeHandleID, window);
						ImGui::SetFocusID(minRangeHandleID, window);
						ImGui::FocusWindow(window);
					}
					ImRect grabBB;
					if (ImGui::SliderBehavior(item_bb, minRangeHandleID, ImGuiDataType_Float, pMinRange, &minValue, &maxValue, "", ImGuiSliderFlags_None, &grabBB))
					{
						ImGui::DataTypeClamp(ImGuiDataType_Float, pMinRange, &minValue, pMaxRange);
					}
				}
				{
					ImGuiID maxRangeHandleID = ImGui::GetID("##SliderMax");
					ImRect maxRangeHandleBB(ImVec2(maxRangePosX - triangleSize, item_bb.Max.y - triangleSize * 2), ImVec2(maxRangePosX + triangleSize, item_bb.Max.y));
					ImGui::ItemAdd(maxRangeHandleBB, maxRangeHandleID);
					const bool hovered = ImGui::ItemHoverable(maxRangeHandleBB, maxRangeHandleID, ImGuiItemFlags_None);
					const bool clicked = hovered && ImGui::IsMouseClicked(0, maxRangeHandleID);
					if (clicked || g.NavActivateId == maxRangeHandleID)
					{
						if (clicked)
							ImGui::SetKeyOwner(ImGuiKey_MouseLeft, maxRangeHandleID);
						ImGui::SetActiveID(maxRangeHandleID, window);
						ImGui::SetFocusID(maxRangeHandleID, window);
						ImGui::FocusWindow(window);
					}
					ImRect grabBB;
					if (ImGui::SliderBehavior(item_bb, maxRangeHandleID, ImGuiDataType_Float, pMaxRange, &minValue, &maxValue, "", ImGuiSliderFlags_None, &grabBB))
					{
						ImGui::DataTypeClamp(ImGuiDataType_Float, pMaxRange, pMinRange, &maxValue);
					}
				}

				ImDrawList* pDrawList = ImGui::GetWindowDrawList();

				pDrawList->AddRectFilled(item_bb.Min, item_bb.Max, ImColor(0.3f, 0.8f, 1.0f, 1.0f));
				pDrawList->AddRect(item_bb.Min, item_bb.Max, ImColor(0.0f, 0.0f, 0.0f, 1.0f));
				pDrawList->AddRectFilled(item_bb.Min, ImVec2(minRangePosX, item_bb.Max.y), ImColor(0.0f, 0.0f, 0.0f, 1.0f));
				pDrawList->AddRect(item_bb.Min, ImVec2(minRangePosX, item_bb.Max.y), ImColor(0.0f, 0.0f, 0.0f, 1.0f));
				pDrawList->AddRectFilled(ImVec2(maxRangePosX, item_bb.Min.y), item_bb.Max, ImColor(1.0f, 1.0f, 1.0f, 1.0f));
				pDrawList->AddRect(ImVec2(maxRangePosX, item_bb.Min.y), item_bb.Max, ImColor(0.0f, 0.0f, 0.0f, 1.0f));

				const ImVec2 blackTri[] = {
					ImVec2(-1.0f, 0.0f) * triangleSize + ImVec2(minRangePosX, item_bb.Min.y),
					ImVec2(1.0f, 0.0f) * triangleSize + ImVec2(minRangePosX, item_bb.Min.y),
					ImVec2(0.0f, 2.0f) * triangleSize + ImVec2(minRangePosX, item_bb.Min.y),
				};

				pDrawList->AddTriangleFilled(blackTri[0], blackTri[1], blackTri[2], ImColor(0.0f, 0.0f, 0.0f, 1.0f));
				pDrawList->AddTriangle(blackTri[0], blackTri[1], blackTri[2], ImColor(1.0f, 1.0f, 1.0f, 1.0f));

				const ImVec2 whiteTri[] = {
					ImVec2(1.0f, 0.0f) * triangleSize + ImVec2(maxRangePosX, item_bb.Max.y),
					ImVec2(-1.0f, 0.0f) * triangleSize + ImVec2(maxRangePosX, item_bb.Max.y),
					ImVec2(0.0f, -2.0f) * triangleSize + ImVec2(maxRangePosX, item_bb.Max.y),
				};
				pDrawList->AddTriangleFilled(whiteTri[0], whiteTri[1], whiteTri[2], ImColor(1.0f, 1.0f, 1.0f, 1.0f));
				pDrawList->AddTriangle(whiteTri[0], whiteTri[1], whiteTri[2], ImColor(0.0f, 0.0f, 0.0f, 1.0f));

				ImGui::SameLine();
				ImGui::SetNextItemWidth(60);
				ImGui::DragFloat("##RangeMax", pMaxRange, stepSize, *pMinRange, maxValue, "%.2f");

				ImGui::SameLine();
				if (ImGui::Button(ICON_FA_RECYCLE "##ResetRange"))
				{
					captureContext.RangeMax = 1.0f;
					captureContext.RangeMin = 0.0f;
				}
			}


			// Image
			{
				ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollWithMouse;
				if (captureContext.XRay)
				{
					windowFlags |= ImGuiWindowFlags_NoScrollbar;
				}
				else
				{
					windowFlags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
					windowFlags |= ImGuiWindowFlags_AlwaysHorizontalScrollbar;
				}

				ImVec2 avail = ImGui::GetContentRegionAvail();
				ImGui::BeginChild("##ImageView", ImVec2(avail.x, avail.y - ImGui::GetTextLineHeight()), false, windowFlags);

				ImVec2 UV;

				ImVec2 imageSize = ImVec2((float)mipSize.x, (float)mipSize.y) * captureContext.Scale;
				ImVec2 checkersSize = ImMax(ImGui::GetContentRegionAvail(), imageSize);
				ImVec2 c = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddImage(GraphicsCommon::GetDefaultTexture(DefaultTexture::CheckerPattern), c, c + ImGui::GetContentRegionAvail(), ImVec2(0.0f, 0.0f), checkersSize / 50.0f, ImColor(0.1f, 0.1f, 0.1f, 1.0f));

				static bool held = false;
				if (captureContext.XRay)
				{
					ImVec2 maxSize = ImGui::GetContentRegionAvail() - ImVec2(0, ImGui::GetTextLineHeight());
					ImGui::GetWindowDrawList()->AddImage(captureContext.pTextureTarget, viewportOrigin, viewportOrigin + viewportSize);
					ImGui::ItemSize(viewportSize);
					held = false;
					UV = (ImGui::GetMousePos() - viewportOrigin) / viewportSize;
				}
				else
				{

					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
					ImGui::ImageButton("##ImageView", captureContext.pTextureTarget, imageSize);
					UV = (ImGui::GetMousePos() - ImGui::GetItemRectMin()) / ImGui::GetItemRectSize();
					ImGui::PopStyleVar();
					if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
					{
						held = true;
					}
				}

				captureContext.HoveredPixel = Vector2u((uint32)Math::Floor(UV.x * mipSize.x), (uint32)Math::Floor(UV.y * mipSize.y));

				if (held)
				{
					if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
					{
						held = false;
					}
					else
					{
						ImGuiContext& g = *ImGui::GetCurrentContext();
						ImGuiWindow* window = g.CurrentWindow;
						if (held)
						{
							ImGui::SetScrollX(window, window->Scroll.x - ImGui::GetIO().MouseDelta.x);
							ImGui::SetScrollY(window, window->Scroll.y - ImGui::GetIO().MouseDelta.y);
						}
					}
				}

				ImGui::EndChild();

				if (ImGui::IsItemHovered())
				{
					float wheel = ImGui::GetIO().MouseWheel;
					if (wheel != 0)
					{
						float logScale = logf(captureContext.Scale);
						logScale += wheel / 5.0f;
						captureContext.Scale = Math::Clamp(expf(logScale), 0.0f, 1000.0f);
					}
				}

				// Texture Description
				{
					UV = ImClamp(UV, ImVec2(0, 0), ImVec2(1, 1));
					Vector2u texel((uint32)Math::Floor(UV.x * mipSize.x), (uint32)Math::Floor(UV.y * mipSize.y));
					ImGui::Text("%s - %dx%d %d mips - %s - %8d, %8d (%.4f, %.4f)", captureContext.SourceName.c_str(), desc.Width, desc.Height, desc.Mips, formatInfo.pName, texel.x, texel.y, UV.x, 1.0f - UV.y);

					CaptureTextureContext::PickData pickData = captureContext.Pick;

					const char* componentNames[] = { "R", "G", "B", "A" };

					std::string valueString;
					if (formatInfo.Type == FormatType::Integer)
					{
						for (uint32 i = 0; i < formatInfo.NumComponents; ++i)
						{
							if (i != 0)
								valueString += ", ";
							valueString += Sprintf("%s: %d", componentNames[i], pickData.DataUInt[i]);
						}

						valueString += " (";
						for (uint32 i = 0; i < formatInfo.NumComponents; ++i)
						{
							if (i != 0)
								valueString += ", ";
							valueString += Sprintf("0x%08x", pickData.DataUInt[i]);
						}
						valueString += ")";
					}
					else
					{
						for (uint32 i = 0; i < formatInfo.NumComponents; ++i)
						{
							if (i != 0)
								valueString += ", ";
							valueString += Sprintf("%s: %f", componentNames[i], (&pickData.DataFloat.x)[i]);
						}
					}
					ImGui::SameLine();
					ImGui::Text(" - %s", valueString.c_str());
				}
			}
			ImGui::PopID();
		}
		ImGui::End();
	}
}
