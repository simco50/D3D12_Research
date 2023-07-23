#include "stdafx.h"
#include "FooProfiler.h"
#include "imgui_internal.h"

FooProfiler gProfiler;

void FooProfiler::DrawTimings()
{
	static int HistoryIndex = 0;
	// The height of each bar
	static float BarHeight = 25.0f;
	// The max depth of each thread
	static int MaxDepth = 8;
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

	ImGui::Checkbox("Pause", &m_Paused);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100);
	ImGui::SliderFloat("Scale", &timelineScale, 1, 20, "%.2f");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100);
	ImGui::SliderFloat("Bar Height", &BarHeight, 1, 20);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100);
	ImGui::SliderInt("Max Depth", &MaxDepth, 1, 20);

	if (ImGui::IsKeyPressed(ImGuiKey_Space))
		m_Paused = !m_Paused;

	if (m_Paused)
	{
		if (ImGui::Button("<") || ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
			++HistoryIndex;
		ImGui::SameLine();
		if (ImGui::Button(">") || ImGui::IsKeyPressed(ImGuiKey_RightArrow))
			--HistoryIndex;
		HistoryIndex = Math::Clamp(HistoryIndex, 0, (int)m_SampleHistory.size() - 2);
		ImGui::SameLine();
		ImGui::Text("Frame: %d", -HistoryIndex - 1);
	}
	else
	{
		HistoryIndex = 0;
	}

	SampleHistory& data = GetHistoryData(HistoryIndex);

	uint64 frameTicks = data.TicksEnd - data.TicksBegin;
	float frameTime = (float)frameTicks / ticksPerMs;

	ImGui::Text("Frame time: %.2f ms", frameTime);

	float timelineHeight = MaxDepth * BarHeight * m_ThreadData.size();
	if(ImGui::BeginChild("TimelineWindow", ImVec2(0, timelineHeight), false, ImGuiWindowFlags_NoBackground))
	{
		ImVec2 area = ImGui::GetContentRegionAvail();
		if (ImGui::BeginChild("ChildTimelineWindow", ImVec2(200, 0), false, ImGuiWindowFlags_NoBackground))
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

				std::unordered_map<uint32, uint32> threadToIndex;
				for (auto& threadData : m_ThreadData)
					threadToIndex[threadData.second.ThreadID] = (uint32)threadToIndex.size();

				for (int i = 0; i < MaxMs; ++i)
				{
					float x = cursor.x + tickScale * MsToTicks((float)i);
					pDraw->AddLine(ImVec2(x, cursor.y), ImVec2(x, cursor.y + ImGui::GetContentRegionAvail().y), lineColor);
				}

				for (int i = 0; i <= (int)threadToIndex.size(); ++i)
				{
					float y = BarHeight * MaxDepth * i;
					pDraw->AddLine(ImVec2(cursor.x, cursor.y + y), ImVec2(cursor.x + ImGui::GetContentRegionAvail().x, cursor.y + y), lineColor);
				}

				for (uint32 i = 0; i < data.CurrentIndex; ++i)
				{
					const SampleRegion& region = data.Regions[i];
					if (region.StackDepth >= (uint32)MaxDepth)
						continue;

					check(region.EndTicks >= region.BeginTicks);
					uint64 numTicks = region.EndTicks - region.BeginTicks;

					float width = tickScale * numTicks;
					float startPos = tickScale * (region.BeginTicks - data.TicksBegin);

					ImVec2 min(startPos, region.StackDepth* BarHeight + MaxDepth * BarHeight * threadToIndex[region.ThreadID]);
					ImVec2 extents(width, BarHeight);
					ImVec2 max = min + extents;

					if (ImGui::ItemAdd(ImRect(cursor + min, cursor + max), i))
					{
						pDraw->AddRectFilled(cursor + min, cursor + max, ImColor(0, 0, 0));
						pDraw->AddRectFilled(cursor + min + ImVec2(2, 2), cursor + max - ImVec2(2, 2), region.Color);
						ImVec2 textSize = ImGui::CalcTextSize(region.pName);
						if (textSize.x < width * 0.9f)
						{
							pDraw->AddText(cursor + min + (ImVec2(width, BarHeight) - textSize) * 0.5f, ImColor(0.0f, 0.0f, 0.0f), region.pName);
						}
						if (ImGui::IsItemHovered())
						{
							if (ImGui::BeginTooltip())
							{
								ImGui::Text("Name: %s", region.pName);
								ImGui::Text("Time: %f", (float)(region.EndTicks - region.BeginTicks) / frequency * 1000.0f);
								ImGui::EndTooltip();
							}
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
