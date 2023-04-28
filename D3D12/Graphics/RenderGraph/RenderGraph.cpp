#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/Profiler.h"

RGPass& RGPass::Read(Span<RGResource*> resources)
{
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
	if (EnumHasAnyFlags(Flags, RGPassFlag::Copy))
		state = D3D12_RESOURCE_STATE_COPY_SOURCE;

	for (RGResource* pResource : resources)
	{
		if (pResource)
		{
			bool isIndirectArgs = pResource->Type == RGResourceType::Buffer && EnumHasAllFlags(static_cast<RGBuffer*>(pResource)->GetDesc().Usage, BufferFlag::IndirectArguments);
			AddAccess(pResource, isIndirectArgs ? D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT : state);
		}
	}
	return *this;
}

RGPass& RGPass::Write(Span<RGResource*> resources)
{
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (EnumHasAnyFlags(Flags, RGPassFlag::Copy))
		state = D3D12_RESOURCE_STATE_COPY_DEST;

	for (RGResource* pResource : resources)
	{
		if (pResource)
			AddAccess(pResource, state);
	}

	return *this;
}

RGPass& RGPass::RenderTarget(RGTexture* pResource, RenderTargetLoadAction loadAccess, RGTexture* pResolveTarget)
{
	check(EnumHasAllFlags(Flags, RGPassFlag::Raster));
	AddAccess(pResource, D3D12_RESOURCE_STATE_RENDER_TARGET);
	if(pResolveTarget && pResolveTarget != pResource)
		AddAccess(pResolveTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);

	RenderTargets.push_back({ pResource, loadAccess, pResolveTarget });
	return *this;
}

RGPass& RGPass::DepthStencil(RGTexture* pResource, RenderTargetLoadAction depthAccess, bool writeDepth, RenderTargetLoadAction stencilAccess)
{
	check(EnumHasAllFlags(Flags, RGPassFlag::Raster));
	checkf(!DepthStencilTarget.pResource, "Depth Target already assigned");
	AddAccess(pResource, writeDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_DEPTH_READ);
	DepthStencilTarget = { pResource, depthAccess, stencilAccess, writeDepth };
	return *this;
}

void RGPass::AddAccess(RGResource* pResource, D3D12_RESOURCE_STATES state)
{
	check(pResource);
	auto it = std::find_if(Accesses.begin(), Accesses.end(), [=](const ResourceAccess& access) { return pResource == access.pResource; });
	if (it != Accesses.end())
	{
		checkf(!EnumHasAllFlags(it->Access, state), "Redundant state set on resource '%s'", pResource->GetName());
		checkf(!ResourceState::HasWriteResourceState(it->Access) || !ResourceState::HasWriteResourceState(state), "Resource (%s) may only have 1 write state", pResource->GetName());
		it->Access |= state;
	}
	else
	{
		Accesses.push_back({ pResource, state });
	}
}

RGGraph::RGGraph(RGResourcePool& resourcePool, uint64 allocatorSize /*= 0xFFFF*/)
	: m_Allocator(allocatorSize), m_ResourcePool(resourcePool)
{
}

RGGraph::~RGGraph()
{
	DestroyData();
}

