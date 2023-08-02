
#include "stdafx.h"
#include "FooProfiler.h"
#include "imgui_internal.h"
#include "Core/Paths.h"
#include "IconsFontAwesome4.h"

FooProfiler gProfiler;
GPUProfiler gGPUProfiler;

struct HUDContext
{
	float TimelineScale = 5.0f;
	ImVec2 TimelineOffset = ImVec2(0.0f, 0.0f);

	bool IsSelectingRange = false;
	float RangeSelectionStart = 0.0f;
	char SearchString[128]{};
};

static HUDContext gHUDContext;

struct StyleOptions
{
	int MaxDepth = 8;
	int MaxTime = 120;

	float BarHeight = 25;
	float BarPadding = 2;
	ImVec4 BarColorMultiplier = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	ImVec4 BGTextColor = ImVec4(1.0f, 1.0f, 1.0f, 0.5f);
	ImVec4 FGTextColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	ImVec4 BarHighlightColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

	bool DebugMode = false;
};

static StyleOptions gStyle;

void EditStyle()
{
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
	ImGui::SliderInt("Depth", &gStyle.MaxDepth, 1, 12);
	ImGui::SliderInt("Max Time", &gStyle.MaxTime, 8, 66);
	ImGui::SliderFloat("Bar Height", &gStyle.BarHeight, 8, 33);
	ImGui::SliderFloat("Bar Padding", &gStyle.BarPadding, 0, 5);
	ImGui::ColorEdit4("Bar Color Multiplier", &gStyle.BarColorMultiplier.x);
	ImGui::ColorEdit4("Background Text Color", &gStyle.BGTextColor.x);
	ImGui::ColorEdit4("Foreground Text Color", &gStyle.FGTextColor.x);
	ImGui::ColorEdit4("Bar Highlight Color", &gStyle.BarHighlightColor.x);
	ImGui::Checkbox("Debug Mode", &gStyle.DebugMode);
	ImGui::PopItemWidth();
}

