#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/Profiler.h"
#include "Core/CommandLine.h"

RGPass& RGPass::Read(Span<RGHandleT> resources)
{
	RGResourceAccess flags = RGResourceAccess::None;
	if (EnumHasAnyFlags(Flags, RGPassFlag::Raster | RGPassFlag::Compute))
		flags |= RGResourceAccess::SRV;

	Read(resources, flags);
	return *this;
}

RGPass& RGPass::Write(Span<RGHandleT*> resources)
{
	RGResourceAccess flags = RGResourceAccess::None;
	if (EnumHasAnyFlags(Flags, RGPassFlag::Raster | RGPassFlag::Compute))
		flags |= RGResourceAccess::UAV;
	Write(resources, flags);
	return *this;
}

RGPass& RGPass::ReadWrite(Span<RGHandleT*> resources)
{
	check(EnumHasAnyFlags(Flags, RGPassFlag::Raster | RGPassFlag::Compute));
	for (RGHandleT* pHandle : resources)
	{
		Read(*pHandle, RGResourceAccess::UAV);
	}
	Write(resources, RGResourceAccess::UAV);
	return *this;
}

void IsReadWrite(RenderPassAccess access, bool& isRead, bool& isWrite)
{
	RenderTargetLoadAction loadAction = RenderPassInfo::GetBeginAccess(access);
	isRead |= loadAction == RenderTargetLoadAction::Load;
	RenderTargetStoreAction storeAction = RenderPassInfo::GetEndAccess(access);
	isWrite |= storeAction == RenderTargetStoreAction::Store;
}

RGPass& RGPass::RenderTarget(RGHandle<Texture>& resource, RenderPassAccess access)
{
	bool read = false;
	bool write = false;
	IsReadWrite(access, read, write);

	if (read)
		Read(resource, RGResourceAccess::RenderTarget);
	if (write)
		Write(&resource, RGResourceAccess::RenderTarget);

	RenderTargets.push_back({ resource, access });
	return *this;
}

RGPass& RGPass::DepthStencil(RGHandle<Texture>& resource, RenderPassAccess depthAccess, bool writeDepth, RenderPassAccess stencilAccess)
{
	checkf(!DepthStencilTarget.Resource.IsValid(), "Depth Target already assigned");

	bool read = false;
	bool write = false;
	IsReadWrite(depthAccess, read, write);
	IsReadWrite(stencilAccess, read, write);

	if (read)
		Read(resource, RGResourceAccess::Depth);
	if (write && writeDepth)
		Write(&resource, RGResourceAccess::Depth);

	DepthStencilTarget = { resource, depthAccess, stencilAccess, writeDepth };
	return *this;
}

void RGPass::Read(Span<RGHandleT> resources, RGResourceAccess useFlag)
{
	for (RGHandleT resource : resources)
	{
		RGNode& node = Graph.GetResourceNode(resource);
		node.UseFlags |= useFlag;
		checkf(ReadsFrom(resource) == false, "Pass already reads from this resource");
		Reads.push_back(resource);
	}
}

void RGPass::Write(Span<RGHandleT*> resources, RGResourceAccess useFlag)
{
	for (RGHandleT* pHandle : resources)
	{
		checkf(WritesTo(*pHandle) == false, "Pass already writes to this resource");
		const RGNode& node = Graph.GetResourceNode(*pHandle);
		checkf(node.pResource->Version == node.Version, "Version mismatch");
		++node.pResource->Version;
		if (node.pResource->IsImported)
		{
			Flags |= RGPassFlag::NeverCull;
		}
		
		*pHandle = Graph.CreateResourceNode(node.pResource);
		RGNode& newNode = Graph.GetResourceNode(*pHandle);
		newNode.UseFlags |= useFlag;
		Writes.push_back(*pHandle);
	}
}

RGPass& RGGraph::AddCopyPass(const char* pName, RGHandleT source, RGHandleT& target)
{
	return AddPass(pName, RGPassFlag::Copy)
		.Read(source)
		.Write(&target)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.CopyResource(resources.Get(source), resources.Get(target));
			});
}

RGGraph::RGGraph(GraphicsDevice* pDevice, RGResourcePool& resourcePool, uint64 allocatorSize /*= 0xFFFF*/)
	: m_pDevice(pDevice), m_Allocator(allocatorSize), m_ResourcePool(resourcePool)
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
		pPass->Flags |= RGPassFlag::NeverCull;
