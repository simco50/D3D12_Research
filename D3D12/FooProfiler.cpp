
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
	int MaxTime = 80;

	float BarHeight = 25;
	float BarPadding = 2;

	ImVec4 BarColorMultiplier = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	ImVec4 BGTextColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
	ImVec4 FGTextColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
	ImVec4 BarHighlightColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

	bool DebugMode = false;
};

static StyleOptions gStyle;

static void EditStyle()
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

// 32-bit FNV hash
static uint32 HashString(const char* pStr)
{
	uint32 result = 0x811c9dc5;
	while (*pStr)
	{
		result ^= *pStr++;
		result *= 0x1000193;
	}
	return result;
}

// From https://github.com/stolk/hsvbench
static ImVec4 HSVtoRGB(float h, float s, float v)
{
	const float h6 = 6.0f * h;
	const float r = fabsf(h6 - 3.0f) - 1.0f;
	const float g = 2.0f - fabsf(h6 - 2.0f);
	const float b = 2.0f - fabsf(h6 - 4.0f);

	const float is = 1.0f - s;
	ImVec4 rgba;
	rgba.x = v * (s * ImClamp(r, 0.0f, 1.0f) + is);
	rgba.y = v * (s * ImClamp(g, 0.0f, 1.0f) + is);
	rgba.z = v * (s * ImClamp(b, 0.0f, 1.0f) + is);
	rgba.w = 1.0f;
	return rgba;
}

// Generate a color from a string. Used to color bars
static ImColor ColorFromString(const char* pName)
{
	uint32 hash = HashString(pName);
	float hashF = (float)hash / UINT32_MAX;
	return ImColor(HSVtoRGB(hashF, 0.5f, 0.6f));
}

