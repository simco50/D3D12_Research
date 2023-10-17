
#include "stdafx.h"
#include "Profiler.h"
#include "imgui_internal.h"
#include "Core/Paths.h"
#include "IconsFontAwesome4.h"

struct StyleOptions
{
	int MaxDepth = 10;
	int MaxTime = 80;

	float BarHeight = 25;
	float BarPadding = 2;
	float ScrollBarSize = 15.0f;

	ImVec4 BarColorMultiplier = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	ImVec4 BGTextColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
	ImVec4 FGTextColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
	ImVec4 BarHighlightColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

	bool DebugMode = false;
};

struct HUDContext
{
	StyleOptions Style;

	float TimelineScale = 5.0f;
	ImVec2 TimelineOffset = ImVec2(0.0f, 0.0f);

	bool IsSelectingRange = false;
	float RangeSelectionStart = 0.0f;
	char SearchString[128]{};
	bool PauseThreshold = false;
	float PauseThresholdTime = 100.0f;
	bool IsPaused = false;
};

static HUDContext gHUDContext;
static HUDContext& Context()
{
	return gHUDContext;
}

static void EditStyle(StyleOptions& style)
{
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
	ImGui::SliderInt("Depth", &style.MaxDepth, 1, 12);
	ImGui::SliderInt("Max Time", &style.MaxTime, 8, 66);
	ImGui::SliderFloat("Bar Height", &style.BarHeight, 8, 33);
	ImGui::SliderFloat("Bar Padding", &style.BarPadding, 0, 5);
	ImGui::SliderFloat("Scroll Bar Size", &style.ScrollBarSize, 1.0f, 40.0f);
	ImGui::ColorEdit4("Bar Color Multiplier", &style.BarColorMultiplier.x);
	ImGui::ColorEdit4("Background Text Color", &style.BGTextColor.x);
	ImGui::ColorEdit4("Foreground Text Color", &style.FGTextColor.x);
	ImGui::ColorEdit4("Bar Highlight Color", &style.BarHighlightColor.x);
	ImGui::Separator();
	ImGui::Checkbox("Debug Mode", &style.DebugMode);
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

static void DrawProfilerTimeline(const ImVec2& size = ImVec2(0, 0))
{
	HUDContext& context = gHUDContext;
	StyleOptions& style = context.Style;

	ImVec2 sizeActual = ImGui::CalcItemSize(size, ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);

	ImRect timelineRect(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + sizeActual);
	ImGui::ItemSize(timelineRect.GetSize());

	// The current (scaled) size of the timeline
	float timelineWidth = timelineRect.GetWidth() * context.TimelineScale;

	ImVec2 cursor = timelineRect.Min + context.TimelineOffset;
	ImVec2 cursorStart = cursor;
	ImDrawList* pDraw = ImGui::GetWindowDrawList();

	ImGuiID timelineID = ImGui::GetID("Timeline");
	timelineRect.Max -= ImVec2(style.ScrollBarSize, style.ScrollBarSize);
	if (ImGui::ItemAdd(timelineRect, timelineID))
	{
		ImGui::PushClipRect(timelineRect.Min, timelineRect.Max, true);

		// How many ticks per ms
		uint64 frequency = 0;
		QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);
		const float MsToTicks = (float)frequency / 1000.0f;
		const float TicksToMs = 1000.0f / frequency;

		// How many ticks are in the timeline
		float ticksInTimeline = MsToTicks * style.MaxTime;

		uint64 timelineTicksBegin, timelineTicksEnd;
		gCPUProfiler.GetHistoryRange(timelineTicksBegin, timelineTicksEnd);
		uint64 beginAnchor = timelineTicksBegin;

		// How many pixels is one tick
		const float TicksToPixels = timelineWidth / ticksInTimeline;

		// Add vertical bars for each ms interval
		/*
			0	1	2	3
			|	|	|	|
			|	|	|	|
			|	|	|	|
		*/
		pDraw->AddRectFilled(timelineRect.Min, ImVec2(timelineRect.Max.x, timelineRect.Min.y + style.BarHeight), ImColor(0.0f, 0.0f, 0.0f, 0.1f));
		pDraw->AddRect(timelineRect.Min - ImVec2(10, 0), ImVec2(timelineRect.Max.x + 10, timelineRect.Min.y + style.BarHeight), ImColor(1.0f, 1.0f, 1.0f, 0.4f));
		for (int i = 0; i < style.MaxTime; ++i)
		{
			float x0 = (float)i * MsToTicks * TicksToPixels;
			float msWidth = 1.0f * MsToTicks * TicksToPixels;
			ImVec2 tickPos = ImVec2(cursor.x + x0, timelineRect.Min.y);
			pDraw->AddLine(tickPos + ImVec2(0, style.BarHeight * 0.5f), tickPos + ImVec2(0, style.BarHeight), ImColor(style.BGTextColor));

			if (i % 2 == 0)
			{
				pDraw->AddRectFilled(tickPos + ImVec2(0, style.BarHeight), tickPos + ImVec2(msWidth, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.02f));
				const char* pBarText;
				ImFormatStringToTempBuffer(&pBarText, nullptr, "%d ms", i);
				pDraw->AddText(tickPos + ImVec2(5, 0), ImColor(style.BGTextColor), pBarText);
			}
		}

		cursor.y += style.BarHeight;

		// Add dark shade background for every even frame
		int frameNr = 0;
		gCPUProfiler.ForEachFrame([&](uint32 frameIndex, Span<const CPUProfiler::SampleRegion> regions)
			{
				if (regions.GetSize() > 0 && frameNr++ % 2 == 0)
				{
					float beginOffset = (regions[0].BeginTicks - beginAnchor) * TicksToPixels;
					float endOffset = (regions[0].EndTicks - beginAnchor) * TicksToPixels;
					pDraw->AddRectFilled(ImVec2(cursor.x + beginOffset, timelineRect.Min.y), ImVec2(cursor.x + endOffset, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.05f));
				}
			});

		ImGui::PushClipRect(timelineRect.Min + ImVec2(0, style.BarHeight), timelineRect.Max, true);

		// Common function to draw a single bar
		/*
			[=== SomeFunction (1.2 ms) ===]
		*/
		bool anyHovered = false;
		auto DrawBar = [&](uint32 id, uint64 beginTicks, uint64 endTicks, uint32 depth, const char* pName, bool* pOutHovered = nullptr)
		{
			bool hovered = false;
			if (endTicks > beginAnchor)
			{
				float startPos = (beginTicks < beginAnchor ? 0 : beginTicks - beginAnchor) * TicksToPixels;
				float endPos = (endTicks - beginAnchor) * TicksToPixels;
				float y = depth * style.BarHeight;
				ImRect itemRect = ImRect(cursor + ImVec2(startPos, y), cursor + ImVec2(endPos, y + style.BarHeight));

				// Ensure a bar always has a width
				itemRect.Max.x = ImMax(itemRect.Max.x, itemRect.Min.x + 1);
				if (ImGui::ItemAdd(itemRect, id, 0))
				{
					float ms = TicksToMs * (float)(endTicks - beginTicks);

					ImColor color = ColorFromString(pName) * style.BarColorMultiplier;
					ImColor textColor = style.FGTextColor;
					// Fade out the bars that don't match the filter
					if (context.SearchString[0] != 0 && !strstr(pName, context.SearchString))
					{
						color.Value.w *= 0.3f;
						textColor.Value.w *= 0.5f;
					}
					else if (context.PauseThreshold && ms >= context.PauseThresholdTime)
					{
						gCPUProfiler.SetPaused(true);
						gGPUProfiler.SetPaused(true);
					}

					// Darken the bottom
					ImColor colorBottom = color.Value * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

					hovered = ImGui::IsItemHovered() && !anyHovered;
					anyHovered |= hovered;

					// If the bar is double-clicked, zoom in to make the bar fill the entire window
					if (ImGui::ButtonBehavior(itemRect, ImGui::GetItemID(), nullptr, nullptr, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_PressedOnDoubleClick))
					{
						// Zoom ration to make the bar fit the entire window
						float zoom = timelineWidth / itemRect.GetWidth();
						context.TimelineScale = zoom;

						// Recompute the timeline size with new zoom
						float newTimelineWidth = timelineRect.GetWidth() * context.TimelineScale;
						float newTickScale = newTimelineWidth / ticksInTimeline;
						float newStartPos = newTickScale * (beginTicks - beginAnchor);

						context.TimelineOffset.x = -newStartPos;
					}

					// Draw the bar rect and outline if hovered

					// Only pad if bar is large enough
					float maxPaddingX = ImMax(itemRect.GetWidth() * 0.5f - 1.0f, 0.0f);
					const ImVec2 padding(ImMin(style.BarPadding, maxPaddingX), style.BarPadding);
					if (hovered)
					{
						ImColor highlightColor = color.Value * ImVec4(1.5f, 1.5f, 1.5f, 1.0f);
						color.Value = color.Value * ImVec4(1.2f, 1.2f, 1.2f, 1.0f);
						colorBottom.Value = colorBottom.Value * ImVec4(1.2f, 1.2f, 1.2f, 1.0f);
						pDraw->AddRectFilledMultiColor(itemRect.Min + padding, itemRect.Max - padding, color, color, colorBottom, colorBottom);
						pDraw->AddRect(itemRect.Min, itemRect.Max, highlightColor, 0.0f, ImDrawFlags_None, 3.0f);
					}
					else
					{
						pDraw->AddRectFilledMultiColor(itemRect.Min + padding, itemRect.Max - padding, color, color, colorBottom, colorBottom);
					}

					// If the bar size is large enough, draw the name of the bar on top
					if (itemRect.GetWidth() > 10.0f)
					{
						const char* pBarText;
						ImFormatStringToTempBuffer(&pBarText, nullptr, "%s (%.2f ms)", pName, ms);

						ImVec2 textSize = ImGui::CalcTextSize(pBarText);
						const char* pEtc = "...";
						float etcWidth = 20.0f;
						if (textSize.x < itemRect.GetWidth() * 0.9f)
						{
							pDraw->AddText(itemRect.Min + (ImVec2(itemRect.GetWidth(), style.BarHeight) - textSize) * 0.5f, textColor, pBarText);
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

							ImVec2 textPos = itemRect.Min + ImVec2(4, (style.BarHeight - textSize.y) * 0.5f);
							pDraw->AddText(textPos, textColor, pBarText, pChar);
							pDraw->AddText(textPos + ImVec2(textWidth, 0), textColor, pEtc);
						}
					}
				}
			}
			if (pOutHovered)
				*pOutHovered = hovered;
		};

		// Add track name and expander
		/*
			(>) Main Thread [1234]
		*/
		auto TrackHeader = [&](const char* pName, uint32 id)
		{
			pDraw->AddRectFilled(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y + style.BarHeight), ImColor(0.0f, 0.0f, 0.0f, 0.3f));

			bool isOpen = ImGui::GetCurrentWindow()->StateStorage.GetBool(id, true);
			ImVec2 trackTextCursor = ImVec2(timelineRect.Min.x, cursor.y);

			float caretSize = ImGui::GetTextLineHeight();
			if (ImGui::ItemAdd(ImRect(trackTextCursor, trackTextCursor + ImVec2(caretSize, caretSize)), id))
			{
				if (ImGui::IsItemHovered())
					pDraw->AddRect(ImGui::GetItemRectMin() + ImVec2(2, 2), ImGui::GetItemRectMax() - ImVec2(2, 2), ImColor(style.BGTextColor), 3.0f);
				pDraw->AddText(ImGui::GetItemRectMin() + ImVec2(2, 2), ImColor(style.BGTextColor), isOpen ? ICON_FA_CARET_DOWN : ICON_FA_CARET_RIGHT);
				if (ImGui::ButtonBehavior(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), id, nullptr, nullptr, ImGuiButtonFlags_MouseButtonLeft))
				{
					isOpen = !isOpen;
					ImGui::GetCurrentWindow()->StateStorage.SetBool(id, isOpen);
				}
			}

			trackTextCursor.x += caretSize;
			pDraw->AddText(trackTextCursor, ImColor(style.BGTextColor), pName);
			return isOpen;
		};

		// Draw each GPU thread track
		Span<const GPUProfiler::QueueInfo> queues = gGPUProfiler.GetQueueInfo();
		for (uint32 queueIndex = 0; queueIndex < queues.GetSize(); ++queueIndex)
		{
			const GPUProfiler::QueueInfo& queue = queues[queueIndex];

			// Add thread name for track
			bool isOpen = TrackHeader(queue.Name, ImGui::GetID(&queue));
			uint32 maxDepth = isOpen ? style.MaxDepth : 1;
			uint32 trackDepth = 1;
			cursor.y += style.BarHeight;

			// Add a bar in the right place for each sample region
			/*
				|[=============]			|
				|	[======]				|
			*/
			gGPUProfiler.ForEachFrame([&](uint32 frameIndex, Span<const GPUProfiler::SampleRegion> regions)
				{
					for (const GPUProfiler::SampleRegion& region : regions)
					{
						// Only process regions for the current queue
						if (queueIndex != region.QueueIndex)
							continue;
						// Skip regions above the max depth
						if ((int)region.Depth >= maxDepth)
							continue;

						trackDepth = ImMax(trackDepth, (uint32)region.Depth + 1);

						uint64 cpuBeginTicks = queue.GpuToCpuTicks(region.BeginTicks);
						uint64 cpuEndTicks = queue.GpuToCpuTicks(region.EndTicks);

						bool hovered;
						DrawBar(ImGui::GetID(&region), cpuBeginTicks, cpuEndTicks, region.Depth, region.pName, &hovered);
						if (hovered)
						{
							if (ImGui::BeginTooltip())
							{
								ImGui::Text("%s | %.3f ms", region.pName, TicksToMs * (float)(cpuEndTicks - cpuBeginTicks));
								ImGui::Text("Frame %d", frameIndex);
								if (region.pFilePath)
									ImGui::Text("%s:%d", Paths::GetFileName(region.pFilePath).c_str(), region.LineNumber);
								ImGui::EndTooltip();
							}
						}
					}
				});

			// Add vertical line to end track
			cursor.y += trackDepth * style.BarHeight;
			pDraw->AddLine(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y), ImColor(style.BGTextColor));
		}


		// Split between GPU and CPU tracks
		pDraw->AddLine(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y), ImColor(style.BGTextColor), 4);

		// Draw each CPU thread track
		Span<const CPUProfiler::ThreadData> threads = gCPUProfiler.GetThreads();
		for (uint32 threadIndex = 0; threadIndex < (uint32)threads.GetSize(); ++threadIndex)
		{
			// Add thread name for track
			const CPUProfiler::ThreadData& thread = threads[threadIndex];
			const char* pHeaderText;
			ImFormatStringToTempBuffer(&pHeaderText, nullptr, "%s [%d]", thread.Name, thread.ThreadID);
			bool isOpen = TrackHeader(pHeaderText, ImGui::GetID(&thread));

			uint32 maxDepth = isOpen ? style.MaxDepth : 1;
			uint32 trackDepth = 1;
			cursor.y += style.BarHeight;

			// Add a bar in the right place for each sample region
			/*
				|[=============]			|
				|	[======]				|
			*/
			gCPUProfiler.ForEachFrame([&](uint32 frameIndex, Span<const CPUProfiler::SampleRegion> regions)
				{
					for (const CPUProfiler::SampleRegion& region : regions)
					{
						// Only process regions for the current thread
						if (region.ThreadIndex != threadIndex)
							continue;
						// Skip regions above the max depth
						if (region.Depth >= maxDepth)
							continue;

						trackDepth = ImMax(trackDepth, (uint32)region.Depth + 1);

						bool hovered;
						DrawBar(ImGui::GetID(&region), region.BeginTicks, region.EndTicks, region.Depth, region.pName, &hovered);
						if (hovered)
						{
							if (ImGui::BeginTooltip())
							{
								ImGui::Text("%s | %.3f ms", region.pName, TicksToMs * (float)(region.EndTicks - region.BeginTicks));
								ImGui::Text("Frame %d", frameIndex);
								if (region.pFilePath)
									ImGui::Text("%s:%d", Paths::GetFileName(region.pFilePath).c_str(), region.LineNumber);
								ImGui::EndTooltip();
							}
						}
					}
				});

			// Add vertical line to end track
			cursor.y += trackDepth * style.BarHeight;
			pDraw->AddLine(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y), ImColor(style.BGTextColor));
		}

		// The final height of the timeline
		float timelineHeight = cursor.y - cursorStart.y;

		if (ImGui::IsWindowFocused())
		{
			// Profile range
			// If not currently in selection, start selection when left mouse button is pressed
			if (!context.IsSelectingRange && ImGui::IsMouseHoveringRect(timelineRect.Min, timelineRect.Max))
			{
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					context.RangeSelectionStart = ImGui::GetMousePos().x;
					context.IsSelectingRange = true;
				}
			}
			else if (context.IsSelectingRange)
			{
				// If mouse button is released, exit measuring mode
				if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
				{
					context.IsSelectingRange = false;
				}
				else
				{
					// Distance between mouse cursor and measuring start
					float distance = fabs(ImGui::GetMousePos().x - context.RangeSelectionStart);

					// Fade in based on distance
					float opacity = ImClamp(distance / 30.0f, 0.0f, 1.0f);
					if (opacity > 0.0f)
					{
						float time = (distance / TicksToPixels) * TicksToMs;

						// Draw measure region
						pDraw->AddRectFilled(ImVec2(context.RangeSelectionStart, timelineRect.Min.y), ImVec2(ImGui::GetMousePos().x, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.1f));
						pDraw->AddLine(ImVec2(context.RangeSelectionStart, timelineRect.Min.y), ImVec2(context.RangeSelectionStart, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.3f), 3.0f);
						pDraw->AddLine(ImVec2(ImGui::GetMousePos().x, timelineRect.Min.y), ImVec2(ImGui::GetMousePos().x, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.3f), 3.0f);

						// Add line and arrows
						ImColor measureColor = style.FGTextColor;
						measureColor.Value.w *= opacity;
						ImVec2 lineStart = ImVec2(context.RangeSelectionStart, ImGui::GetMousePos().y);
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
				float logScale = logf(context.TimelineScale);
				logScale += zoomDelta;
				float newScale = ImClamp(expf(logScale), 1.0f, 100.0f);

				float scaleFactor = newScale / context.TimelineScale;
				context.TimelineScale *= scaleFactor;
				ImVec2 mousePos = ImGui::GetMousePos() - timelineRect.Min;
				context.TimelineOffset.x = mousePos.x - (mousePos.x - context.TimelineOffset.x) * scaleFactor;
			}
		}

		// Panning behavior
		bool held;
		ImGui::ButtonBehavior(timelineRect, timelineID, nullptr, &held, ImGuiButtonFlags_MouseButtonRight);
		if (held)
			context.TimelineOffset += ImGui::GetIO().MouseDelta;

		// Compute the new timeline size to correctly clamp the offset
		timelineWidth = timelineRect.GetWidth() * context.TimelineScale;
		context.TimelineOffset = ImClamp(context.TimelineOffset, ImMin(ImVec2(0.0f, 0.0f), timelineRect.GetSize() - ImVec2(timelineWidth, timelineHeight)), ImVec2(0.0f, 0.0f));

		ImGui::PopClipRect();
		ImGui::PopClipRect();

		// Draw a debug rect around the timeline item and the whole (unclipped) timeline rect
		if (style.DebugMode)
		{
			pDraw->PushClipRectFullScreen();
			pDraw->AddRect(cursorStart, cursorStart + ImVec2(timelineWidth, timelineHeight), ImColor(1.0f, 0.0f, 0.0f), 0.0f, ImDrawFlags_None, 3.0f);
			pDraw->AddRect(timelineRect.Min, timelineRect.Max, ImColor(0.0f, 1.0f, 0.0f), 0.0f, ImDrawFlags_None, 2.0f);
			pDraw->PopClipRect();
		}

		// Horizontal scroll bar
		ImS64 scrollH = -(ImS64)context.TimelineOffset.x;
		ImGui::ScrollbarEx(ImRect(ImVec2(timelineRect.Min.x, timelineRect.Max.y), ImVec2(timelineRect.Max.x + style.ScrollBarSize, timelineRect.Max.y + style.ScrollBarSize)), ImGui::GetID("ScrollH"), ImGuiAxis_X, &scrollH, (ImS64)timelineRect.GetSize().x, (ImS64)timelineWidth, ImDrawFlags_None);
		context.TimelineOffset.x = -(float)scrollH;

		// Vertical scroll bar
		ImS64 scrollV = -(ImS64)context.TimelineOffset.y;
		ImGui::ScrollbarEx(ImRect(ImVec2(timelineRect.Max.x, timelineRect.Min.y), ImVec2(timelineRect.Max.x + style.ScrollBarSize, timelineRect.Max.y)), ImGui::GetID("ScrollV"), ImGuiAxis_Y, &scrollV, (ImS64)timelineRect.GetSize().y, (ImS64)timelineHeight, ImDrawFlags_None);
		context.TimelineOffset.y = -(float)scrollV;
	}
}

