#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/Graphics.h"
#include "Graphics/CommandContext.h"
#include "Graphics/Profiler.h"
#include "ResourceAllocator.h"

namespace RG
{
	ResourceHandle RenderPassBuilder::Read(const ResourceHandle& resource)
	{
		m_RenderGraph.GetResourceNode(resource);
#if RG_DEBUG
		RG_ASSERT(m_Pass.ReadsFrom(resource) == false, "Pass already reads from this resource");
#endif
		m_Pass.m_Reads.push_back(resource);
		return resource;
	}

	ResourceHandleMutable RenderPassBuilder::Write(ResourceHandleMutable& resource)
	{
#if RG_DEBUG
		RG_ASSERT(m_Pass.WritesTo(resource) == false, "Pass already writes to this resource");
#endif
		const ResourceNode& node = m_RenderGraph.GetResourceNode(resource);
		RG_ASSERT(node.m_pResource->m_Version == node.m_Version, "Version mismatch");
		++node.m_pResource->m_Version;
		if (node.m_pResource->m_IsImported)
		{
			m_Pass.m_NeverCull = true;
		}
		resource.Invalidate();
		ResourceHandleMutable newResource = m_RenderGraph.CreateResourceNode(node.m_pResource);
		m_Pass.m_Writes.push_back(newResource);
		return newResource;
	}

	ResourceHandleMutable RenderPassBuilder::CreateTexture(const char* pName, const TextureDesc& desc)
	{
		return m_RenderGraph.CreateTexture(pName, desc);
	}

	ResourceHandleMutable RenderPassBuilder::CreateBuffer(const char* pName, const BufferDesc& desc)
	{
		return m_RenderGraph.CreateBuffer(pName, desc);
	}

	void RenderPassBuilder::NeverCull()
	{
		m_Pass.m_NeverCull = true;
	}

	RenderGraph::RenderGraph(ResourceAllocator* pAllocator)
		: m_pAllocator(pAllocator)
	{

	}

	RenderGraph::~RenderGraph()
	{
		DestroyData();
	}

