#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Profiler.h"
#include "ResourceAllocator.h"
#include "Core/CommandLine.h"

RGResourceHandle RGPassBuilder::Read(const RGResourceHandle& resource)
{
	m_RenderGraph.GetResourceNode(resource);
#if RG_DEBUG
	RG_ASSERT(m_Pass.ReadsFrom(resource) == false, "Pass already reads from this resource");
#endif
	m_Pass.m_Reads.push_back(resource);
	return resource;
}

RGResourceHandle RGPassBuilder::Write(RGResourceHandle& resource)
{
#if RG_DEBUG
	RG_ASSERT(m_Pass.WritesTo(resource) == false, "Pass already writes to this resource");
#endif
	const RGNode& node = m_RenderGraph.GetResourceNode(resource);
	RG_ASSERT(node.pResource->m_Version == node.Version, "Version mismatch");
	++node.pResource->m_Version;
	if (node.pResource->m_IsImported)
	{
		m_Pass.m_NeverCull = true;
	}
	resource.Invalidate();
	RGResourceHandle newResource = m_RenderGraph.CreateResourceNode(node.pResource);
	m_Pass.m_Writes.push_back(newResource);
	return newResource;
}

RGResourceHandle RGPassBuilder::CreateTexture(const char* pName, const TextureDesc& desc)
{
	return m_RenderGraph.CreateTexture(pName, desc);
}

RGResourceHandle RGPassBuilder::CreateBuffer(const char* pName, const BufferDesc& desc)
{
	return m_RenderGraph.CreateBuffer(pName, desc);
}

void RGPassBuilder::NeverCull()
{
	m_Pass.m_NeverCull = true;
}

RGGraph::RGGraph(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
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
					pPass->m_Reads.push_back(alias.To);
				}
			}

			//Remove any write to "From" should be removed
			for (size_t i = 0; i < pPass->m_Writes.size(); ++i)
			{
				if (pPass->m_Writes[i] == alias.From)
				{
					std::swap(pPass->m_Writes[i], pPass->m_Writes[pPass->m_Writes.size() - 1]);
					pPass->m_Writes.pop_back();
					break;
				}
			}
		}
	}

	//Set all the compile metadata
	for (RGPass* pPass : m_RenderPasses)
	{
#if 1
		pPass->m_NeverCull = true;
#endif

		pPass->m_References = (int)pPass->m_Writes.size() + (int)pPass->m_NeverCull;

		for (RGResourceHandle read : pPass->m_Reads)
		{
			RGNode& node = m_ResourceNodes[read.Index];
			node.Reads++;
		}

		for (RGResourceHandle read : pPass->m_Writes)
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
			RG_ASSERT(pWriter->m_References >= 1, "Pass is expected to have references");
			--pWriter->m_References;
			if (pWriter->m_References == 0)
			{
				std::vector<RGResourceHandle>& reads = pWriter->m_Reads;
				for (RGResourceHandle resource : reads)
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

	AddPass("Present",
		[&](RGPassBuilder& builder) {
			builder.Read(resource);
			return [=](CommandContext&, const RGPassResources&) {
			};
		});
}

RGResourceHandle RGGraph::MoveResource(RGResourceHandle From, RGResourceHandle To)
{
	RG_ASSERT(IsValidHandle(To), "Resource is invalid");
	const RGNode& node = GetResourceNode(From);
	m_Aliases.push_back(RGResourceAlias{ From, To });
	++node.pResource->m_Version;
	return CreateResourceNode(node.pResource);
}

RGPass& RGGraph::AddPass(RGPass* pPass)
{
	m_RenderPasses.push_back(pPass);

	if (m_ImmediateMode)
	{
		CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		ExecutePass(pPass, *pContext);
		m_LastFenceValue = pContext->Execute(false);
	}

	return *pPass;
}

int64 RGGraph::Execute()
{
	if (m_ImmediateMode == false)
	{
		int eclFrequency = 4;
		int i = 0;
		CommandContext* pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		for (RGPass* pPass : m_RenderPasses)
		{
			if (pPass->m_References > 0)
			{
				ExecutePass(pPass, *pContext);
			}

			++i;
			if (i == eclFrequency)
			{
				i = 0;
				m_LastFenceValue = pContext->Execute(false);
				pContext = m_pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
			}
		}
		if (i > 0)
		{
			m_LastFenceValue = pContext->Execute(false);
		}
	}
	DestroyData();
	return m_LastFenceValue;
}

void RGGraph::ExecutePass(RGPass* pPass, CommandContext& context)
{
	PrepareResources(pPass);

	RGPassResources resources(*this, *pPass);

	//#todo: Automatically insert resource barriers
	//#todo: Check if we're in a Graphics pass and automatically call BeginRenderPass

	{
		GPU_PROFILE_SCOPE(pPass->m_Name, &context);
		pPass->Execute(resources, context);
	}

	//#todo: Check if we're in a Graphics pass and automatically call EndRenderPass

	ReleaseResources(pPass);
}

void RGGraph::PrepareResources(RGPass* pPass)
{
	for (RGResourceHandle handle : pPass->m_Writes)
	{
		ConditionallyCreateResource(GetResource(handle));
	}
	for (RGResourceHandle handle : pPass->m_Writes)
	{
		ConditionallyCreateResource(GetResource(handle));
	}
}

void RGGraph::ReleaseResources(RGPass* pPass)
{
	for (RGResourceHandle handle : pPass->m_Writes)
	{
		ConditionallyReleaseResource(GetResource(handle));
	}
	for (RGResourceHandle handle : pPass->m_Writes)
	{
		ConditionallyReleaseResource(GetResource(handle));
	}
}

void RGGraph::DestroyData()
{
	for (RGPass* pPass : m_RenderPasses)
	{
		delete pPass;
	}
	m_RenderPasses.clear();
	for (RGResource* pResource : m_Resources)
	{
		delete pResource;
	}
	m_Resources.clear();
	m_ResourceNodes.clear();
	m_Aliases.clear();
}

void RGGraph::ConditionallyCreateResource(RGResource* pResource)
{
	if (pResource->m_pPhysicalResource == nullptr)
	{
		switch (pResource->m_Type)
		{
		case RGResourceType::Texture:
			pResource->m_pPhysicalResource = m_pAllocator->CreateTexture(static_cast<RGTexture*>(pResource)->GetDesc());
			break;
		case RGResourceType::Buffer:
			break;
		case RGResourceType::None:
		default:
			noEntry();
			break;
		}
	}
}

void RGGraph::ConditionallyReleaseResource(RGResource* pResource)
{
	pResource->m_References--;
	if (pResource->m_References == 0 && pResource->m_IsImported == false)
	{
		switch (pResource->m_Type)
		{
		case RGResourceType::Texture:
			m_pAllocator->ReleaseTexture(static_cast<Texture*>(pResource->m_pPhysicalResource));
			break;
		case RGResourceType::Buffer:
			break;
		case RGResourceType::None:
		default:
			noEntry();
			break;
		}
	}
}

Texture* RGPassResources::GetTexture(RGResourceHandle handle) const
{
	RG_ASSERT(m_Pass.ReadsFrom(handle) || m_Pass.WritesTo(handle), "Pass doesn't read or write to this resource");
	const RGNode& node = m_Graph.GetResourceNode(handle);
	check(node.pResource);
	check(node.pResource->m_Type == RGResourceType::Texture);
	return static_cast<Texture*>(node.pResource->m_pPhysicalResource);
}
