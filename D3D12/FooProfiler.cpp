
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

/**
 * @brief Generate a color from a string. Used to color bars
 */
static ImColor ColorFromString(const char* pName)
{
	/*
	  Converts a given set of HSV values `h', `s', `v' into RGB
	  coordinates. The output RGB values are in the range [0, 1], and
	  the input HSV values are in the ranges h = [0, 360], and s, v =
	  [0, 1], respectively.|
	*/
	auto HSVtoRGB = [](float h, float s, float v)
	{
		ImVec4 rgb;
		float fC = v * s;
		float fHPrime = fmodf(h / 60.0f, 6);
		float fX = fC * (1 - fabs(fmodf(fHPrime, 2) - 1));
		float fM = v - fC;

		if (0 <= fHPrime && fHPrime < 1)
		{
			rgb.x = fC;
			rgb.y = fX;
			rgb.z = 0;
		}
		else if (1 <= fHPrime && fHPrime < 2)
		{
			rgb.x = fX;
			rgb.y = fC;
			rgb.z = 0;
		}
		else if (2 <= fHPrime && fHPrime < 3)
		{
			rgb.x = 0;
			rgb.y = fC;
			rgb.z = fX;
		}
		else if (3 <= fHPrime && fHPrime < 4)
		{
			rgb.x = 0;
			rgb.y = fX;
			rgb.z = fC;
		}
		else if (4 <= fHPrime && fHPrime < 5)
		{
			rgb.x = fX;
			rgb.y = 0;
			rgb.z = fC;
		}
		else if (5 <= fHPrime && fHPrime < 6)
		{
			rgb.x = fC;
			rgb.y = 0;
			rgb.z = fX;
		}
		else
		{
			rgb.x = 0;
			rgb.y = 0;
			rgb.z = 0;
		}

		rgb.x += fM;
		rgb.y += fM;
		rgb.z += fM;
		rgb.w = 1.0f;
		return rgb;
	};


	auto FNVHash = [](const char* pStr)
	{
		uint32 result = 0x811c9dc5;
		while (*pStr)
		{
			result ^= *pStr++;
			result *= 0x1000193;
		}
		return result;
	};

	uint32 hue = FNVHash(pName) % 360;
	return ImColor(HSVtoRGB((float)hue, 0.5f, 0.6f));
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
	const SampleRegion& frameSample = data.Regions[0];
	const uint64 beginAnchor = frameSample.BeginTicks;
	const uint64 frameTicks = frameSample.EndTicks - frameSample.BeginTicks;
	const float frameTime = (float)frameTicks / ticksPerMs;

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

		// Add vertical lines for each ms interval
		/*
			0	1	2	3
			|	|	|	|
			|	|	|	|
			|	|	|	|
		*/

		pDraw->AddRectFilled(timelineRect.Min, ImVec2(timelineRect.Max.x, timelineRect.Min.y + gStyle.BarHeight), ImColor(1.0f, 1.0f, 1.0f, 0.1f));
		pDraw->AddRect(timelineRect.Min - ImVec2(10, 0), ImVec2(timelineRect.Max.x + 10, timelineRect.Min.y + gStyle.BarHeight), ImColor(1.0f, 1.0f, 1.0f, 0.4f));
		for (int i = 0; i < gStyle.MaxTime; i += 2)
		{
			float x0 = tickScale * MsToTicks((float)i);
			float msWidth = tickScale * MsToTicks(1);
			ImVec2 tickPos = ImVec2(cursor.x + x0, timelineRect.Min.y);
			pDraw->AddRectFilled(tickPos + ImVec2(0, gStyle.BarHeight), tickPos + ImVec2(msWidth, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.02f));
			pDraw->AddText(tickPos + ImVec2(5, 0), ImColor(gStyle.BGTextColor), Sprintf("%d ms", i).c_str());
			pDraw->AddLine(tickPos + ImVec2(0, gStyle.BarHeight * 0.5f), tickPos + ImVec2(0, gStyle.BarHeight), ImColor(gStyle.BGTextColor));
			pDraw->AddLine(tickPos + ImVec2(msWidth, gStyle.BarHeight * 0.75f), tickPos + ImVec2(msWidth, gStyle.BarHeight), ImColor(gStyle.BGTextColor));
		}

		cursor.y += gStyle.BarHeight;


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
					ImColor textColor = gStyle.FGTextColor;
					if (gHUDContext.SearchString[0] != 0 && !strstr(pName, gHUDContext.SearchString))
					{
						color.Value.w *= 0.3f;
						textColor.Value.w *= 0.5f;
					}

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
						pDraw->AddRect(itemRect.Min, itemRect.Max, ImColor(gStyle.BarHighlightColor), rounding, ImDrawFlags_None, 3.0f);
					pDraw->AddRectFilled(itemRect.Min + padding, itemRect.Max - padding, color, rounding);
					ImVec2 textSize = ImGui::CalcTextSize(pName);
					const char* pEtc = "...";
					float etcWidth = 20.0f;
					if (textSize.x < itemRect.GetWidth() * 0.9f)
					{
						pDraw->AddText(itemRect.Min + (ImVec2(itemRect.GetWidth(), gStyle.BarHeight) - textSize) * 0.5f, textColor, pName);
					}
					else if (itemRect.GetWidth() > etcWidth + 10)
					{
						const char* pChar = pName;
						float currentOffset = 10;
						while (*pChar++)
						{
							float width = ImGui::CalcTextSize(pChar, pChar + 1).x;
							if (currentOffset + width + etcWidth > itemRect.GetWidth())
								break;
							currentOffset += width;
						}

						float textWidth = ImGui::CalcTextSize(pName, pChar).x;

						ImVec2 textPos = itemRect.Min + ImVec2(4, (gStyle.BarHeight - textSize.y) * 0.5f);
						pDraw->AddText(textPos, textColor, pName, pChar);
						pDraw->AddText(textPos + ImVec2(textWidth, 0), textColor, pEtc);
					}
				}
			}
		};

		// Add track name and expander
		auto TrackHeader = [&](const char* pName, uint32 id)
		{
			ImVec2 trackTextCursor = ImVec2(timelineRect.Min.x, cursor.y);
			ImVec2 circleCenter = trackTextCursor + ImVec2(gStyle.BarHeight, gStyle.BarHeight) * 0.5f;
			float triSize = ImGui::GetTextLineHeight() * 0.2f;

			bool isOpen = ImGui::GetCurrentWindow()->StateStorage.GetBool(id, true);
			ImRect triRect = ImRect(trackTextCursor, trackTextCursor + ImVec2(triSize, triSize) * 5.0f);
			ImGui::ItemAdd(triRect, id);
			pDraw->AddCircle(circleCenter, triSize * 2, ImColor(gStyle.BGTextColor), 0, 1.0);
			if (isOpen)
			{
				float r = triSize;
				ImVec2 a = ImVec2(+0.000f, +0.750f) * r;
				ImVec2 b = ImVec2(-0.866f, -0.750f) * r;
				ImVec2 c = ImVec2(+0.866f, -0.750f) * r;
				pDraw->AddTriangleFilled(circleCenter + a, circleCenter + b, circleCenter + c, ImColor(gStyle.BGTextColor));
			}
			else
			{
				float r = triSize;
				ImVec2 a = ImVec2(+0.750f, +0.000f) * r;
				ImVec2 b = ImVec2(-0.750f, +0.866f) * r;
				ImVec2 c = ImVec2(-0.750f, -0.866f) * r;
				pDraw->AddTriangleFilled(circleCenter + a, circleCenter + b, circleCenter + c, ImColor(gStyle.BGTextColor));
			}
			trackTextCursor.x += gStyle.BarHeight;

			if (ImGui::ButtonBehavior(triRect, id, nullptr, nullptr, ImGuiButtonFlags_MouseButtonLeft))
			{
				isOpen = !isOpen;
				ImGui::GetCurrentWindow()->StateStorage.SetBool(id, isOpen);
			}

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
					if ((int)region.Depth >= maxDepth)
						return;
					if (queueIndex != region.QueueIndex)
						return;

					trackDepth = Math::Max(trackDepth, region.Depth + 1);

					uint64 cpuBeginTicks = queue.GpuToCpuTicks(region.BeginTicks);
					uint64 cpuEndTicks = queue.GpuToCpuTicks(region.EndTicks);

					DrawBar(ImGui::GetID(&region), cpuBeginTicks, cpuEndTicks, region.Depth, region.pName, ColorFromString(region.pName), [&]()
						{
							ImGui::Text("Frame %d", frameIndex);
							ImGui::Text("%s | %.3f ms", region.pName, TicksToMs((float)(cpuEndTicks - cpuBeginTicks)));
						});
				});

			// Add vertical line to end track
			cursor.y += trackDepth * gStyle.BarHeight;
			pDraw->AddLine(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y), ImColor(gStyle.BGTextColor));
		}

		// Split between GPU and CPU tracks
		pDraw->AddLine(ImVec2(timelineRect.Min.x, cursor.y), ImVec2(timelineRect.Max.x, cursor.y), ImColor(gStyle.BGTextColor), 4);

		// Draw each CPU thread track
		for (uint32 threadIndex = 0; threadIndex < (uint32)m_ThreadData.size(); ++threadIndex)
		{
			// Add thread name for track
			const ThreadData& thread = m_ThreadData[threadIndex];
			bool isOpen = TrackHeader(Sprintf("%s [%d]", thread.Name, thread.ThreadID).c_str(), ImGui::GetID(&thread));

			uint32 maxDepth = isOpen ? gStyle.MaxDepth : 1;
			uint32 trackDepth = 1;
			cursor.y += gStyle.BarHeight;

			// Add a bar in the right place for each sample region
			/*
				|[=============]			|
				|	[======]				|
			*/
			ForEachRegion([&](uint32 frameIndex, const SampleRegion& region)
				{
					// Only process regions for the current thread
					if (region.ThreadIndex != threadIndex)
						return;
					if (region.Depth >= maxDepth)
						return;

					trackDepth = Math::Max(trackDepth, region.Depth + 1);

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

		if (ImGui::IsWindowFocused() && ImGui::IsMouseHoveringRect(timelineRect.Min, timelineRect.Max))
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

		ImS64 scrollH = -(ImS64)gHUDContext.TimelineOffset.x;
		ImGui::ScrollbarEx(ImRect(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImGui::GetContentRegionAvail()), ImGui::GetID("ScrollH"), ImGuiAxis_X, &scrollH, (ImS64)timelineRect.GetSize().x, (ImS64)timelineWidth, ImDrawFlags_None);
		gHUDContext.TimelineOffset.x = -(float)scrollH;

		ImS64 scrollV = -(ImS64)gHUDContext.TimelineOffset.y;
		ImGui::ScrollbarEx(ImRect(ImVec2(timelineRect.Max.x, timelineRect.Min.y), ImVec2(timelineRect.Max.x + 15, timelineRect.Max.y)), ImGui::GetID("ScrollV"), ImGuiAxis_Y, &scrollV, (ImS64)timelineRect.GetSize().y, (ImS64)timelineHeight, ImDrawFlags_None);
		gHUDContext.TimelineOffset.y = -(float)scrollV;
	}
}
