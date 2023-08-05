
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
	int MaxDepth = 10;
	int MaxTime = 120;

	float BarHeight = 25;
	float BarPadding = 2;
	ImVec4 BarColorMultiplier = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	ImVec4 BGTextColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
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
	ImGuiWindow* pWindow = ImGui::GetCurrentWindow();

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

	ImRect timelineRect(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImGui::GetContentRegionAvail() - ImVec2(0, 15));
	ImGui::ItemSize(timelineRect);

	// The current (scaled) size of the timeline
	float timelineWidth = timelineRect.GetWidth() * gHUDContext.TimelineScale;

	ImVec2 cursor = timelineRect.Min + gHUDContext.TimelineOffset;
	ImVec2 cursorStart = cursor;
	ImDrawList* pDraw = ImGui::GetWindowDrawList();

	ImGuiID timelineID = ImGui::GetID("Timeline");
	if (ImGui::ItemAdd(timelineRect, timelineID))
	{
		ImGui::PushClipRect(timelineRect.Min, timelineRect.Max, true);

		// How many pixels is one tick
		float tickScale = timelineWidth / ticksInTimeline;

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
			pDraw->AddRectFilled(ImVec2(cursor.x + x0, timelineRect.Min.y + gStyle.BarHeight), ImVec2(cursor.x + x1, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.02f));
			pDraw->AddText(ImVec2(cursor.x + x0, timelineRect.Min.y), ImColor(gStyle.BGTextColor), Sprintf("%d ms", i).c_str());
		}

		// Draw a vertical line to mark each CPU frame
		/*
			|		|	|
			|		|	|
			|		|	|
		*/
		ForEachHistory([&](uint32 frameIndex, const SampleHistory& regionData)
			{
				float frameTimeEnd = (regionData.TicksEnd - beginAnchor) * tickScale;
				pDraw->AddLine(ImVec2(cursor.x + frameTimeEnd, timelineRect.Min.y), ImVec2(cursor.x + frameTimeEnd, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.1f), 4.0f);
			});

		cursor.y += gStyle.BarHeight;

		pDraw->AddLine(timelineRect.Min + ImVec2(0, gStyle.BarHeight), ImVec2(timelineRect.Max.x, timelineRect.Min.y + gStyle.BarHeight), ImColor(gStyle.BGTextColor), 3.0f);

		ImGui::PushClipRect(timelineRect.Min + ImVec2(0, gStyle.BarHeight), timelineRect.Max, true);

		// Common function to draw a single bar
		auto DrawBar = [&](uint32 id, uint64 beginTicks, uint64 endTicks, uint32 depth, const char* pName, ImColor barColor, auto tooltipFn)
		{
			if (endTicks > beginAnchor)
			{
				float startPos = tickScale * (beginTicks < beginAnchor ? 0 : beginTicks - beginAnchor);
				float endPos = tickScale * (endTicks - beginAnchor);
				float y = depth * gStyle.BarHeight;
				ImRect itemRect = ImRect(cursor + ImVec2(startPos, y), cursor + ImVec2(endPos, y + gStyle.BarHeight));

				if (ImGui::ItemAdd(itemRect, id, 0))
				{
					ImColor color = barColor * gStyle.BarColorMultiplier;
					if (gHUDContext.SearchString[0] != 0 && !strstr(pName, gHUDContext.SearchString))
						color.Value.w *= 0.3f;

					bool hovered = ImGui::IsItemHovered();
					if (hovered)
					{
						if (ImGui::BeginTooltip())
						{
							tooltipFn();
							ImGui::EndTooltip();
						}
					}

					if (ImGui::ButtonBehavior(itemRect, ImGui::GetItemID(), nullptr, nullptr, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_PressedOnDoubleClick))
					{
						// The zoom required to make the bar fit the entire window
						float zoom = timelineWidth / ImGui::GetItemRectSize().x;
						gHUDContext.TimelineScale = zoom;

						// Recompute the timeline size with new zoom
						float newTimelineWidth = timelineRect.GetWidth() * gHUDContext.TimelineScale;
						float newTickScale = newTimelineWidth / ticksInTimeline;
						float newStartPos = newTickScale * (beginTicks - beginAnchor);

						gHUDContext.TimelineOffset.x = -newStartPos;
					}

					const float rounding = 0.0f;
					const ImVec2 padding(gStyle.BarPadding, gStyle.BarPadding);
					if (hovered)
						pDraw->AddRectFilled(itemRect.Min, itemRect.Max, ImColor(gStyle.BarHighlightColor), rounding);
					pDraw->AddRectFilled(itemRect.Min + padding, itemRect.Max - padding, color, rounding);
					ImVec2 textSize = ImGui::CalcTextSize(pName);
					if (textSize.x < itemRect.GetWidth() * 0.9f)
					{
						pDraw->AddText(itemRect.Min + (ImVec2(itemRect.GetWidth(), gStyle.BarHeight) - textSize) * 0.5f, ImColor(gStyle.FGTextColor), pName);
					}
					else if (itemRect.GetWidth() > 30.0f)
					{
						//pDraw->PushClipRect(itemRect.Min + padding, itemRect.Max - padding, true);
						pDraw->AddText(itemRect.Min + ImVec2(4, (gStyle.BarHeight - textSize.y) * 0.5f), ImColor(gStyle.FGTextColor), pName);
						//pDraw->PopClipRect();
					}
				}
			}
		};

		// Draw each GPU thread track
		Span<GPUProfiler::QueueInfo> queues = gGPUProfiler.GetQueueInfo();
		for (uint32 queueIndex = 0; queueIndex < queues.GetSize(); ++queueIndex)
		{
			const GPUProfiler::QueueInfo& queue = queues[queueIndex];

			// Add thread name for track
			pDraw->AddText(ImVec2(timelineRect.Min.x, cursor.y), ImColor(gStyle.BGTextColor), queue.Name);

			uint32 maxDepth = gStyle.MaxDepth;
			uint32 trackDepth = 1;
			cursor.y += gStyle.BarHeight;

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

						if ((int)region.Depth >= maxDepth)
							continue;

						if (queueIndex != region.QueueIndex)
							continue;

						trackDepth = Math::Max(trackDepth, region.Depth + 1);

						uint64 cpuBeginTicks = queue.GpuToCpuTicks(region.BeginTicks);
						uint64 cpuEndTicks = queue.GpuToCpuTicks(region.EndTicks);

						DrawBar(ImGui::GetID(&region), cpuBeginTicks, cpuEndTicks, region.Depth, region.pName, ImColor(0.491f, 0.650f, 0.455f), [&]()
							{
								ImGui::Text("Frame %d", frameIndex);
								ImGui::Text("%s | %.3f ms", region.pName, TicksToMs((float)(cpuEndTicks - cpuBeginTicks)));
							});
					}
				});

			// Add vertical line to end track
			cursor.y += trackDepth * gStyle.BarHeight;
			pDraw->AddLine(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y), ImColor(gStyle.BGTextColor));
		}

		// Split between GPU and CPU tracks
		pDraw->AddLine(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y), ImColor(gStyle.BGTextColor), 6);

		// Draw each CPU thread track
		for (uint32 threadIndex = 0; threadIndex < (uint32)m_ThreadData.size(); ++threadIndex)
		{
			// Add thread name for track
			const ThreadData& thread = m_ThreadData[threadIndex];
			pDraw->AddText(ImVec2(timelineRect.Min.x, cursor.y), ImColor(gStyle.BGTextColor), Sprintf("%s [%d]", thread.Name.c_str(), thread.ThreadID).c_str());

			uint32 maxDepth = gStyle.MaxDepth;
			uint32 trackDepth = 1;
			cursor.y += gStyle.BarHeight;

			// Add a bar in the right place for each sample region
			/*
				|[=============]			|
				|	[======]				|
				|---------------------------|
				|		[===========]		|
				|			[======]		|
			*/
			ForEachHistory([&](uint32 frameIndex, const SampleHistory& regionData)
				{
					for (uint32 i = 0; i < regionData.CurrentIndex; ++i)
					{
						const SampleRegion& region = regionData.Regions[i];

						// Only process regions for the current thread
						if (region.ThreadIndex != threadIndex)
							continue;

						if (region.Depth >= maxDepth)
							continue;

						trackDepth = Math::Max(trackDepth, region.Depth + 1);

						DrawBar(ImGui::GetID(&region), region.BeginTicks, region.EndTicks, region.Depth, region.pName, region.Color, [&]()
							{
								ImGui::Text("Frame %d", frameIndex);
								ImGui::Text("%s | %.3f ms", region.pName, TicksToMs((float)(region.EndTicks - region.BeginTicks)));
								if (region.pFilePath)
									ImGui::Text("%s:%d", Paths::GetFileName(region.pFilePath).c_str(), region.LineNumber);
							});
					}
				});

			// Add vertical line to end track
			cursor.y += trackDepth * gStyle.BarHeight;
			pDraw->AddLine(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y), ImColor(gStyle.BGTextColor));
		}

		// The final height of the timeline
		float timelineHeight = cursor.y - cursorStart.y;

		if (ImGui::IsWindowFocused())
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
					pDraw->AddRectFilled(ImVec2(gHUDContext.RangeSelectionStart, timelineRect.Min.y), ImVec2(ImGui::GetMousePos().x, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.2f));

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
				ImVec2 mousePos = ImGui::GetMousePos() - timelineRect.Min;
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
		timelineWidth = timelineRect.GetWidth() * gHUDContext.TimelineScale;
		gHUDContext.TimelineOffset = ImClamp(gHUDContext.TimelineOffset, timelineRect.GetSize() - ImVec2(timelineWidth, timelineHeight), ImVec2(0.0f, 0.0f));

		ImGui::PopClipRect();
		ImGui::PopClipRect();

		// Draw a debug rect around the timeline item and the whole (unclipped) timeline rect
		if (gStyle.DebugMode)
		{
			pDraw->PushClipRectFullScreen();
			pDraw->AddRect(cursorStart, cursorStart + ImVec2(timelineWidth, timelineHeight), ImColor(1.0f, 0.0f, 0.0f), 0.0f, ImDrawFlags_None, 3.0f);
			pDraw->AddRect(timelineRect.Min, timelineRect.Max, ImColor(0.0f, 1.0f, 0.0f), 0.0f, ImDrawFlags_None, 2.0f);
			pDraw->PopClipRect();
		}
	}

	ImS64 scroll = -(ImS64)gHUDContext.TimelineOffset.x;
	ImGui::ScrollbarEx(ImRect(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImGui::GetContentRegionAvail()), ImGui::GetID("Scroll"), ImGuiAxis_X, &scroll, (ImS64)timelineRect.GetSize().x, (ImS64)timelineWidth, ImDrawFlags_None);
	gHUDContext.TimelineOffset.x = -(float)scroll;
}