void RGGraph::Compile()
{
	constexpr bool PassCulling = false;

	if (PassCulling)
	{
		auto WritesTo = [&](RGResource* pResource, RGPass* pPass)
		{
			for (RGPass::ResourceAccess& access : pPass->Accesses)
			{
				if (access.pResource == pResource && ResourceState::HasWriteResourceState(access.Access))
					return true;
			}
			return false;
		};

		std::vector<RGPass*> cullStack;
		for (RGPass* pPass : m_RenderPasses)
		{
			// If the pass should never cull or the pass writes to an imported resource, we add it on the stack
			if (EnumHasAllFlags(pPass->Flags, RGPassFlag::NeverCull))
			{
				cullStack.push_back(pPass);
			}
			else
			{
				for (RGPass::ResourceAccess access : pPass->Accesses)
				{
					if (access.pResource->IsImported && ResourceState::HasWriteResourceState(access.Access))
					{
						cullStack.push_back(pPass);
						break;
					}
				}
			}

			// Collect all passes that write to a resource which this pass accesses
			for (RGPass::ResourceAccess access : pPass->Accesses)
			{
				for (RGPass* pIterPass : m_RenderPasses)
				{
					if (pIterPass != pPass && WritesTo(access.pResource, pIterPass))
					{
						if (std::find(pPass->PassDependencies.begin(), pPass->PassDependencies.end(), pIterPass) == pPass->PassDependencies.end())
							pPass->PassDependencies.push_back(pIterPass);
					}
				}
			}
		}

		while (!cullStack.empty())
		{
			RGPass* pPass = cullStack.back();
			cullStack.pop_back();
			if (pPass->IsCulled)
			{
				cullStack.insert(cullStack.end(), pPass->PassDependencies.begin(), pPass->PassDependencies.end());
				pPass->IsCulled = false;
			}
		}
	}
	else
	{
		for (RGPass* pPass : m_RenderPasses)
			pPass->IsCulled = false;
	}

	// Tell the resources when they're first/last accessed and apply usage flags
	for (const RGPass* pPass : m_RenderPasses)
	{
		if (pPass->IsCulled)
			continue;

		for (const RGPass::ResourceAccess& access : pPass->Accesses)
		{
			RGResource* pResource = access.pResource;
			pResource->pFirstAccess = pResource->pFirstAccess ? pResource->pFirstAccess : pPass;
			pResource->pLastAccess = pPass;

			D3D12_RESOURCE_STATES state = access.Access;
			if (pResource->Type == RGResourceType::Buffer)
			{
				BufferDesc& desc = static_cast<RGBuffer*>(pResource)->Desc;
				if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
					desc.Usage |= BufferFlag::UnorderedAccess;
				if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE))
					desc.Usage |= BufferFlag::ShaderResource;
			}
			else if (pResource->Type == RGResourceType::Texture)
			{
				TextureDesc& desc = static_cast<RGTexture*>(pResource)->Desc;
				if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
					desc.Usage |= TextureFlag::UnorderedAccess;
				if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE))
					desc.Usage |= TextureFlag::ShaderResource;
				if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_DEPTH_WRITE))
					desc.Usage |= TextureFlag::DepthStencil;
				if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_RENDER_TARGET))
					desc.Usage |= TextureFlag::RenderTarget;
			}
		}
	}

	// Go through all resources accesses and allocate on first access and de-allocate on last access
	// It's important to make the distinction between the RefCountPtr allocation and the Raw resource itself.
	// A de-allocate returns the resource back to the pool by resetting the RefCountPtr however the Raw resource keeps a reference to it to use during execution.
	// This is how we can "alias" resources (exact match only for now) and allocate our resources during compilation so that execution is thread-safe.
	for (const RGPass* pPass : m_RenderPasses)
	{
		if (pPass->IsCulled)
			continue;

		for (const RGPass::ResourceAccess& access : pPass->Accesses)
		{
			RGResource* pResource = access.pResource;
			if (!pResource->pPhysicalResource)
			{
				if (pResource->Type == RGResourceType::Texture)
					pResource->SetResource(m_ResourcePool.Allocate(pResource->GetName(), static_cast<RGTexture*>(pResource)->GetDesc()));
				else if (pResource->Type == RGResourceType::Buffer)
					pResource->SetResource(m_ResourcePool.Allocate(pResource->GetName(), static_cast<RGBuffer*>(pResource)->GetDesc()));
				else
					noEntry();
			}
			check(pResource->pPhysicalResource);
		}

		for (const RGPass::ResourceAccess& access : pPass->Accesses)
		{
			RGResource* pResource = access.pResource;
			if (!pResource->IsImported && !pResource->IsExported && pResource->pLastAccess == pPass)
			{
				check(pResource->pPhysicalResource);
				pResource->Release();
			}
		}
	}

	// #todo Should exported resources that are not used actually be exported?
	for (RGResource* pResource : m_Resources)
	{
		if (pResource->IsExported && !pResource->pPhysicalResource)
		{
			if (pResource->Type == RGResourceType::Texture)
				pResource->SetResource(m_ResourcePool.Allocate(pResource->GetName(), static_cast<RGTexture*>(pResource)->GetDesc()));
			else if (pResource->Type == RGResourceType::Buffer)
				pResource->SetResource(m_ResourcePool.Allocate(pResource->GetName(), static_cast<RGBuffer*>(pResource)->GetDesc()));
			else
				noEntry();
		}
	}
}

void RGGraph::Export(RGTexture* pTexture, RefCountPtr<Texture>* pTarget)
{
	auto it = std::find_if(m_ExportTextures.begin(), m_ExportTextures.end(), [&](const ExportedTexture& tex) { return tex.pTarget == pTarget; });
	checkf(it == m_ExportTextures.end(), "Texture '%s' is exported to a target that has already been exported to by another texture ('%s').", pTexture->GetName(), it->pTexture->GetName());
	pTexture->IsExported = true;
	m_ExportTextures.push_back({ pTexture, pTarget });
}