void DrawProfilerHUD()
{
	HUDContext& context = Context();
	StyleOptions& style = context.Style;

	if (gCPUProfiler.IsPaused())
		ImGui::Text("Paused");
	else
		ImGui::Text("Press Space to pause");

	ImGui::SameLine(ImGui::GetWindowWidth() - 620);

	ImGui::Checkbox("Pause threshold", &Context().PauseThreshold);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(150);
	ImGui::SliderFloat("##Treshold", &context.PauseThresholdTime, 0.0f, 16.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();

	ImGui::Dummy(ImVec2(30, 0));
	ImGui::SameLine();
	
	ImGui::Text("Filter");
	ImGui::SetNextItemWidth(150);
	ImGui::SameLine();
	ImGui::InputText("##Search", context.SearchString, ARRAYSIZE(context.SearchString));
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_TIMES "##clearfilter"))
		context.SearchString[0] = 0;
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_PAINT_BRUSH "##styleeditor"))
		ImGui::OpenPopup("Style Editor");

	if (ImGui::BeginPopup("Style Editor"))
	{
		EditStyle(style);
		ImGui::EndPopup();
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Space))
	{
		context.IsPaused = !context.IsPaused;
	}

	gCPUProfiler.SetPaused(context.IsPaused);
	gGPUProfiler.SetPaused(context.IsPaused);

	DrawProfilerTimeline(ImVec2(0, 0));
}