void DrawProfilerHUD()
{
	// How many ticks per ms
	uint64 frequency = 0;
	QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);
	float ticksPerMs = (float)frequency / 1000.0f;

	auto TicksToMs = [&](float ticks) { return (float)ticks / ticksPerMs; };
	auto MsToTicks = [&](float ms) { return ms * ticksPerMs; };

	// How many ticks are in the timeline
	float ticksInTimeline = ticksPerMs * gStyle.MaxTime;

	const FooProfiler::SampleHistory& data = gProfiler.GetHistory();
	const FooProfiler::SampleRegion& frameSample = data.Regions[0];
	const uint64 beginAnchor = frameSample.BeginTicks;

	if (gProfiler.IsPaused())
		ImGui::Text("Paused");
	else
		ImGui::Text("Press Space to pause");

	ImGui::SameLine(ImGui::GetWindowWidth() - 260);
	ImGui::Text("Search");
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
		gProfiler.SetPaused(!gProfiler.IsPaused());
		gGPUProfiler.SetPaused(!gGPUProfiler.IsPaused());
	}

	ImRect timelineRect(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImGui::GetContentRegionAvail() - ImVec2(15, 15));
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

		// Add vertical bars for each ms interval
		/*
			0	1	2	3
			|	|	|	|
			|	|	|	|
			|	|	|	|
		*/
		pDraw->AddRectFilled(timelineRect.Min, ImVec2(timelineRect.Max.x, timelineRect.Min.y + gStyle.BarHeight), ImColor(0.0f, 0.0f, 0.0f, 0.1f));
		pDraw->AddRect(timelineRect.Min - ImVec2(10, 0), ImVec2(timelineRect.Max.x + 10, timelineRect.Min.y + gStyle.BarHeight), ImColor(1.0f, 1.0f, 1.0f, 0.4f));
		for (int i = 0; i < gStyle.MaxTime; i += 2)
		{
			float x0 = tickScale * MsToTicks((float)i);
			float msWidth = tickScale * MsToTicks(1);
			ImVec2 tickPos = ImVec2(cursor.x + x0, timelineRect.Min.y);
			pDraw->AddRectFilled(tickPos + ImVec2(0, gStyle.BarHeight), tickPos + ImVec2(msWidth, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.02f));
			const char* pBarText;
			ImFormatStringToTempBuffer(&pBarText, nullptr, "%d ms", i);
			pDraw->AddText(tickPos + ImVec2(5, 0), ImColor(gStyle.BGTextColor), pBarText);
			pDraw->AddLine(tickPos + ImVec2(0, gStyle.BarHeight * 0.5f), tickPos + ImVec2(0, gStyle.BarHeight), ImColor(gStyle.BGTextColor));
			pDraw->AddLine(tickPos + ImVec2(msWidth, gStyle.BarHeight * 0.75f), tickPos + ImVec2(msWidth, gStyle.BarHeight), ImColor(gStyle.BGTextColor));
		}
		cursor.y += gStyle.BarHeight;

		ImGui::PushClipRect(timelineRect.Min + ImVec2(0, gStyle.BarHeight), timelineRect.Max, true);

		// Common function to draw a single bar
		/*
			[=== SomeFunction (1.2 ms) ===]
		*/
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
					ImColor textColor = gStyle.FGTextColor;
					// Fade out the bars that don't match the filter
					if (gHUDContext.SearchString[0] != 0 && !strstr(pName, gHUDContext.SearchString))
					{
						color.Value.w *= 0.3f;
						textColor.Value.w *= 0.5f;
					}
					// Darken the bottom
					ImColor colorBottom = color.Value * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

					// Draw tooltip if hovered
					bool hovered = ImGui::IsItemHovered();
					if (hovered)
					{
						if (ImGui::BeginTooltip())
						{
							tooltipFn();
							ImGui::EndTooltip();
						}
					}

					// If the bar is double-clicked, zoom in to make the bar fill the entire window
					if (ImGui::ButtonBehavior(itemRect, ImGui::GetItemID(), nullptr, nullptr, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_PressedOnDoubleClick))
					{
						// Zoom ration to make the bar fit the entire window
						float zoom = timelineWidth / itemRect.GetWidth();
						gHUDContext.TimelineScale = zoom;

						// Recompute the timeline size with new zoom
						float newTimelineWidth = timelineRect.GetWidth() * gHUDContext.TimelineScale;
						float newTickScale = newTimelineWidth / ticksInTimeline;
						float newStartPos = newTickScale * (beginTicks - beginAnchor);

						gHUDContext.TimelineOffset.x = -newStartPos;
					}

					// Draw the bar rect and outline if hovered
					const ImVec2 padding(gStyle.BarPadding, gStyle.BarPadding);
					pDraw->AddRectFilledMultiColor(itemRect.Min + padding, itemRect.Max - padding, color, color, colorBottom, colorBottom);
					if (hovered)
						pDraw->AddRect(itemRect.Min, itemRect.Max, ImColor(gStyle.BarHighlightColor), 0.0f, ImDrawFlags_None, 3.0f);

					// If the bar size is large enough, draw the name of the bar on top
					if (itemRect.GetWidth() > 10.0f)
					{
						float ms = TicksToMs((float)(endTicks - beginTicks));
						const char* pBarText;
						ImFormatStringToTempBuffer(&pBarText, nullptr, "%s (%.2f ms)", pName, ms);

						ImVec2 textSize = ImGui::CalcTextSize(pBarText);
						const char* pEtc = "...";
						float etcWidth = 20.0f;
						if (textSize.x < itemRect.GetWidth() * 0.9f)
						{
							pDraw->AddText(itemRect.Min + (ImVec2(itemRect.GetWidth(), gStyle.BarHeight) - textSize) * 0.5f, textColor, pBarText);
						}
						else if (itemRect.GetWidth() > etcWidth + 10)
						{
							const char* pChar = pBarText;
							float currentOffset = 10;
							while (*pChar++)
							{
								float width = ImGui::CalcTextSize(pChar, pChar + 1).x;
								if (currentOffset + width + etcWidth > itemRect.GetWidth())
									break;
								currentOffset += width;
							}

							float textWidth = ImGui::CalcTextSize(pBarText, pChar).x;

							ImVec2 textPos = itemRect.Min + ImVec2(4, (gStyle.BarHeight - textSize.y) * 0.5f);
							pDraw->AddText(textPos, textColor, pBarText, pChar);
							pDraw->AddText(textPos + ImVec2(textWidth, 0), textColor, pEtc);
						}
					}
				}
			}
		};

		// Add track name and expander
		/*
			(>) Main Thread [1234]
		*/
		auto TrackHeader = [&](const char* pName, uint32 id)
		{
			pDraw->AddRectFilled(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y + gStyle.BarHeight), ImColor(0.0f, 0.0f, 0.0f, 0.3f));

			bool isOpen = ImGui::GetCurrentWindow()->StateStorage.GetBool(id, true);
			ImVec2 trackTextCursor = ImVec2(timelineRect.Min.x, cursor.y);

			float caretSize = ImGui::GetTextLineHeight();
			if (ImGui::ItemAdd(ImRect(trackTextCursor, trackTextCursor + ImVec2(caretSize, caretSize)), id))
			{
				pDraw->AddText(ImGui::GetItemRectMin() + ImVec2(2, 2), ImColor(gStyle.BGTextColor), isOpen ? ICON_FA_CARET_DOWN : ICON_FA_CARET_RIGHT);
				if (ImGui::ButtonBehavior(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), id, nullptr, nullptr, ImGuiButtonFlags_MouseButtonLeft))
				{
					isOpen = !isOpen;
					ImGui::GetCurrentWindow()->StateStorage.SetBool(id, isOpen);
				}
			}
			trackTextCursor.x += caretSize;
			pDraw->AddText(trackTextCursor, ImColor(gStyle.BGTextColor), pName);
			return isOpen;
		};

		// Draw each GPU thread track
		Span<GPUProfiler::QueueInfo> queues = gGPUProfiler.GetQueueInfo();
		for (uint32 queueIndex = 0; queueIndex < queues.GetSize(); ++queueIndex)
		{
			const GPUProfiler::QueueInfo& queue = queues[queueIndex];

			// Add thread name for track
			bool isOpen = TrackHeader(queue.Name, ImGui::GetID(&queue));
			uint32 maxDepth = isOpen ? gStyle.MaxDepth : 1;
			uint32 trackDepth = 1;
			cursor.y += gStyle.BarHeight;

			// Add a bar in the right place for each sample region
			/*
				|[=============]			|
				|	[======]				|
			*/
			gGPUProfiler.ForEachRegion([&](uint32 frameIndex, GPUProfiler::SampleRegion& region)
				{
					// Only process regions for the current queue
					if (queueIndex != region.QueueIndex)
						return;
					// Skip regions above the max depth
					if ((int)region.Depth >= maxDepth)
						return;

					trackDepth = ImMax(trackDepth, (uint32)region.Depth + 1);

					uint64 cpuBeginTicks = queue.GpuToCpuTicks(region.BeginTicks);
					uint64 cpuEndTicks = queue.GpuToCpuTicks(region.EndTicks);

					DrawBar(ImGui::GetID(&region), cpuBeginTicks, cpuEndTicks, region.Depth, region.pName, ColorFromString(region.pName), [&]()
						{
							ImGui::Text("Frame %d", frameIndex);
							ImGui::Text("%s | %.3f ms", region.pName, TicksToMs((float)(cpuEndTicks - cpuBeginTicks)));
							if (region.pFilePath)
								ImGui::Text("%s:%d", Paths::GetFileName(region.pFilePath).c_str(), region.LineNumber);
						});
				});

			// Add vertical line to end track
			cursor.y += trackDepth * gStyle.BarHeight;
			pDraw->AddLine(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y), ImColor(gStyle.BGTextColor));
		}

		// Split between GPU and CPU tracks
		pDraw->AddLine(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y), ImColor(gStyle.BGTextColor), 4);

		// Draw each CPU thread track
		Span<FooProfiler::ThreadData> threads = gProfiler.GetThreads();
		for (uint32 threadIndex = 0; threadIndex < (uint32)threads.GetSize(); ++threadIndex)
		{
			// Add thread name for track
			const FooProfiler::ThreadData& thread = threads[threadIndex];
			const char* pHeaderText;
			ImFormatStringToTempBuffer(&pHeaderText, nullptr, "%s [%d]", thread.Name, thread.ThreadID);
			bool isOpen = TrackHeader(pHeaderText, ImGui::GetID(&thread));

			uint32 maxDepth = isOpen ? gStyle.MaxDepth : 1;
			uint32 trackDepth = 1;
			cursor.y += gStyle.BarHeight;

			// Add a bar in the right place for each sample region
			/*
				|[=============]			|
				|	[======]				|
			*/
			gProfiler.ForEachRegion([&](uint32 frameIndex, const FooProfiler::SampleRegion& region)
				{
					// Only process regions for the current thread
					if (region.ThreadIndex != threadIndex)
						return;
					// Skip regions above the max depth
					if (region.Depth >= maxDepth)
						return;

					trackDepth = ImMax(trackDepth, (uint32)region.Depth + 1);

					DrawBar(ImGui::GetID(&region), region.BeginTicks, region.EndTicks, region.Depth, region.pName, ColorFromString(region.pName), [&]()
						{
							ImGui::Text("Frame %d", frameIndex);
							ImGui::Text("%s | %.3f ms", region.pName, TicksToMs((float)(region.EndTicks - region.BeginTicks)));
							if (region.pFilePath)
								ImGui::Text("%s:%d", Paths::GetFileName(region.pFilePath).c_str(), region.LineNumber);
						});
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
			// If not currently in selection, start selection when left mouse button is pressed
			if (!gHUDContext.IsSelectingRange && ImGui::IsMouseHoveringRect(timelineRect.Min, timelineRect.Max))
			{
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					gHUDContext.RangeSelectionStart = ImGui::GetMousePos().x;
					gHUDContext.IsSelectingRange = true;
				}
			}
			else if(gHUDContext.IsSelectingRange)
			{
				// If mouse button is released, exit measuring mode
				if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
				{
					gHUDContext.IsSelectingRange = false;
				}
				else
				{
					// Distance between mouse cursor and measuring start
					float distance = fabs(ImGui::GetMousePos().x - gHUDContext.RangeSelectionStart);

					// Fade in based on distance
					float opacity = ImClamp(distance / 30.0f, 0.0f, 1.0f);
					if (opacity > 0.0f)
					{
						float time = TicksToMs(distance / tickScale);

						// Draw measure region
						pDraw->AddRectFilled(ImVec2(gHUDContext.RangeSelectionStart, timelineRect.Min.y), ImVec2(ImGui::GetMousePos().x, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.1f));
						pDraw->AddLine(ImVec2(gHUDContext.RangeSelectionStart, timelineRect.Min.y), ImVec2(gHUDContext.RangeSelectionStart, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.3f), 3.0f);
						pDraw->AddLine(ImVec2(ImGui::GetMousePos().x, timelineRect.Min.y), ImVec2(ImGui::GetMousePos().x, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.3f), 3.0f);

						// Add line and arrows
						ImColor measureColor = gStyle.FGTextColor;
						measureColor.Value.w *= opacity;
						ImVec2 lineStart = ImVec2(gHUDContext.RangeSelectionStart, ImGui::GetMousePos().y);
						ImVec2 lineEnd = ImGui::GetMousePos();
						if (lineStart.x > lineEnd.x)
							std::swap(lineStart.x, lineEnd.x);
						pDraw->AddLine(lineStart, lineEnd, measureColor);
						pDraw->AddLine(lineStart, lineStart + ImVec2(5, 5), measureColor);
						pDraw->AddLine(lineStart, lineStart + ImVec2(5, -5), measureColor);
						pDraw->AddLine(lineEnd, lineEnd + ImVec2(-5, 5), measureColor);
						pDraw->AddLine(lineEnd, lineEnd + ImVec2(-5, -5), measureColor);

						// Add text in the middle
						const char* pTimeText;
						ImFormatStringToTempBuffer(&pTimeText, nullptr, "Time: %.3f ms", time);
						ImVec2 textSize = ImGui::CalcTextSize(pTimeText);
						pDraw->AddText((lineEnd + lineStart) / 2 - ImVec2(textSize.x * 0.5f, textSize.y), measureColor, pTimeText);
					}
				}
			}

			// Zoom behavior
			float zoomDelta = 0.0f;
			if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
				zoomDelta += ImGui::GetIO().MouseWheel / 5.0f;

			if (zoomDelta != 0)
			{
				// Logarithmic scale
				float logScale = logf(gHUDContext.TimelineScale);
				logScale += zoomDelta;
				float newScale = ImClamp(expf(logScale), 1.0f, 100.0f);

				float scaleFactor = newScale / gHUDContext.TimelineScale;
				gHUDContext.TimelineScale *= scaleFactor;
				ImVec2 mousePos = ImGui::GetMousePos() - timelineRect.Min;
				gHUDContext.TimelineOffset.x = mousePos.x - (mousePos.x - gHUDContext.TimelineOffset.x) * scaleFactor;
			}
		}

		// Panning behavior
		bool held;
		ImGui::ButtonBehavior(timelineRect, timelineID, nullptr, &held, ImGuiButtonFlags_MouseButtonRight);
		if (held)
			gHUDContext.TimelineOffset += ImGui::GetIO().MouseDelta;

		// Compute the new timeline size to correctly clamp the offset
		timelineWidth = timelineRect.GetWidth() * gHUDContext.TimelineScale;
		gHUDContext.TimelineOffset = ImClamp(gHUDContext.TimelineOffset, ImMin(ImVec2(0.0f, 0.0f), timelineRect.GetSize() - ImVec2(timelineWidth, timelineHeight)), ImVec2(0.0f, 0.0f));

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

		// Horizontal scroll bar
		ImS64 scrollH = -(ImS64)gHUDContext.TimelineOffset.x;
		ImGui::ScrollbarEx(ImRect(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImGui::GetContentRegionAvail()), ImGui::GetID("ScrollH"), ImGuiAxis_X, &scrollH, (ImS64)timelineRect.GetSize().x, (ImS64)timelineWidth, ImDrawFlags_None);
		gHUDContext.TimelineOffset.x = -(float)scrollH;

		// Vertical scroll bar
		ImS64 scrollV = -(ImS64)gHUDContext.TimelineOffset.y;
		ImGui::ScrollbarEx(ImRect(ImVec2(timelineRect.Max.x, timelineRect.Min.y), ImVec2(timelineRect.Max.x + 15, timelineRect.Max.y)), ImGui::GetID("ScrollV"), ImGuiAxis_Y, &scrollV, (ImS64)timelineRect.GetSize().y, (ImS64)timelineHeight, ImDrawFlags_None);
		gHUDContext.TimelineOffset.y = -(float)scrollV;
	}
}
