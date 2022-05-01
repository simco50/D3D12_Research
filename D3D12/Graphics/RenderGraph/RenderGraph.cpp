#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/Profiler.h"
#include "Core/CommandLine.h"

RGPass& RGPass::Read(Span<RGResourceHandle> resources)
{
	for (RGResourceHandle resource : resources)
	{
		Graph.GetResourceNode(resource);
#if RG_DEBUG
		RG_ASSERT(ReadsFrom(resource) == false, "Pass already reads from this resource");
#endif
		Reads.push_back(resource);
	}
	return *this;
}

RGPass& RGPass::RenderTarget(RGResourceHandle& resource, RenderPassAccess access)
{
	Write(&resource);
	RenderTargets.push_back({ resource, access });
	return *this;
}

RGPass& RGPass::DepthStencil(RGResourceHandle& resource, RenderPassAccess depthAccess, bool write, RenderPassAccess stencilAccess)
{
	RG_ASSERT(!DepthStencilTarget.Resource.IsValid(), "Depth Target already assigned");
	write ? Write(&resource) : Read(resource);
	DepthStencilTarget = { resource, depthAccess, stencilAccess ,write };
	return *this;
}

RGPass& RGPass::Write(Span<RGResourceHandle*> resources)
{
	for (RGResourceHandle* pResource : resources)
	{
#if RG_DEBUG
		RG_ASSERT(WritesTo(*pResource) == false, "Pass already writes to this resource");
#endif
		const RGNode& node = Graph.GetResourceNode(*pResource);
		RG_ASSERT(node.pResource->m_Version == node.Version, "Version mismatch");
		++node.pResource->m_Version;
		if (node.pResource->m_IsImported)
		{
			ShouldNeverCull = true;
		}
		pResource->Invalidate();
		*pResource = Graph.CreateResourceNode(node.pResource);
		Writes.push_back(*pResource);
	}
	return *this;
}

RGPass& RGPass::NeverCull()
{
	ShouldNeverCull = true;
	return *this;
}

RGGraph::RGGraph(GraphicsDevice* pDevice, uint64 allocatorSize /*= 0xFFFF*/)
	: m_pDevice(pDevice), m_Allocator(allocatorSize)
{
	m_ImmediateMode = CommandLine::GetBool("rgimmediate");
}

RGGraph::~RGGraph()
{
	DestroyData();
}

void RGGraph::Compile()
{
	//Process all the resource aliases
	for (RGResourceAlias& alias : m_Aliases)
	{
		const RGNode& fromNode = GetResourceNode(alias.From);
		const RGNode& toNode = GetResourceNode(alias.To);

		//Reroute all "to" resources to be the "from" resource
		for (RGNode& node : m_ResourceNodes)
		{
			if (node.pResource == toNode.pResource)
			{
				node.pResource = fromNode.pResource;
			}
		}

		for (RGPass* pPass : m_RenderPasses)
		{
			//Make all renderpasses that read from "From" also read from "To"
			if (pPass->ReadsFrom(alias.From))
			{
				if (pPass->ReadsFrom(alias.To) == false)
				{
					pPass->Reads.push_back(alias.To);
				}
			}

			//Remove any write to "From" should be removed
			for (size_t i = 0; i < pPass->Writes.size(); ++i)
			{
				if (pPass->Writes[i] == alias.From)
				{
					std::swap(pPass->Writes[i], pPass->Writes[pPass->Writes.size() - 1]);
					pPass->Writes.pop_back();
					break;
				}
			}
		}
	}

	//Set all the compile metadata
	for (RGPass* pPass : m_RenderPasses)
	{
#if 1
		pPass->ShouldNeverCull = true;
#endif

		pPass->References = (int)pPass->Writes.size() + (int)pPass->ShouldNeverCull;

		for (RGResourceHandle read : pPass->Reads)
		{
			RGNode& node = m_ResourceNodes[read.Index];
			node.Reads++;
		}

		for (RGResourceHandle read : pPass->Writes)
		{
			RGNode& node = m_ResourceNodes[read.Index];
			node.pWriter = pPass;
		}
	}

	//Do the culling!
	std::queue<RGNode*> stack;
	for (RGNode& node : m_ResourceNodes)
	{
		if (node.Reads == 0)
		{
			stack.push(&node);
		}
	}
	while (!stack.empty())
	{
		const RGNode* pNode = stack.front();
		stack.pop();
		RGPass* pWriter = pNode->pWriter;
		if (pWriter)
		{
			RG_ASSERT(pWriter->References >= 1, "Pass (%s) is expected to have references", pWriter->pName);
			--pWriter->References;
			if (pWriter->References == 0)
			{
				for (RGResourceHandle resource : pWriter->Reads)
				{
					RGNode& node = m_ResourceNodes[resource.Index];
					--node.Reads;
					if (node.Reads == 0)
					{
						stack.push(&node);
					}
				}
			}
		}
	}

	//Set the final reference count
	for (RGNode& node : m_ResourceNodes)
	{
		node.pResource->m_References += node.Reads;
	}
}