#endif
		pPass->References = (int)pPass->Writes.size() + (int)EnumHasAllFlags(pPass->Flags, RGPassFlag::NeverCull);

		for (RGHandleT read : pPass->Reads)
		{
			RGNode& node = m_ResourceNodes[read.Index];
			node.Reads++;
		}

		for (RGHandleT write : pPass->Writes)
		{
			RGNode& node = m_ResourceNodes[write.Index];
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
			checkf(pWriter->References >= 1, "Pass (%s) is expected to have references", pWriter->Name);
			--pWriter->References;
			if (pWriter->References == 0)
			{
				for (RGHandleT resource : pWriter->Reads)
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

	// Tell the resources when they're first/last accessed
	for (RGPass* pPass : m_RenderPasses)
	{
		if (pPass->References > 0)
		{
			for (RGHandleT read : pPass->Reads)
			{
				RGNode& node = m_ResourceNodes[read.Index];
				node.pResource->pFirstAccess = node.pResource->pFirstAccess ? node.pResource->pFirstAccess : pPass;
				node.pResource->pLastAccess = pPass;
			}

			for (RGHandleT read : pPass->Writes)
			{
				RGNode& node = m_ResourceNodes[read.Index];
				node.pResource->pFirstAccess = node.pResource->pFirstAccess ? node.pResource->pFirstAccess : pPass;
				node.pResource->pLastAccess = pPass;
			}
		}
	}

	for (ExportedResource<Texture>& exportResource : m_ExportTextures)
	{
		RGResource* pResource = GetResource(exportResource.Handle);
		// Don't release resource if exported
		pResource->pLastAccess = nullptr;
	}
	for (ExportedResource<Buffer>& exportResource : m_ExportBuffers)
	{
		RGResource* pResource = GetResource(exportResource.Handle);
		// Don't release resource if exported
		pResource->pLastAccess = nullptr;
	}


	//Set the final reference count and propagate use flags
	for (RGNode& node : m_ResourceNodes)
	{
		RGResource* pResource = node.pResource;
		pResource->References += node.Reads;

		if (node.Reads > 0)
		{
			if (pResource->Type == RGResourceType::Texture)
			{
				TextureFlag& textureFlags = pResource->TextureDesc.Usage;
				if (EnumHasAnyFlags(node.UseFlags, RGResourceAccess::RenderTarget))
					textureFlags |= TextureFlag::RenderTarget;
				if (EnumHasAnyFlags(node.UseFlags, RGResourceAccess::Depth))
					textureFlags |= TextureFlag::DepthStencil;
				if (EnumHasAnyFlags(node.UseFlags, RGResourceAccess::UAV))
					textureFlags |= TextureFlag::UnorderedAccess;
				if (EnumHasAnyFlags(node.UseFlags, RGResourceAccess::SRV))
					textureFlags |= TextureFlag::ShaderResource;
			}
			else if (pResource->Type == RGResourceType::Buffer)
			{
				BufferFlag& bufferFlags = pResource->BufferDesc.Usage;
				if (EnumHasAnyFlags(node.UseFlags, RGResourceAccess::UAV))
					bufferFlags |= BufferFlag::UnorderedAccess;
				if (EnumHasAnyFlags(node.UseFlags, RGResourceAccess::SRV))
					bufferFlags |= BufferFlag::ShaderResource;
			}
		}
	}
}

RGHandleT RGGraph::MoveResource(RGHandleT From, RGHandleT To)
{
	checkf(IsValidHandle(To), "Resource is invalid");
	const RGNode& node = GetResourceNode(From);
	m_Aliases.push_back(RGResourceAlias{ From, To });
	++node.pResource->Version;
	return CreateResourceNode(node.pResource);
}

void RGGraph::PushEvent(const char* pName)
{
	std::string name = pName;
	AddPass("EventPass", RGPassFlag::Invisible | RGPassFlag::NeverCull)
		.Bind([name](CommandContext& context, const RGPassResources& resources)
			{
				GPU_PROFILE_BEGIN(name.c_str(), &context);
			});
}

void RGGraph::PopEvent()
{
	AddPass("EventPass", RGPassFlag::Invisible | RGPassFlag::NeverCull)
		.Bind([](CommandContext& context, const RGPassResources& resources)
			{
				GPU_PROFILE_END();
			});
}

SyncPoint RGGraph::Execute()
{
	CommandContext* pContext = m_pDevice->AllocateCommandContext(D3D12_COMMAND_LIST_TYPE_DIRECT);
	for (uint32 passIndex = 0; passIndex < (uint32)m_RenderPasses.size(); ++passIndex)
	{
		RGPass* pPass = m_RenderPasses[passIndex];

		if (pPass->References > 0)
		{
			ExecutePass(pPass, *pContext);
		}
	}
	m_LastSyncPoint = pContext->Execute(false);

	for (ExportedResource<Texture>& exportResource : m_ExportTextures)
	{
		*exportResource.pTarget = GetResource(exportResource.Handle)->GetRHI<Texture>();
	}

	for (ExportedResource<Buffer>& exportResource : m_ExportBuffers)
	{
		*exportResource.pTarget = GetResource(exportResource.Handle)->GetRHI<Buffer>();
	}

	DestroyData();
	return m_LastSyncPoint;
}

void RGGraph::ExecutePass(RGPass* pPass, CommandContext& context)
{
	PrepareResources(pPass, context);

	if(pPass->ExecuteCallback.IsBound())
	{
		GPU_PROFILE_SCOPE_CONDITIONAL(pPass->Name, &context, !EnumHasAnyFlags(pPass->Flags, RGPassFlag::Invisible));
		RGPassResources resources(*this, *pPass);
		pPass->ExecuteCallback.Execute(context, resources);
	}

	ReleaseResources(pPass);
}

void RGGraph::PrepareResources(RGPass* pPass, CommandContext& context)
{
	auto IsRenderTarget = [&](const RGResource* pResource)
	{
		if (pResource->Type == RGResourceType::Texture)
		{
			for (RGPass::RenderTargetAccess& rt : pPass->RenderTargets)
			{
				if (GetResourceNode(rt.Resource).pResource == pResource)
					return true;
			}
		}
		return false;
	};

	auto IsDepthStencil = [&](const RGResource* pResource)
	{
		if(pResource->Type == RGResourceType::Texture && pPass->DepthStencilTarget.Resource.IsValid())
			return GetResourceNode(pPass->DepthStencilTarget.Resource).pResource == pResource;
		return false;
	};

	auto WritesTo = [&](const RGResource* pResource)
	{
		for (RGHandleT write : pPass->Writes)
		{
			if (pResource == GetResourceNode(write).pResource)
				return true;
		}
		return false;
	};

	auto ConditionallyCreateResource = [&](RGResource* pResource) {
		if (!pResource->IsImported && pResource->pFirstAccess == pPass && pResource->pPhysicalResource == nullptr)
		{
			if (pResource->Type == RGResourceType::Texture)
			{
				pResource->pPhysicalResource = m_ResourcePool.Allocate(pResource->Name, pResource->TextureDesc);
			}
			else if (pResource->Type == RGResourceType::Buffer)
			{
				pResource->pPhysicalResource = m_ResourcePool.Allocate(pResource->Name, pResource->BufferDesc);
			}
		}
		check(pResource->pPhysicalResource);
	};

	D3D12_RESOURCE_STATES readState = D3D12_RESOURCE_STATE_COMMON;
	D3D12_RESOURCE_STATES writeState = D3D12_RESOURCE_STATE_COMMON;
	bool isWriteAllowed = false;
	if (EnumHasAnyFlags(pPass->Flags, RGPassFlag::Raster))
	{
		readState |= D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
		writeState |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	if (EnumHasAnyFlags(pPass->Flags, RGPassFlag::Compute))
	{
		readState |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		writeState |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	if (EnumHasAnyFlags(pPass->Flags, RGPassFlag::Copy))
	{
		isWriteAllowed = EnumHasAnyFlags(pPass->Flags, RGPassFlag::Compute | RGPassFlag::Raster);
		readState |= D3D12_RESOURCE_STATE_COPY_SOURCE;
		writeState |= D3D12_RESOURCE_STATE_COPY_DEST;
	}

	bool writesDepth = pPass->DepthStencilTarget.Write;

	for (RGHandleT& handle : pPass->Writes)
	{
		RGResource* pResource = GetResource(handle);
		ConditionallyCreateResource(pResource);

		if (IsDepthStencil(pResource))
		{
			if(writesDepth)
				context.InsertResourceBarrier(pResource->pPhysicalResource, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		}
		else if (IsRenderTarget(pResource))
		{
			context.InsertResourceBarrier(pResource->pPhysicalResource, D3D12_RESOURCE_STATE_RENDER_TARGET);
		}
		else if(writeState != D3D12_RESOURCE_STATE_COMMON)
		{
			check(!isWriteAllowed);
			context.InsertResourceBarrier(pResource->pPhysicalResource, writeState);
		}
	}

	for (RGHandleT& handle : pPass->Reads)
	{
		RGResource* pResource = GetResource(handle);
		ConditionallyCreateResource(pResource);
		check(pResource->pPhysicalResource);

		if (IsDepthStencil(pResource))
		{
			if (!writesDepth)
				context.InsertResourceBarrier(pResource->pPhysicalResource, D3D12_RESOURCE_STATE_DEPTH_READ);
		}
		else if (IsRenderTarget(pResource))
		{
			// Do nothing
		}
		else if (readState != D3D12_RESOURCE_STATE_COMMON && !WritesTo(pResource))
		{
			context.InsertResourceBarrier(pResource->pPhysicalResource, readState);
		}
	}

	context.FlushResourceBarriers();
}

void RGGraph::ReleaseResources(RGPass* pPass)
{
	auto ConditionallyReleaseResource = [&](RGResource* pResource) {
		if (!pResource->IsImported && pResource->pLastAccess == pPass)
		{
			check(pResource->pPhysicalResource);
			pResource->pPhysicalResource = nullptr;
		}
	};

	for (RGHandleT& handle : pPass->Reads)
	{
		RGResource* pResource = GetResource(handle);
		ConditionallyReleaseResource(pResource);
	}
}

void RGGraph::DestroyData()
{
	m_RenderPasses.clear();
	m_Resources.clear();
	m_ResourceNodes.clear();
	m_Aliases.clear();
}

RenderPassInfo RGPassResources::GetRenderPassInfo() const
{
	RenderPassInfo passInfo;
	for (RGPass::RenderTargetAccess renderTarget : m_Pass.RenderTargets)
	{
		passInfo.RenderTargets[passInfo.RenderTargetCount].Target = Get<Texture>(renderTarget.Resource);
		passInfo.RenderTargets[passInfo.RenderTargetCount].Access = renderTarget.Access;
		passInfo.RenderTargetCount++;
	}
	if (m_Pass.DepthStencilTarget.Resource.IsValid())
	{
		passInfo.DepthStencilTarget.Target = Get<Texture>(m_Pass.DepthStencilTarget.Resource);
		passInfo.DepthStencilTarget.Access = m_Pass.DepthStencilTarget.Access;
		passInfo.DepthStencilTarget.StencilAccess = m_Pass.DepthStencilTarget.StencilAccess;
		passInfo.DepthStencilTarget.Write = m_Pass.DepthStencilTarget.Write;
	}
	return passInfo;
}

const RGResource* RGPassResources::GetResource(RGHandleT handle) const
{
	checkf(m_Pass.ReadsFrom(handle) || m_Pass.WritesTo(handle), "Resource is not accessed by this pass");
	return m_Graph.GetResource(handle);
}

RefCountPtr<Texture> RGResourcePool::Allocate(const char* pName, const TextureDesc& desc)
{
	for (PooledTexture& texture : m_TexturePool)
	{
		RefCountPtr<Texture>& pTexture = texture.pResource;
		if (pTexture->GetNumRefs() == 1 && pTexture->GetDesc() == desc)
		{
			texture.LastUsedFrame = m_FrameIndex;
			pTexture->SetName(pName);
			return pTexture;
		}
	}
	return m_TexturePool.emplace_back(PooledTexture{ GetParent()->CreateTexture(desc, pName), m_FrameIndex }).pResource;
}

RefCountPtr<Buffer> RGResourcePool::Allocate(const char* pName, const BufferDesc& desc)
{
	for (PooledBuffer& buffer : m_BufferPool)
	{
		RefCountPtr<Buffer>& pBuffer = buffer.pResource;
		if (pBuffer->GetNumRefs() == 1 && pBuffer->GetDesc() == desc)
		{
			buffer.LastUsedFrame = m_FrameIndex;
			pBuffer->SetName(pName);
			return pBuffer;
		}
	}
	return m_BufferPool.emplace_back(PooledBuffer{ GetParent()->CreateBuffer(desc, pName), m_FrameIndex }).pResource;
}

void RGResourcePool::Tick()
{
	for (uint32 i = 0; i < (uint32)m_TexturePool.size();)
	{
		PooledTexture& texture = m_TexturePool[i];
		if (texture.pResource->GetNumRefs() == 1 && texture.LastUsedFrame + 5 < m_FrameIndex)
		{
			std::swap(m_TexturePool[i], m_TexturePool.back());
			m_TexturePool.pop_back();
		}
		else
		{
			++i;
		}
	}
	for (uint32 i = 0; i < (uint32)m_BufferPool.size();)
	{
		PooledBuffer& buffer = m_BufferPool[i];
		if (buffer.pResource->GetNumRefs() == 1 && buffer.LastUsedFrame + 5 < m_FrameIndex)
		{
			std::swap(m_BufferPool[i], m_BufferPool.back());
			m_BufferPool.pop_back();
		}
		else
		{
			++i;
		}
	}
	++m_FrameIndex;
}
