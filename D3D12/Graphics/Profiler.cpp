#include "stdafx.h"
#include "Profiler.h"
#include "RHI/Graphics.h"
#include "RHI/CommandContext.h"
#include "RHI/CommandQueue.h"
#include "RHI/Buffer.h"
#include "FooProfiler.h"

void ProfileNode::StartTimer(CommandContext* pInContext)
{
	LastHitFrame = Profiler::Get()->GetFrameIndex();

	pContext = pInContext;
	LARGE_INTEGER start;
	QueryPerformanceCounter(&start);
	CPUStartTime = start.QuadPart;

	if (pInContext)
	{
		GPUTimerIndex = Profiler::Get()->GetNextTimerIndex();
		pContext->GetCommandList()->EndQuery(Profiler::Get()->GetQueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP, GPUTimerIndex * Profiler::QUERY_PAIR_NUM);

#ifdef USE_PIX
		::PIXBeginEvent(pInContext->GetCommandList(), 0, MULTIBYTE_TO_UNICODE(pName));
#endif
	}
	else
	{
#ifdef USE_PIX
		::PIXBeginEvent(~0ull, pName);
#endif
	}
}

void ProfileNode::EndTimer()
{
	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);
	CPUEndTime = end.QuadPart;

	if (pContext)
	{
		pContext->GetCommandList()->EndQuery(Profiler::Get()->GetQueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP, GPUTimerIndex * Profiler::QUERY_PAIR_NUM + 1);
#ifdef USE_PIX
		::PIXEndEvent(pContext->GetCommandList());
#endif
	}
	else
	{
#ifdef USE_PIX
		::PIXEndEvent();
#endif
	}
}

void ProfileNode::PopulateTimes(const uint64* pReadbackData, uint64 cpuFrequency, int frameIndex)
{
	float cpuTime = (float)(CPUEndTime - CPUStartTime) / cpuFrequency * 1000.0f;
	CpuHistory.AddTime(cpuTime);

	if (GPUTimerIndex >= 0)
	{
		check(pReadbackData);
		uint64 start = pReadbackData[GPUTimerIndex * Profiler::QUERY_PAIR_NUM];
		uint64 end = pReadbackData[GPUTimerIndex * Profiler::QUERY_PAIR_NUM + 1];
		uint64 timeFrequency = pContext->GetParent()->GetCommandQueue(pContext->GetType())->GetTimestampFrequency();
		float time = (float)(end - start) / timeFrequency * 1000.0f;
		GpuHistory.AddTime(time);
	}

	for (auto& child : Children)
	{
		child->PopulateTimes(pReadbackData, cpuFrequency, frameIndex);
	}
}

ProfileNode* ProfileNode::GetChild(const char* pInName, int i)
{
	StringHash hash(pInName);
	auto it = Map.find(hash);
	if (it != Map.end())
	{
		return it->second;
	}
	std::unique_ptr<ProfileNode> pNewNode = std::make_unique<ProfileNode>(pInName, this);
	ProfileNode* pNode = Children.insert(Children.begin() + i, std::move(pNewNode))._Ptr->get();
	Map[hash] = pNode;
	return pNode;
}

Profiler* Profiler::Get()
{
	static Profiler profiler;
	return &profiler;
}

void Profiler::Initialize(GraphicsDevice* pParent)
{
	LARGE_INTEGER cpuFrequency;
	QueryPerformanceFrequency(&cpuFrequency);
	m_CpuTimestampFrequency = cpuFrequency.QuadPart;

	D3D12_QUERY_HEAP_DESC desc{};
	desc.Count = HEAP_SIZE;
	desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	desc.NodeMask = 0;
	VERIFY_HR_EX(pParent->GetDevice()->CreateQueryHeap(&desc, IID_PPV_ARGS(m_pQueryHeap.GetAddressOf())), pParent->GetDevice());
	D3D::SetObjectName(m_pQueryHeap.Get(), "Profiler Timestamp Query Heap");

	m_pReadBackBuffer = pParent->CreateBuffer(BufferDesc::CreateReadback(sizeof(uint64) * SwapChain::NUM_FRAMES * HEAP_SIZE), "Profiling Readback Buffer");

	m_pRootBlock = std::make_unique<ProfileNode>("Total", nullptr);
	m_pRootBlock->StartTimer(nullptr);
	m_pCurrentBlock = m_pRootBlock.get();
}

void Profiler::Shutdown()
{
	m_pReadBackBuffer.Reset();
	m_pQueryHeap.Reset();
}

void Profiler::Begin(const char* pName, CommandContext* pContext)
{
	gProfiler.BeginRegion(pName);

	if (m_pCurrentBlock->Map.find(pName) != m_pCurrentBlock->Map.end())
	{
		m_pCurrentBlock = m_pCurrentBlock->GetChild(pName);
		m_pCurrentBlock->StartTimer(pContext);
	}
	else
	{
		uint32 i = 0;
		if (m_pPreviousBlock)
		{
			for (; i < m_pCurrentBlock->Children.size(); ++i)
			{
				if (m_pCurrentBlock->Children[i].get() == m_pPreviousBlock)
				{
					++i;
					break;
				}
			}
		}
		m_pCurrentBlock = m_pCurrentBlock->GetChild(pName, i);
		m_pCurrentBlock->StartTimer(pContext);
	}
}