void RGGraph::Export(RGBuffer* pBuffer, RefCountPtr<Buffer>* pTarget)
{
	auto it = std::find_if(m_ExportBuffers.begin(), m_ExportBuffers.end(), [&](const ExportedBuffer& buff) { return buff.pTarget == pTarget; });
	checkf(it == m_ExportBuffers.end(), "Buffer '%s' is exported to a target that has already been exported to by another texture ('%s').", pBuffer->GetName(), it->pBuffer->GetName());
	pBuffer->IsExported = true;
	m_ExportBuffers.push_back({ pBuffer, pTarget });
}

void RGGraph::PushEvent(const char* pName)
{
	m_Events.push_back(pName);
}

void RGGraph::PopEvent()
{
	if (!m_Events.empty())
		m_Events.pop_back();
	else
		++m_RenderPasses.back()->NumEventsToEnd;
}

void RGGraph::Execute(CommandContext* pContext)
{
	GPU_PROFILE_SCOPE("Render", pContext);
	{
		for (uint32 passIndex = 0; passIndex < (uint32)m_RenderPasses.size(); ++passIndex)
		{
			RGPass* pPass = m_RenderPasses[passIndex];

			if (!pPass->IsCulled)
				ExecutePass(pPass, *pContext);
		}
	}

	for (ExportedTexture& exportResource : m_ExportTextures)
	{
		check(exportResource.pTexture->pPhysicalResource);
		// Exported resources don't reduce their ref count, so release here as the refcount increases by exporting.
		exportResource.pTexture->Release();
		RefCountPtr<Texture> pTexture = exportResource.pTexture->Get();
		pTexture->SetName(exportResource.pTexture->GetName());
		*exportResource.pTarget = pTexture;
	}

	for (ExportedBuffer& exportResource : m_ExportBuffers)
	{
		check(exportResource.pBuffer->pPhysicalResource);
		// Exported resources don't reduce their ref count, so release here as the refcount increases by exporting.
		exportResource.pBuffer->Release();
		RefCountPtr<Buffer> pBuffer = exportResource.pBuffer->Get();
		pBuffer->SetName(exportResource.pBuffer->GetName());
		*exportResource.pTarget = pBuffer;
	}

	DestroyData();
}

void RGGraph::ExecutePass(RGPass* pPass, CommandContext& context)
{
	for (const std::string& event : pPass->EventsToStart)
		GPU_PROFILE_BEGIN(event.c_str(), &context);

	{
		GPU_PROFILE_SCOPE(pPass->GetName(), &context);
		PrepareResources(pPass, context);
		if (pPass->pExecuteCallback)
		{
			RGPassResources resources(*pPass);

			bool useRenderPass = EnumHasAllFlags(pPass->Flags, RGPassFlag::Raster) && !EnumHasAllFlags(pPass->Flags, RGPassFlag::NoRenderPass);

			if (useRenderPass)
				context.BeginRenderPass(resources.GetRenderPassInfo());

			pPass->pExecuteCallback->Execute(context, resources);

			if (useRenderPass)
				context.EndRenderPass();
		}
	}

	while (pPass->NumEventsToEnd--)
		GPU_PROFILE_END();
}

void RGGraph::PrepareResources(RGPass* pPass, CommandContext& context)
{
	for (const RGPass::ResourceAccess& access : pPass->Accesses)
	{
		RGResource* pResource = access.pResource;
		checkf(pResource->pPhysicalResource, "Resource was not allocated during the graph compile phase");
		checkf(pResource->IsImported || pResource->IsExported || pResource->pPhysicalResource->GetNumRefs() == 1, "If resource is not external, it's reference should be released during the graph compile phase");
		context.InsertResourceBarrier(pResource->pPhysicalResource, access.Access);
	}

	context.FlushResourceBarriers();
}

void RGGraph::DestroyData()
{
	m_RenderPasses.clear();
	m_Resources.clear();
	m_ExportTextures.clear();
	m_ExportBuffers.clear();
}

