#include "stdafx.h"
#include "FooProfiler.h"
#include "imgui_internal.h"
#include "Core/Paths.h"

FooProfiler gProfiler;

struct HUDContext
{
	int HistoryIndex = 0;
	float TimelineScale = 1.0f;
};

static HUDContext gHUDContext;

struct StyleOptions
{
	int MaxDepth = 8;
	int MaxTime = 22;

	float BarHeight = 25;
	float BarPadding = 2;
	ImVec4 BarColorMultiplier = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	ImVec4 LineColor = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
	ImVec4 BGTextColor = ImVec4(1.0f, 1.0f, 1.0f, 0.4f);
	ImVec4 FGTextColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	ImVec4 BarHighlightColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
};

static StyleOptions gStyle;

void EditStyle()
{
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
	ImGui::SliderInt("Depth", &gStyle.MaxDepth, 1, 12);
	ImGui::SliderInt("Max Time", &gStyle.MaxTime, 8, 33);
	ImGui::SliderFloat("Bar Height", &gStyle.BarHeight, 8, 33);
	ImGui::SliderFloat("Bar Padding", &gStyle.BarPadding, 0, 5);
	ImGui::ColorEdit4("Bar Color Multiplier", &gStyle.BarColorMultiplier.x);
	ImGui::ColorEdit4("Line Color", &gStyle.LineColor.x);
	ImGui::ColorEdit4("Background Text Color", &gStyle.BGTextColor.x);
	ImGui::ColorEdit4("Foreground Text Color", &gStyle.FGTextColor.x);
	ImGui::ColorEdit4("Bar Highlight Color", &gStyle.BarHighlightColor.x);
	ImGui::PopItemWidth();
}