void Profiler::End()
{
	m_pCurrentBlock->EndTimer();
	m_pPreviousBlock = m_pCurrentBlock;
	m_pCurrentBlock = m_pCurrentBlock->pParent;
	gProfiler.EndRegion();
}

void Profiler::Resolve(CommandContext* pContext)
{
	checkf(m_pCurrentBlock == m_pRootBlock.get(), "The current block isn't the root block then something must've gone wrong!");

	m_pCurrentBlock->EndTimer();
	m_pCurrentBlock->PopulateTimes((const uint64*)m_pReadBackBuffer->GetMappedData(), m_CpuTimestampFrequency, m_CurrentReadbackFrame);

	int offset = MAX_GPU_TIME_QUERIES * QUERY_PAIR_NUM * m_CurrentReadbackFrame;
	pContext->GetCommandList()->ResolveQueryData(m_pQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, m_CurrentTimer * QUERY_PAIR_NUM, m_pReadBackBuffer->GetResource(), offset * sizeof(uint64));

	m_CurrentTimer = 0;
	m_CurrentReadbackFrame = (m_CurrentReadbackFrame + 1) % SwapChain::NUM_FRAMES;
	m_pCurrentBlock->StartTimer(nullptr);
	++m_FrameIndex;
	m_pPreviousBlock = nullptr;
}

int32 Profiler::GetNextTimerIndex()
{
	check(m_CurrentTimer < MAX_GPU_TIME_QUERIES);
	return m_CurrentTimer++;
}

/// IMGUI

void Profiler::DrawImGui()
{
	ImGui::Spacing();
	if (ImGui::BeginTable("Profiling", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
	{
		ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_None, 4);
		ImGui::TableSetupColumn("CPU (ms)", ImGuiTableColumnFlags_None, 1);
		ImGui::TableSetupColumn("GPU (ms)", ImGuiTableColumnFlags_None, 1);
		ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_None, 4);
		ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_None, 4);
		ImGui::TableHeadersRow();

		DrawImGui(m_pRootBlock.get(), 0);

		ImGui::EndTable();
	}
	ImGui::Separator();
}

void Profiler::DrawImGui(const ProfileNode* pNode, int depth)
{
	if (m_FrameIndex - pNode->LastHitFrame < 60)
	{
		static const ImColor CpuColor = ImColor(0, 125, 200);
		static const ImColor GpuColor = ImColor(120, 183, 0);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::PushID(pNode);

		bool expand = false;
		if (pNode->Children.size() > 0)
		{
			expand = ImGui::TreeNodeEx(pNode->pName, depth < 3 ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
		}
		else
		{
			ImGui::Bullet();
			ImGui::Selectable(pNode->pName);
		}

		// 0
		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImU32(CpuColor));
		ImGui::PushStyleColor(ImGuiCol_Text, ImU32(CpuColor));
		float cpuTime = pNode->CpuHistory.GetAverage();
		{
			ImGui::TableNextColumn();
			ImGui::Text("%4.2f ms", cpuTime);
		}
		ImGui::PopStyleColor(2);

		// 1
		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImU32(GpuColor));
		ImGui::PushStyleColor(ImGuiCol_Text, ImU32(GpuColor));
		float gpuTime = pNode->GpuHistory.GetAverage();
		{
			ImGui::TableNextColumn();
			gpuTime > 0 ? ImGui::Text("%4.2f ms", gpuTime) : ImGui::Text("N/A");
		}
		ImGui::PopStyleColor(2);

		// 2
		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImU32(CpuColor));
		ImGui::PushStyleColor(ImGuiCol_Text, ImU32(CpuColor));
		ImGui::TableNextColumn();
		if (cpuTime > 0)
		{
			const float* pData;
			uint32 offset, count;
			pNode->CpuHistory.GetHistory(&pData, &count, &offset);

			ImGui::PlotLines("", pData, count, offset, 0, 0.0f, 0.03f, ImVec2(ImGui::GetColumnWidth(), 0));
		}
		ImGui::PopStyleColor(2);

		// 3
		ImGui::PushStyleColor(ImGuiCol_PlotLines, ImU32(GpuColor));
		ImGui::PushStyleColor(ImGuiCol_Text, ImU32(GpuColor));
		ImGui::TableNextColumn();
		if (gpuTime > 0)
		{
			const float* pData;
			uint32 offset, count;
			pNode->GpuHistory.GetHistory(&pData, &count, &offset);

			ImGui::PlotLines("", pData, count, offset, 0, 0.0f, 0.03f, ImVec2(ImGui::GetColumnWidth(), 0));
		}
		ImGui::PopStyleColor(2);

		if (expand)
		{
			for (auto& pChild : pNode->Children)
			{
				DrawImGui(pChild.get(), depth + 1);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}