void FooProfiler::DrawHUD()
{
	// How many ticks per ms
	uint64 frequency = 0;
	QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);
	float ticksPerMs = (float)frequency / 1000.0f;

	auto TicksToMs = [&](float ticks) { return (float)ticks / ticksPerMs; };
	auto MsToTicks = [&](float ms) { return ms * ticksPerMs; };

	// How many ticks are in the timeline
	float ticksInTimeline = ticksPerMs * gStyle.MaxTime;

	const SampleHistory& data = GetHistory();
	const uint64 beginAnchor = data.TicksBegin;
	const uint64 frameTicks = data.TicksEnd - data.TicksBegin;
	const float frameTime = (float)frameTicks / ticksPerMs;

	ImGui::Checkbox("Pause", &m_Paused);
	ImGui::SameLine();
	ImGui::Text("Frame time: %.2f ms", frameTime);

	ImGui::SameLine(ImGui::GetWindowWidth() - 250);
	ImGui::Text("Filter");
	ImGui::SetNextItemWidth(150);
	ImGui::SameLine();
	ImGui::InputText("##Search", gHUDContext.SearchString, ARRAYSIZE(gHUDContext.SearchString));
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_TIMES "##clearfilter"))
		gHUDContext.SearchString[0] = 0;
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_PAINT_BRUSH "##styleeditor"))
		ImGui::OpenPopup("Style Editor");

	if (ImGui::BeginPopup("Style Editor"))
	{
		EditStyle();
		ImGui::EndPopup();
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Space))
	{
		m_Paused = !m_Paused;
		gGPUProfiler.m_Paused = !gGPUProfiler.m_Paused;
	}

	// Include a row for the track header
	const float trackHeight = (gStyle.MaxDepth + 1) * gStyle.BarHeight;
	const float timelineHeight = trackHeight * m_ThreadData.size();

	// The width of the timeline
	float availableWidth = ImGui::GetContentRegionAvail().x;
	
	// The current (scaled) size of the timeline
	float timelineWidth = availableWidth * gHUDContext.TimelineScale;
	ImVec2 timelineSize = ImVec2(timelineWidth, timelineHeight);

	ImGui::Dummy(ImGui::GetContentRegionAvail() - ImVec2(0, 15));
	ImRect timelineRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	ImGuiID timelineID = ImGui::GetID("Timeline");

	ImVec2 localCursor = timelineRect.Min;

	if (ImGui::ItemAdd(timelineRect, timelineID))
	{
		ImGui::PushClipRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true);

		ImVec2 cursor = localCursor + gHUDContext.TimelineOffset;

		// How many pixels is one tick
		float tickScale = timelineWidth / ticksInTimeline;

		ImDrawList* pDraw = ImGui::GetWindowDrawList();

		// Draw a debug rect around the timeline item and the whole (unclipped) timeline rect
		if (gStyle.DebugMode)
		{
			pDraw->PushClipRect(ImVec2(0, 0), ImVec2(100000, 100000), false);
			pDraw->AddRect(cursor, cursor + timelineSize, ImColor(1.0f, 0.0f, 0.0f));
			pDraw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0.0f, 1.0f, 0.0f));
			pDraw->PopClipRect();
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
			float x0 = tickScale * MsToTicks((float)i);
			float x1 = tickScale * MsToTicks((float)i + 1);
			pDraw->AddRectFilled(ImVec2(cursor.x + x0, localCursor.y + gStyle.BarHeight), ImVec2(cursor.x + x1, localCursor.y + timelineHeight), ImColor(1.0f, 1.0f, 1.0f, 0.02f));
			pDraw->AddText(ImVec2(cursor.x + x0, localCursor.y), ImColor(gStyle.BGTextColor), Sprintf("%d ms", i).c_str());
		}

		// Add thread names for each track
		/*
			0	1	2	3
			Main Thread
			|	|	|	|
			Worker Thread
			|	|	|	|
		*/
		for (int i = 0; i < (int)m_ThreadData.size(); ++i)
		{
			float y = trackHeight * i;
			const ThreadData& threadData = m_ThreadData[i];
			pDraw->AddText(ImVec2(localCursor.x, cursor.y + y), ImColor(gStyle.BGTextColor), Sprintf("%s [%d]", threadData.Name.c_str(), threadData.ThreadID).c_str());
		}

		// Interval times and name headers take up one bar's space
		cursor.y += gStyle.BarHeight;

		// Add horizontal lines for each thread track
		/*
			_______________
			_______________
			_______________
		*/
		for (int i = 0; i < (int)m_ThreadData.size(); ++i)
		{
			float y = trackHeight * i;
			pDraw->AddLine(ImVec2(cursor.x, cursor.y + y), ImVec2(cursor.x + timelineWidth, cursor.y + y), ImColor(gStyle.BGTextColor));
		}

		// Add a bar in the right place for each sample region
		/*
			|[=============]			|
			|	[======]				|
			|---------------------------|
			|		[===========]		|
			|			[======]		|
		*/
		gGPUProfiler.ForEachHistory([&](uint32 frameIndex, GPUProfiler::SampleHistory& data)
			{
				for (uint32 i = 0; i < data.NumRegions; ++i)
				{
					const GPUProfiler::SampleRegion& region = data.Regions[i];
					const GPUProfiler::QueueInfo& queue = gGPUProfiler.GetQueueInfo()[region.QueueIndex];
					uint64 cpuBeginTicks = queue.GpuToCpuTicks(region.BeginTicks);
					uint64 cpuEndTicks = queue.GpuToCpuTicks(region.EndTicks);

					if (cpuEndTicks > beginAnchor)
					{
						float startPos = tickScale * (cpuBeginTicks < beginAnchor ? 0 : cpuBeginTicks - beginAnchor);
						float endPos = tickScale * (cpuEndTicks - beginAnchor);
						float y = region.Depth * gStyle.BarHeight;
						ImRect itemRect = ImRect(cursor + ImVec2(startPos, y), cursor + ImVec2(endPos, y + gStyle.BarHeight));

						uint32 itemID = ImGui::GetID(&region);
						if (ImGui::ItemAdd(itemRect, itemID, 0))
						{
							bool hovered = ImGui::IsItemHovered();
							if (hovered)
							{
								if (ImGui::BeginTooltip())
								{
									ImGui::Text("Frame %d", frameIndex);
									ImGui::Text("%s | %.3f ms", region.pName, TicksToMs((float)(cpuEndTicks - cpuBeginTicks)));
									ImGui::EndTooltip();
								}
							}

							const float rounding = 0.0f;
							const ImVec2 padding(gStyle.BarPadding, gStyle.BarPadding);
							if (hovered)
								pDraw->AddRectFilled(itemRect.Min, itemRect.Max, ImColor(gStyle.BarHighlightColor), rounding);
							pDraw->AddRectFilled(itemRect.Min + padding, itemRect.Max - padding, ImColor(0.4f, 0.3f, 0.7f), rounding);
							ImVec2 textSize = ImGui::CalcTextSize(region.pName);
							if (textSize.x < itemRect.GetWidth() * 0.9f)
							{
								pDraw->AddText(itemRect.Min + (ImVec2(itemRect.GetWidth(), gStyle.BarHeight) - textSize) * 0.5f, ImColor(gStyle.FGTextColor), region.pName);
							}
							else if (itemRect.GetWidth() > 30.0f)
							{
								pDraw->PushClipRect(itemRect.Min + padding, itemRect.Max - padding, true);
								pDraw->AddText(itemRect.Min + ImVec2(4, (gStyle.BarHeight - textSize.y) * 0.5f), ImColor(gStyle.FGTextColor), region.pName);
								pDraw->PopClipRect();
							}
						}
					}
				}
			});

		cursor.y += trackHeight;

		ForEachHistory([&](uint32 frameIndex, const SampleHistory& regionData)
			{
				float frameTimeEnd = (regionData.TicksEnd - beginAnchor) * tickScale;
				pDraw->AddLine(cursor + ImVec2(frameTimeEnd, 0), cursor + ImVec2(frameTimeEnd, timelineHeight), ImColor(1.0f, 1.0f, 1.0f, 0.1f), 4.0f);

				for (uint32 i = 0; i < regionData.CurrentIndex; ++i)
				{
					const SampleRegion& region = regionData.Regions[i];
					if (region.Depth >= (uint32)gStyle.MaxDepth)
						continue;

					ImColor color = ImColor(region.Color);
					if (gHUDContext.SearchString[0] != 0 && !strstr(region.pName, gHUDContext.SearchString))
						color.Value.w *= 0.3f;

					check(region.EndTicks >= region.BeginTicks);
					uint64 numTicks = region.EndTicks - region.BeginTicks;

					float width = tickScale * numTicks;
					float startPos = tickScale * (region.BeginTicks - beginAnchor);

					ImVec2 barTopLeft = cursor + ImVec2(startPos, region.ThreadIndex * trackHeight + gStyle.BarHeight * region.Depth);
					ImVec2 barBottomRight = barTopLeft + ImVec2(width, gStyle.BarHeight);
					ImRect itemRect = ImRect(barTopLeft, barBottomRight);

					uint32 itemID = ImGui::GetID(&region);
					if (ImGui::ItemAdd(itemRect, itemID, 0))
					{
						bool hovered = ImGui::IsItemHovered();
						if (hovered)
						{
							if (ImGui::BeginTooltip())
							{
								ImGui::Text("Frame %d", frameIndex);
								ImGui::Text("%s | %.3f ms", region.pName, TicksToMs((float)(region.EndTicks - region.BeginTicks)));
								if (region.pFilePath)
									ImGui::Text("%s:%d", Paths::GetFileName(region.pFilePath).c_str(), region.LineNumber);
								ImGui::EndTooltip();
							}
						}

						if (ImGui::ButtonBehavior(itemRect, ImGui::GetItemID(), nullptr, nullptr, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_PressedOnDoubleClick))
						{
							// The zoom required to make the bar fit the entire window
							float zoom = timelineWidth / width;
							gHUDContext.TimelineScale = zoom;

							// Recompute the timeline size with new zoom
							float newTimelineWidth = availableWidth * gHUDContext.TimelineScale;
							float newTickScale = newTimelineWidth / ticksInTimeline;
							float newStartPos = newTickScale * (region.BeginTicks - beginAnchor);

							gHUDContext.TimelineOffset.x = -newStartPos;
						}

						const float rounding = 0.0f;
						const ImVec2 padding(gStyle.BarPadding, gStyle.BarPadding);
						if (hovered)
							pDraw->AddRectFilled(itemRect.Min, itemRect.Max, ImColor(gStyle.BarHighlightColor), rounding);
						pDraw->AddRectFilled(itemRect.Min + padding, itemRect.Max - padding, ImColor((ImVec4)color * gStyle.BarColorMultiplier), rounding);
						ImVec2 textSize = ImGui::CalcTextSize(region.pName);
						if (textSize.x < width * 0.9f)
						{
							pDraw->AddText(itemRect.Min + (ImVec2(width, gStyle.BarHeight) - textSize) * 0.5f, ImColor(gStyle.FGTextColor), region.pName);
						}
						else if (width > 30.0f)
						{
							pDraw->PushClipRect(itemRect.Min + padding, itemRect.Max - padding, true);
							pDraw->AddText(itemRect.Min + ImVec2(4, (gStyle.BarHeight - textSize.y) * 0.5f), ImColor(gStyle.FGTextColor), region.pName);
							pDraw->PopClipRect();
						}
					}
				}
			});

		if (ImGui::GetHoveredID() == timelineID)
		{
			// Profile range
			if (!gHUDContext.IsSelectingRange)
			{
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					gHUDContext.RangeSelectionStart = ImGui::GetMousePos().x;
					gHUDContext.IsSelectingRange = true;
				}
			}
			else
			{
				if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
				{
					gHUDContext.IsSelectingRange = false;
				}
				else if (fabs(ImGui::GetMousePos().x - gHUDContext.RangeSelectionStart) > 1)
				{
					pDraw->AddRectFilled(ImVec2(gHUDContext.RangeSelectionStart, localCursor.y), ImVec2(ImGui::GetMousePos().x, localCursor.y + timelineHeight), ImColor(1.0f, 1.0f, 1.0f, 0.2f));

					const ImColor measureColor(1.0f, 1.0f, 1.0f);
					ImVec2 lineStart = ImVec2(gHUDContext.RangeSelectionStart, ImGui::GetMousePos().y);
					ImVec2 lineEnd = ImGui::GetMousePos();
					if (lineStart.x > lineEnd.x)
						std::swap(lineStart.x, lineEnd.x);

					// Add line and arrows
					pDraw->AddLine(lineStart, lineEnd, measureColor);
					pDraw->AddLine(lineStart, lineStart + ImVec2(5, 5), measureColor);
					pDraw->AddLine(lineStart, lineStart + ImVec2(5, -5), measureColor);
					pDraw->AddLine(lineEnd, lineEnd + ImVec2(-5, 5), measureColor);
					pDraw->AddLine(lineEnd, lineEnd + ImVec2(-5, -5), measureColor);

					// Add text in the middle
					std::string text = Sprintf("Time: %.3f ms", TicksToMs(fabs(ImGui::GetMousePos().x - gHUDContext.RangeSelectionStart) / tickScale));
					ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
					pDraw->AddText((lineEnd + lineStart) / 2 - ImVec2(textSize.x * 0.5f, textSize.y), measureColor, text.c_str());
					
				}
			}

			// Zoom behavior
			float zoomDelta = 0.0f;
			if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
				zoomDelta += ImGui::GetIO().MouseWheel / 5.0f;
			zoomDelta -= 0.3f * ImGui::IsKeyPressed(ImGuiKey_O);
			zoomDelta += 0.3f * ImGui::IsKeyPressed(ImGuiKey_P);

			if (zoomDelta != 0)
			{
				float logScale = logf(gHUDContext.TimelineScale);
				logScale += zoomDelta;
				float newScale = Math::Clamp(expf(logScale), 1.0f, 100.0f);

				float scaleFactor = newScale / gHUDContext.TimelineScale;
				gHUDContext.TimelineScale *= scaleFactor;
				ImVec2 mousePos = ImGui::GetMousePos() - localCursor;
				gHUDContext.TimelineOffset.x = mousePos.x - (mousePos.x - gHUDContext.TimelineOffset.x) * scaleFactor;
			}
		}

		// Panning behavior
		bool held;
		ImGui::ButtonBehavior(timelineRect, timelineID, nullptr, &held, ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_AllowItemOverlap);
		ImGui::SetItemAllowOverlap();
		if (held)
			gHUDContext.TimelineOffset += ImGui::GetIO().MouseDelta;

		// Compute the new timeline size to correctly clamp the offset
		timelineWidth = availableWidth * gHUDContext.TimelineScale;
		timelineSize = ImVec2(timelineWidth, timelineHeight);
		gHUDContext.TimelineOffset = ImClamp(gHUDContext.TimelineOffset, ImGui::GetContentRegionAvail() - timelineSize, ImVec2(0.0f, 0.0f));

		ImGui::PopClipRect();
	}

	ImS64 scroll = -(ImS64)gHUDContext.TimelineOffset.x;
	ImGui::ScrollbarEx(ImRect(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImGui::GetContentRegionMax()), ImGui::GetID("Scroll"), ImGuiAxis_X, &scroll, (ImS64)timelineRect.GetSize().x, (ImS64)timelineWidth, ImDrawFlags_None);
	gHUDContext.TimelineOffset.x = -(float)scroll;
}
