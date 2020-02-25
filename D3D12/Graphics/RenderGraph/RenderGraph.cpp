#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/Graphics.h"
#include "Graphics/CommandContext.h"
#include "Graphics/Profiler.h"
#include "ResourceAllocator.h"

RGResourceHandle RGPassBuilder::Read(const RGResourceHandle& resource)
{
	m_RenderGraph.GetResourceNode(resource);
#if RG_DEBUG
	RG_ASSERT(m_Pass.ReadsFrom(resource) == false, "Pass already reads from this resource");
#endif
	m_Pass.m_Reads.push_back(resource);
	return resource;
}

RGResourceHandleMutable RGPassBuilder::Write(RGResourceHandleMutable& resource)
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
	RGResourceHandleMutable newResource = m_RenderGraph.CreateResourceNode(node.pResource);
	m_Pass.m_Writes.push_back(newResource);
	return newResource;
}

RGResourceHandleMutable RGPassBuilder::CreateTexture(const char* pName, const TextureDesc& desc)
{
	return m_RenderGraph.CreateTexture(pName, desc);
}

RGResourceHandleMutable RGPassBuilder::CreateBuffer(const char* pName, const BufferDesc& desc)
{
	return m_RenderGraph.CreateBuffer(pName, desc);
}

void RGPassBuilder::NeverCull()
{
	m_Pass.m_NeverCull = true;
}

RGGraph::RGGraph(RGResourceAllocator* pAllocator)
	: m_pAllocator(pAllocator)
{

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

		for (RGPassBase* pPass : m_RenderPasses)
		{
			//Make all renderpasses that read from "From" also read from "To"
			for (RGResourceHandle read : pPass->m_Reads)
			{
				if (pPass->ReadsFrom(alias.From))
				{
					if (pPass->ReadsFrom(alias.To) == false)
					{
						pPass->m_Reads.push_back(alias.To);
					}
					break;
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
	for (RGPassBase* pPass : m_RenderPasses)
	{
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
		RGPassBase* pWriter = pNode->pWriter;
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
	struct Empty {};
	AddCallbackPass<Empty>("Present",
		[&](RGPassBuilder& builder, Empty&) {
			builder.Read(resource);
			builder.NeverCull();
		},
		[=](CommandContext&, const RGPassResources&, const Empty&) {
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

int64 RGGraph::Execute(Graphics* pGraphics)
{
	CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	for (RGPassBase* pPass : m_RenderPasses)
	{
		if (pPass->m_References > 0)
		{
			ExecutePass(pPass, *pContext, m_pAllocator);
		}
	}
	DestroyData();
	return pContext->Execute(false);
}

void RGGraph::ExecutePass(RGPassBase* pPass, CommandContext& context, RGResourceAllocator* pAllocator)
{
	PrepareResources(pPass, m_pAllocator);

	RGPassResources resources(*this, *pPass);

	//#todo: Automatically insert resource barriers
	//#todo: Check if we're in a Graphics pass and automatically call BeginRenderPass

	GPU_PROFILE_BEGIN(pPass->m_Name, &context);
	pPass->Execute(resources, context);
	GPU_PROFILE_END(&context);

	//#todo: Check if we're in a Graphics pass and automatically call EndRenderPass

	ReleaseResources(pPass, m_pAllocator);
}

void RGGraph::PrepareResources(RGPassBase* pPass, RGResourceAllocator* pAllocator)
{
	for (RGResourceHandle handle : pPass->m_Writes)
	{
		ConditionallyCreateResource(GetResource(handle), pAllocator);
	}
	for (RGResourceHandle handle : pPass->m_Writes)
	{
		ConditionallyCreateResource(GetResource(handle), pAllocator);
	}
}

void RGGraph::ReleaseResources(RGPassBase* pPass, RGResourceAllocator* pAllocator)
{
	for (RGResourceHandle handle : pPass->m_Writes)
	{
		ConditionallyReleaseResource(GetResource(handle), pAllocator);
	}
	for (RGResourceHandle handle : pPass->m_Writes)
	{
		ConditionallyReleaseResource(GetResource(handle), pAllocator);
	}
}

void RGGraph::DestroyData()
{
	for (RGPassBase* pPass : m_RenderPasses)
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

void RGGraph::ConditionallyCreateResource(RGResource* pResource, RGResourceAllocator* pAllocator)
{
	if (pResource->m_pPhysicalResource == nullptr)
	{
		switch (pResource->m_Type)
		{
		case RGResourceType::Texture:
			pResource->m_pPhysicalResource = pAllocator->CreateTexture(static_cast<RGTexture*>(pResource)->GetDesc());
			break;
		case RGResourceType::Buffer:
			break;
		case RGResourceType::None:
		default:
			assert(0);
			break;
		}
	}
}

void RGGraph::ConditionallyReleaseResource(RGResource* pResource, RGResourceAllocator* pAllocator)
{
	pResource->m_References--;
	if (pResource->m_References == 0 && pResource->m_IsImported == false)
	{
		switch (pResource->m_Type)
		{
		case RGResourceType::Texture:
			pAllocator->ReleaseTexture(static_cast<Texture*>(pResource->m_pPhysicalResource));
			break;
		case RGResourceType::Buffer:
			break;
		case RGResourceType::None:
		default:
			assert(0);
			break;
		}
	}
}