void FooProfiler::DrawHUD()
{
	if (ImGui::BeginChild("ProfileHUD", ImVec2(0, 0), false, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBackground))
	{
		static bool styleEditor = false;
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("Tools"))
			{
				ImGui::MenuItem("Style Editor", nullptr, &styleEditor);
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		if (styleEditor)
		{
			ImGui::Begin("Style Editor", &styleEditor);
			EditStyle();
			ImGui::End();
		}

		// How many ticks per ms
		uint64 frequency = 0;
		QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);
		float ticksPerMs = (float)frequency / 1000.0f;

		auto TicksToMs = [&](uint64 ticks) { return (float)ticks / ticksPerMs; };
		auto MsToTicks = [&](float ms) { return ms * ticksPerMs; };

		// How many ticks are in the timeline
		float ticksInTimeline = ticksPerMs * gStyle.MaxTime;

		if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
		{
			ImGui::SetItemKeyOwner(ImGuiKey_LeftCtrl);
			ImGui::SetItemKeyOwner(ImGuiKey_RightCtrl);

			float logScale = logf(gHUDContext.TimelineScale);
			logScale += ImGui::GetIO().MouseWheel / 5.0f;
			gHUDContext.TimelineScale = Math::Clamp(expf(logScale), 1.0f, 100.0f);
		}

		const SampleHistory& data = GetHistoryData(gHUDContext.HistoryIndex);
		uint64 frameTicks = data.TicksEnd - data.TicksBegin;
		float frameTime = (float)frameTicks / ticksPerMs;

		ImGui::Text("Frame time: %.2f ms", frameTime);
		ImGui::SameLine();

		ImGui::Checkbox("Pause", &m_Paused);
		ImGui::SameLine();
		if (ImGui::Button("<") || ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
			++gHUDContext.HistoryIndex;
		ImGui::SameLine();
		if (ImGui::Button(">") || ImGui::IsKeyPressed(ImGuiKey_RightArrow))
			--gHUDContext.HistoryIndex;
		gHUDContext.HistoryIndex = Math::Clamp(gHUDContext.HistoryIndex, 0, (int)m_SampleHistory.size() - 2);
		ImGui::SameLine();
		ImGui::Text("Frame: %d", -gHUDContext.HistoryIndex - 1);

		if (!m_Paused)
			gHUDContext.HistoryIndex = 0;

		if (ImGui::IsKeyPressed(ImGuiKey_Space))
			m_Paused = !m_Paused;

		float timelineHeight = gStyle.MaxDepth * gStyle.BarHeight * m_ThreadData.size();
		if (ImGui::BeginChild("TimelineContainer", ImVec2(0, 0), true, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar))
		{
			ImGuiWindow* pVerticalScrollWindow = ImGui::GetCurrentWindow();
			ImRect areaRect = ImGui::GetCurrentWindow()->Rect();

			if (ImGui::BeginChild("TimelineContainerV", ImVec2(0, timelineHeight), false, ImGuiWindowFlags_NoBackground))
			{
				if(ImGui::BeginChild("Legend", ImVec2(200, 0), false, ImGuiWindowFlags_NoBackground))
				{
					ImVec2 cursor = ImGui::GetCursorScreenPos();
					cursor.y += gStyle.BarHeight;

					for (const ThreadData& thread : m_ThreadData)
					{
						ImGui::SetCursorScreenPos(cursor);
						ImGui::Text("Name: %s", thread.Name.c_str());
						ImGui::Text("ID: %d", thread.ThreadID);
						cursor.y += gStyle.MaxDepth * gStyle.BarHeight;
					}
				}
				ImGui::EndChild();

				ImGui::SameLine();

				if (ImGui::BeginChild("TimelineContainer", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
				{
					// The width of the timeline
					float availableWidth = ImGui::GetContentRegionAvail().x;
					float timelineWidth = availableWidth * gHUDContext.TimelineScale;

					// How many pixels is one tick
					float tickScale = timelineWidth / ticksInTimeline;

					ImGuiWindow* pHorizontalScrollWindow = ImGui::GetCurrentWindow();

					if (ImGui::BeginChild("TimelineWindow2", ImVec2(timelineWidth, 0), false, ImGuiWindowFlags_NoBackground))
					{
						ImDrawList* pDraw = ImGui::GetWindowDrawList();
						ImVec2 cursor = ImGui::GetCursorScreenPos();

						// Drag and move to pan
						if (ImGui::ItemAdd(ImGui::GetCurrentWindow()->ClipRect, ImGui::GetID("##button")))
						{
							bool h, held;
							ImGui::ButtonBehavior(ImGui::GetCurrentWindow()->ClipRect, ImGui::GetID("##button"), &h, &held, ImGuiButtonFlags_MouseButtonLeft);
							if (held)
							{
								ImGui::SetScrollX(pHorizontalScrollWindow, pHorizontalScrollWindow->Scroll.x - ImGui::GetIO().MouseDelta.x);
								ImGui::SetScrollY(pVerticalScrollWindow, pVerticalScrollWindow->Scroll.y - ImGui::GetIO().MouseDelta.y);
							}
						}

						// Add vertical lines for each ms interval
						/*
							0	1	2	3
							|	|	|	|
							|	|	|	|
							|	|	|	|
						*/
						for (int i = 0; i < gStyle.MaxTime; i += 2)
						{
							float x = cursor.x + tickScale * MsToTicks((float)i);
							pDraw->AddLine(ImVec2(x, cursor.y), ImVec2(x, cursor.y + ImGui::GetContentRegionAvail().y), ImColor(gStyle.LineColor));
							pDraw->AddText(ImVec2(x, cursor.y), ImColor(gStyle.BGTextColor), Sprintf("%d ms", i).c_str());
						}
						// Interval times take up one bar's space
						cursor.y += gStyle.BarHeight;

						// Add horizontal lines for each thread track
						/*
							_______________
							_______________
							_______________
						*/
						for (int i = 0; i < (int)m_ThreadData.size(); ++i)
						{
							float y = gStyle.BarHeight * gStyle.MaxDepth * i;
							pDraw->AddLine(ImVec2(cursor.x, cursor.y + y), ImVec2(cursor.x + ImGui::GetContentRegionAvail().x, cursor.y + y), ImColor(gStyle.LineColor));
						}

						// Add a bar in the right place for each sample region
						/*
							|[				]			|
							|	[		]				|
							|---------------------------|
							|		[			]		|
							|			[		]		|
						*/
						for (uint32 i = 0; i < data.CurrentIndex; ++i)
						{
							const SampleRegion& region = data.Regions[i];
							if (region.Depth >= (uint32)gStyle.MaxDepth)
								continue;

							check(region.EndTicks >= region.BeginTicks);
							uint64 numTicks = region.EndTicks - region.BeginTicks;

							float width = tickScale * numTicks;
							float startPos = tickScale * (region.BeginTicks - data.TicksBegin);

							ImVec2 barTopLeft = cursor + ImVec2(startPos, gStyle.BarHeight * (region.ThreadIndex * gStyle.MaxDepth + region.Depth));
							ImVec2 barBottomRight = barTopLeft + ImVec2(width, gStyle.BarHeight);

							uint32 itemID = ImGui::GetID(Sprintf("##bar_%d", i).c_str());
							if (ImGui::ItemAdd(ImRect(barTopLeft, barBottomRight), itemID))
							{
								bool hovered = ImGui::IsItemHovered();
								if (hovered)
								{
									if (ImGui::BeginTooltip())
									{
										ImGui::Text("%s | %.3f ms", region.pName, TicksToMs(region.EndTicks - region.BeginTicks));
										if(region.pFilePath)
											ImGui::Text("%s:%d", Paths::GetFileName(region.pFilePath).c_str(), region.LineNumber);
										ImGui::EndTooltip();
									}
								}

								const float rounding = 0.0f;
								const ImVec2 padding(gStyle.BarPadding, gStyle.BarPadding);
								if(hovered)
									pDraw->AddRectFilled(barTopLeft, barBottomRight, ImColor(gStyle.BarHighlightColor), rounding);
								pDraw->AddRectFilled(barTopLeft + padding, barBottomRight - padding, ImColor((ImVec4)ImColor(region.Color) * gStyle.BarColorMultiplier), rounding);
								ImVec2 textSize = ImGui::CalcTextSize(region.pName);
								if (textSize.x < width * 0.9f)
								{
									pDraw->AddText(barTopLeft + (ImVec2(width, gStyle.BarHeight) - textSize) * 0.5f, ImColor(gStyle.FGTextColor), region.pName);
								}
								else if (width > 30.0f)
								{
									pDraw->PushClipRect(barTopLeft + padding, barBottomRight - padding, true);
									pDraw->AddText(barTopLeft + ImVec2(4, (gStyle.BarHeight - textSize.y) * 0.5f), ImColor(gStyle.FGTextColor), region.pName);
									pDraw->PopClipRect();
								}
							}
						}
					}
					ImGui::EndChild();
				}
				ImGui::EndChild();
			}
			ImGui::EndChild();
		}
		ImGui::EndChild();
	}
	ImGui::EndChild();
}