RenderPassInfo RGPassResources::GetRenderPassInfo() const
{
	RenderPassInfo passInfo;
	for (const RGPass::RenderTargetAccess& renderTarget : m_Pass.RenderTargets)
	{
		RenderPassInfo::RenderTargetInfo& targetInfo = passInfo.RenderTargets[passInfo.RenderTargetCount++];
		RenderTargetStoreAction storeAction = RenderTargetStoreAction::Store;
		if (renderTarget.pResolveTarget && renderTarget.pResource != renderTarget.pResolveTarget)
			storeAction = RenderTargetStoreAction::Resolve;

		targetInfo.Target = renderTarget.pResource->Get();
		targetInfo.Access = (RenderPassAccess)CombineRenderTargetAction(renderTarget.LoadAccess, storeAction);
		if (renderTarget.pResolveTarget)
			targetInfo.ResolveTarget = renderTarget.pResolveTarget->Get();
	}
	if (m_Pass.DepthStencilTarget.pResource)
	{
		passInfo.DepthStencilTarget.Target = m_Pass.DepthStencilTarget.pResource->Get();
		RenderTargetStoreAction depthStore = m_Pass.DepthStencilTarget.LoadAccess == RenderTargetLoadAction::NoAccess ? RenderTargetStoreAction::NoAccess : RenderTargetStoreAction::Store;
		passInfo.DepthStencilTarget.Access = (RenderPassAccess)CombineRenderTargetAction(m_Pass.DepthStencilTarget.LoadAccess, depthStore);

		RenderTargetStoreAction stencilStore = m_Pass.DepthStencilTarget.StencilLoadAccess == RenderTargetLoadAction::NoAccess ? RenderTargetStoreAction::NoAccess : RenderTargetStoreAction::Store;
		passInfo.DepthStencilTarget.StencilAccess = (RenderPassAccess)CombineRenderTargetAction(m_Pass.DepthStencilTarget.StencilLoadAccess, stencilStore);
		passInfo.DepthStencilTarget.Write = m_Pass.DepthStencilTarget.Write;
	}
	return passInfo;
}

RefCountPtr<Texture> RGResourcePool::Allocate(const char* pName, const TextureDesc& desc)
{
	for (PooledTexture& texture : m_TexturePool)
	{
		RefCountPtr<Texture>& pTexture = texture.pResource;
		if (pTexture->GetNumRefs() == 1 && pTexture->GetDesc().IsCompatible(desc))
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
		if (pBuffer->GetNumRefs() == 1 && pBuffer->GetDesc().IsCompatible(desc))
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
	constexpr uint32 numFrameRetention = 5;

	for (uint32 i = 0; i < (uint32)m_TexturePool.size();)
	{
		PooledTexture& texture = m_TexturePool[i];
		if (texture.pResource->GetNumRefs() == 1 && texture.LastUsedFrame + numFrameRetention < m_FrameIndex)
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
		if (buffer.pResource->GetNumRefs() == 1 && buffer.LastUsedFrame + numFrameRetention < m_FrameIndex)
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

namespace RGUtils
{
	RGPass& AddCopyPass(RGGraph& graph, RGResource* pSource, RGResource* pTarget)
	{
		return graph.AddPass(Sprintf("Copy [%s -> %s]", pSource->GetName(), pTarget->GetName()).c_str(), RGPassFlag::Copy)
			.Read(pSource)
			.Write(pTarget)
			.Bind([=](CommandContext& context)
				{
					context.CopyResource(pSource->GetPhysical(), pTarget->GetPhysical());
				});
	}

	RGPass& AddResolvePass(RGGraph& graph, RGTexture* pSource, RGTexture* pTarget)
	{
		return graph.AddPass(Sprintf("Resolve [%s -> %s]", pSource->GetName(), pTarget->GetName()).c_str(), RGPassFlag::Raster)
			.RenderTarget(pSource, RenderTargetLoadAction::Load, pTarget);
	}

	RGBuffer* CreatePersistent(RGGraph& graph, const char* pName, const BufferDesc& bufferDesc, RefCountPtr<Buffer>* pStorageTarget, bool doExport)
	{
		check(pStorageTarget);
		RGBuffer* pBuffer = nullptr;
		if (pStorageTarget->Get())
		{
			if (pStorageTarget->Get()->GetDesc().IsCompatible(bufferDesc))
				pBuffer = graph.Import(*pStorageTarget);
		}
		if (!pBuffer)
		{
			pBuffer = graph.Create(pName, bufferDesc);
			if(doExport)
				graph.Export(pBuffer, pStorageTarget);
		}
		return pBuffer;
	}

	RGTexture* CreatePersistent(RGGraph& graph, const char* pName, const TextureDesc& textureDesc, RefCountPtr<Texture>* pStorageTarget, bool doExport)
	{
		check(pStorageTarget);
		RGTexture* pTexture = nullptr;
		if (pStorageTarget->Get())
		{
			if (pStorageTarget->Get()->GetDesc().IsCompatible(textureDesc))
				pTexture = graph.TryImport(*pStorageTarget);
		}
		if (!pTexture)
		{
			pTexture = graph.Create(pName, textureDesc);
			if(doExport)
				graph.Export(pTexture, pStorageTarget);
		}
		return pTexture;
	}
}