	void RenderGraph::Compile()
	{
		//Process all the resource aliases
		for (ResourceAlias& alias : m_Aliases)
		{
			const ResourceNode& fromNode = GetResourceNode(alias.From);
			const ResourceNode& toNode = GetResourceNode(alias.To);
			
			//Reroute all "to" resources to be the "from" resource
			for (ResourceNode& node : m_ResourceNodes)
			{
				if (node.m_pResource == toNode.m_pResource)
				{
					node.m_pResource = fromNode.m_pResource;
				}
			}

			for (RenderPassBase* pPass : m_RenderPasses)
			{
				//Make all renderpasses that read from "From" also read from "To"
				for (ResourceHandle read : pPass->m_Reads)
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
		for (RenderPassBase* pPass : m_RenderPasses)
		{
			pPass->m_References = (int)pPass->m_Writes.size() + (int)pPass->m_NeverCull;

			for (ResourceHandle read : pPass->m_Reads)
			{
				ResourceNode& node = m_ResourceNodes[read.Index];
				node.m_Reads++;
			}

			for (ResourceHandle read : pPass->m_Writes)
			{
				ResourceNode& node = m_ResourceNodes[read.Index];
				node.m_pWriter = pPass;
			}
		}

		//Do the culling!
		std::queue<ResourceNode*> stack;
		for (ResourceNode& node : m_ResourceNodes)
		{
			if (node.m_Reads == 0)
			{
				stack.push(&node);
			}
		}
		while (!stack.empty())
		{
			const ResourceNode* pNode = stack.front();
			stack.pop();
			RenderPassBase* pWriter = pNode->m_pWriter;
			if (pWriter)
			{
				RG_ASSERT(pWriter->m_References >= 1, "Pass is expected to have references");
				--pWriter->m_References;
				if (pWriter->m_References == 0)
				{
					std::vector<ResourceHandle>& reads = pWriter->m_Reads;
					for (ResourceHandle resource : reads)
					{
						ResourceNode& node = m_ResourceNodes[resource.Index];
						--node.m_Reads;
						if (node.m_Reads == 0)
						{
							stack.push(&node);
						}
					}
				}
			}
		}

		//Set the final reference count
		for (ResourceNode& node : m_ResourceNodes)
		{
			node.m_pResource->m_References += node.m_Reads;
		}

		//Find when to create and destroy resources
		for (RenderPassBase* pPass : m_RenderPasses)
		{
			if (pPass->m_References == 0)
			{
				RG_ASSERT(pPass->m_NeverCull == false, "A pass that shouldn't get culled can not have 0 references");
				continue;
			}

			for (ResourceHandle read : pPass->m_Reads)
			{
				VirtualResourceBase* pResource = GetResource(read);
				if (pResource->pFirstPassUsage == nullptr)
				{
					pResource->pFirstPassUsage = pPass;
				}
				pResource->pLastPassUsage = pPass;
			}

			for (ResourceHandle write : pPass->m_Writes)
			{
				VirtualResourceBase* pResource = GetResource(write);
				if (pResource->pFirstPassUsage == nullptr)
				{
					pResource->pFirstPassUsage = pPass;
				}
				pResource->pLastPassUsage = pPass;
			}
		}

		for (VirtualResourceBase* pResource : m_Resources)
		{
			if (pResource->m_References > 0)
			{
				RG_ASSERT(!pResource->pFirstPassUsage == !pResource->pLastPassUsage, "A resource's usage should have been calculated in a pair (begin/end usage)");
				if (pResource->pFirstPassUsage && pResource->pLastPassUsage)
				{
					pResource->pFirstPassUsage->m_ResourcesToCreate.push_back(pResource);
					pResource->pLastPassUsage->m_ResourcesToDestroy.push_back(pResource);
				}
			}
		}
	}

	int64 RenderGraph::Execute(Graphics* pGraphics)
	{
		CommandContext* pContext = pGraphics->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
		for (RenderPassBase* pPass : m_RenderPasses)
		{
			if (pPass->m_References > 0)
			{
				PrepareResources(pPass, m_pAllocator);

				RenderPassResources resources(*this, *pPass);

				//#todo: Automatically insert resource barriers
				//#todo: Check if we're in a Graphics pass and automatically call BeginRenderPass

				GPU_PROFILE_BEGIN(pPass->m_Name, *pContext);
				pPass->Execute(resources, *pContext);
				GPU_PROFILE_END(*pContext);

				//#todo: Check if we're in a Graphics pass and automatically call EndRenderPass

				ReleaseResources(pPass, m_pAllocator);
			}
		}
		//#todo: This shouldn't be called if we want to dump a graph
		//DestroyData();
		return pContext->Execute(false);
	}

	void RenderGraph::Present(ResourceHandle resource)
	{
		RG_ASSERT(IsValidHandle(resource), "Resource is invalid");
		struct Empty {};
		AddCallbackPass<Empty>("Present",
			[&](RenderPassBuilder& builder, Empty&) {
				builder.Read(resource);
				builder.NeverCull();
			},
			[=](CommandContext&, const RenderPassResources&, const Empty&) {
			});
	}

	ResourceHandle RenderGraph::MoveResource(ResourceHandle From, ResourceHandle To)
	{
		RG_ASSERT(IsValidHandle(To), "Resource is invalid");
		const ResourceNode& node = GetResourceNode(From);
		m_Aliases.push_back(ResourceAlias{ From, To });
		++node.m_pResource->m_Version;
		return CreateResourceNode(node.m_pResource);
	}

	void RenderGraph::PrepareResources(RenderPassBase* pPass, ResourceAllocator* pAllocator)
	{
		for (VirtualResourceBase* pResource : pPass->m_ResourcesToCreate)
		{
			if (pResource->m_IsImported)
			{
				continue;
			}
			switch (pResource->m_Type)
			{
			case ResourceType::Texture:
				pResource->m_pPhysicalResource = pAllocator->CreateTexture(static_cast<TextureResource*>(pResource)->GetDesc());
				break;
			case ResourceType::Buffer:
				break;
			case ResourceType::None:
			default:
				assert(0);
				break;
			}
		}
	}

	void RenderGraph::ReleaseResources(RenderPassBase* pPass, ResourceAllocator* pAllocator)
	{
		for (VirtualResourceBase* pResource : pPass->m_ResourcesToDestroy)
		{
			if (pResource->m_IsImported)
			{
				continue;
			}
			switch (pResource->m_Type)
			{
			case ResourceType::Texture:
				pAllocator->ReleaseTexture(static_cast<Texture*>(pResource->m_pPhysicalResource));
				break;
			case ResourceType::Buffer:
				break;
			case ResourceType::None:
			default:
				assert(0);
				break;
			}
		}
	}

	void RenderGraph::DestroyData()
	{
		for (RenderPassBase* pPass : m_RenderPasses)
		{
			delete pPass;
		}
		m_RenderPasses.clear();
		for (VirtualResourceBase* pResource : m_Resources)
		{
			delete pResource;
		}
		m_Resources.clear();
		m_ResourceNodes.clear();
		m_Aliases.clear();
	}
}