void RGGraph::Present(RGResourceHandle resource)
{
	RG_ASSERT(IsValidHandle(resource), "Resource is invalid");

	AddPass("Present")
		.Bind([=](CommandContext&, const RGPassResources&) {});
}

RGResourceHandle RGGraph::MoveResource(RGResourceHandle From, RGResourceHandle To)
{
	RG_ASSERT(IsValidHandle(To), "Resource is invalid");
	const RGNode& node = GetResourceNode(From);
	m_Aliases.push_back(RGResourceAlias{ From, To });
	++node.pResource->m_Version;
	return CreateResourceNode(node.pResource);
}

void RGGraph::PushEvent(const char* pName)
{
	ProfileEvent e;
	e.Begin = true;
	e.PassIndex = (uint32)m_RenderPasses.size();
	e.pName = pName;
	m_Events.push_back(e);
	m_EventStackSize++;
}

void RGGraph::PopEvent()
{
	RG_ASSERT(m_RenderPasses.size() > 0, "Can't pop event before a RenderPass has been added");
	RG_ASSERT(m_EventStackSize > 0, "No Event to Pop");
	ProfileEvent e;
	e.Begin = false;
	e.PassIndex = (uint32)m_RenderPasses.size() - 1;
	e.pName = nullptr;
	m_Events.push_back(e);
	m_EventStackSize--;
}

SyncPoint RGGraph::Execute()
{
	RG_ASSERT(m_EventStackSize == 0, "Missing PopEvent");
	if (m_ImmediateMode == false)
	{
		CommandContext* pContext = m_pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		for(uint32 passIndex = 0; passIndex < (uint32)m_RenderPasses.size(); ++passIndex)
		{
			RGPass* pPass = m_RenderPasses[passIndex];

			ProcessEvents(*pContext, passIndex, true);
			if (pPass->References > 0)
			{
				ExecutePass(pPass, *pContext);
			}
			ProcessEvents(*pContext, passIndex, false);
		}
		m_LastSyncPoint = pContext->Execute(false);
	}
	DestroyData();
	return m_LastSyncPoint;
}

void RGGraph::ExecutePass(RGPass* pPass, CommandContext& context)
{
	PrepareResources(pPass);

	RGPassResources resources(*this, *pPass);

	//#todo: Automatically insert resource barriers
	//#todo: Check if we're in a Graphics pass and automatically call BeginRenderPass

	{
		GPU_PROFILE_SCOPE(pPass->pName, &context);
		pPass->ExecuteCallback.Execute(context, resources);
	}

	//#todo: Check if we're in a Graphics pass and automatically call EndRenderPass

	ReleaseResources(pPass);
}

void RGGraph::PrepareResources(RGPass* pPass)
{
}

void RGGraph::ReleaseResources(RGPass* pPass)
{
}

void RGGraph::DestroyData()
{
	for (RGPass* pPass : m_RenderPasses)
	{
		m_Allocator.Release(pPass);
	}
	m_RenderPasses.clear();
	for (RGResource* pResource : m_Resources)
	{
		m_Allocator.Release(pResource);
	}
	m_Resources.clear();
	m_ResourceNodes.clear();
	m_Aliases.clear();
}

void RGGraph::ProcessEvents(CommandContext& context, uint32 passIndex, bool begin)
{
	for (uint32 eventIdx = m_CurrentEvent; eventIdx < m_Events.size() && m_Events[eventIdx].PassIndex == passIndex; ++eventIdx)
	{
		ProfileEvent& e = m_Events[eventIdx];
		if (e.Begin && begin)
		{
			m_CurrentEvent++;
			GPU_PROFILE_BEGIN(e.pName, &context);
		}
		else if (!e.Begin && !begin)
		{
			m_CurrentEvent++;
			GPU_PROFILE_END();
		}
	}
}

void* RGPassResources::GetResource(RGResourceHandle handle) const
{
	RG_ASSERT(m_Pass.ReadsFrom(handle) || m_Pass.WritesTo(handle), "Resource is not accessed by this pass");
	RGResource* pResource = m_Graph.GetResource(handle);
	return pResource->m_pPhysicalResource;
}
