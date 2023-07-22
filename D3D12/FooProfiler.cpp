#include "stdafx.h"
#include "FooProfiler.h"
#include "imgui_internal.h"

FooProfiler gProfiler;

void FooProfiler::DrawTimings()
{
	// The height of each bar
	static float BarHeight = 20.0f;
	// The max depth of each thread
	static int MaxDepth = 6;
	// The zoom scale of the timeline
	static float timelineScale = 1.0f;

	// Max num of frametime to show, timings beyond fall off
	static const int MaxMs = 33;
	static const float MaxFrameTime = 1.0f / MaxMs * 1000.0f;

	const ImColor lineColor(1.0f, 1.0f, 1.0f, 0.1f);

	// How many ticks per ms
	uint64 frequency = 0;
	QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);
	float ticksPerMs = (float)frequency / 1000.0f;

	auto TicksToMs = [&](uint64 ticks) { return (float)ticks / ticksPerMs; };
	auto MsToTicks = [&](float ms) { return ms * ticksPerMs; };

	// How many ticks are in the timeline
	float ticksInTimeline = ticksPerMs * MaxFrameTime;

	if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
	{
		ImGui::SetItemKeyOwner(ImGuiKey_LeftCtrl);
		ImGui::SetItemKeyOwner(ImGuiKey_RightCtrl);

		float logScale = logf(timelineScale);
		logScale += ImGui::GetIO().MouseWheel / 5.0f;
		timelineScale = Math::Clamp(expf(logScale), 1.0f, 20.0f);
	}

	uint64 baseTime = m_SampleRegions[GetThreadData().Head].BeginTime;
	uint64 endTime = m_SampleRegions[GetThreadData().Head].EndTime;
	uint64 frameTicks = endTime - baseTime;
	float frameTime = (float)frameTicks / ticksPerMs;

	ImGui::SetNextItemWidth(100);
	ImGui::SliderFloat("Scale", &timelineScale, 1, 20, "%.2f");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100);
	ImGui::SliderFloat("Bar Height", &BarHeight, 1, 20);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100);
	ImGui::SliderInt("Max Depth", &MaxDepth, 1, 20);
	ImGui::Text("Frame time: %.2f ms", frameTime);

	float timelineHeight = MaxDepth * BarHeight * m_ThreadData.size();
	if(ImGui::BeginChild("TimelineWindow", ImVec2(0, timelineHeight), false, ImGuiWindowFlags_NoBackground))
	{
		ImVec2 area = ImGui::GetContentRegionAvail();
		if (ImGui::BeginChild("TimelineWindow", ImVec2(200, 0), false, ImGuiWindowFlags_NoBackground))
		{
			ImGui::Text("Test");
			ImGui::Text("Test");
			ImGui::Text("Test");
			ImGui::Text("Test");
		}
		ImGui::EndChild();

		ImGui::SameLine();

		if (ImGui::BeginChild("TimelineContainer", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
		{
			// The width of the timeline
			float availableWidth = ImGui::GetContentRegionAvail().x;
			float timelineWidth = availableWidth * timelineScale;

			// How many pixels is one tick
			float tickScale = timelineWidth / ticksInTimeline;

			if (ImGui::BeginChild("TimelineWindow2", ImVec2(timelineWidth, 0), false, ImGuiWindowFlags_NoBackground))
			{
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				ImDrawList* pDraw = ImGui::GetWindowDrawList();
				for (int i = 0; i < MaxMs; ++i)
				{
					float x = cursor.x + tickScale * MsToTicks((float)i);
					pDraw->AddLine(ImVec2(x, cursor.y), ImVec2(x, cursor.y + ImGui::GetContentRegionAvail().y), lineColor);
				}

				std::vector<uint32> sortedThreads;
				for (auto& data : m_ThreadData)
					sortedThreads.push_back(data.second.ThreadID);
				std::sort(sortedThreads.begin(), sortedThreads.end());

				for (uint32 threadId : sortedThreads)
				{
					for(uint32 currentRegion = GetFirstRegion(threadId); currentRegion != 0xFFFFFFFF; currentRegion = m_SampleRegions[currentRegion].Next)
					{
						const SampleRegion& region = m_SampleRegions[currentRegion];
						if (region.Depth >= (uint32)MaxDepth)
							continue;

						check(region.EndTime >= region.BeginTime);
						uint64 numTicks = region.EndTime - region.BeginTime;

						float width = tickScale * numTicks;
						float startPos = tickScale * (region.BeginTime - baseTime);

						ImVec2 min(startPos, region.Depth * BarHeight);
						ImVec2 extents(width, BarHeight);
						ImVec2 max = min + extents;

						if (ImGui::ItemAdd(ImRect(cursor + min, cursor + max), currentRegion))
						{
							pDraw->AddRectFilled(cursor + min, cursor + max, region.Color);
							if (ImGui::IsItemHovered())
							{
								if (ImGui::BeginTooltip())
								{
									ImGui::Text("Name: %s", region.pName);
									ImGui::Text("Time: %f", (float)(region.EndTime - region.BeginTime) / frequency * 1000.0f);
									ImGui::EndTooltip();
								}
							}
						}
					}

					cursor.y += (BarHeight * MaxDepth);
					pDraw->AddLine(cursor, cursor + ImVec2(ImGui::GetContentRegionAvail().x, 0), lineColor);
				}
			}
			ImGui::EndChild();
		}
		ImGui::EndChild();
	}
	ImGui::EndChild();
}
