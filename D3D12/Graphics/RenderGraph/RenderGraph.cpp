#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Core/Profiler.h"
#include "Core/TaskQueue.h"

RGPass& RGPass::Read(Span<RGResource*> resources)
{
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
	if (EnumHasAnyFlags(Flags, RGPassFlag::Copy))
		state = D3D12_RESOURCE_STATE_COPY_SOURCE;

	for (RGResource* pResource : resources)
	{
		if (pResource)
		{
			bool isIndirectArgs = pResource->Type == RGResourceType::Buffer && EnumHasAllFlags(static_cast<RGBuffer*>(pResource)->GetDesc().Flags, BufferFlag::IndirectArguments);
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
	check(!DepthStencilTarget.pResource, "Depth Target already assigned");
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
		check(!EnumHasAllFlags(it->Access, state), "Redundant state set on resource '%s'", pResource->GetName());
		check(!ResourceState::HasWriteResourceState(it->Access) || !ResourceState::HasWriteResourceState(state), "Resource (%s) may only have 1 write state", pResource->GetName());
		it->Access |= state;
	}
	else
	{
		Accesses.push_back({ pResource, state });
	}
}

RGGraph::RGGraph(uint64 allocatorSize /*= 0xFFFF*/)
	: m_Allocator(allocatorSize)
{
}

RGGraph::~RGGraph()
{
	DestroyData();
}

void RGGraph::Compile(RGResourcePool& resourcePool)
{
	PROFILE_CPU_SCOPE();
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
					desc.Flags |= BufferFlag::UnorderedAccess;
				if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE))
					desc.Flags |= BufferFlag::ShaderResource;
			}
			else if (pResource->Type == RGResourceType::Texture)
			{
				TextureDesc& desc = static_cast<RGTexture*>(pResource)->Desc;
				if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
					desc.Flags |= TextureFlag::UnorderedAccess;
				if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE))
					desc.Flags |= TextureFlag::ShaderResource;
				if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_DEPTH_WRITE))
					desc.Flags |= TextureFlag::DepthStencil;
				if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_RENDER_TARGET))
					desc.Flags |= TextureFlag::RenderTarget;
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
					pResource->SetResource(resourcePool.Allocate(pResource->GetName(), static_cast<RGTexture*>(pResource)->GetDesc()));
				else if (pResource->Type == RGResourceType::Buffer)
					pResource->SetResource(resourcePool.Allocate(pResource->GetName(), static_cast<RGBuffer*>(pResource)->GetDesc()));
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
				pResource->SetResource(resourcePool.Allocate(pResource->GetName(), static_cast<RGTexture*>(pResource)->GetDesc()));
			else if (pResource->Type == RGResourceType::Buffer)
				pResource->SetResource(resourcePool.Allocate(pResource->GetName(), static_cast<RGBuffer*>(pResource)->GetDesc()));
			else
				noEntry();
		}
	}

	// Export resources first so they can be available during pass execution.
	for (ExportedTexture& exportResource : m_ExportTextures)
	{
		check(exportResource.pTexture->pPhysicalResource);
		RefCountPtr<Texture> pTexture = exportResource.pTexture->Get();
		*exportResource.pTarget = pTexture;
	}
	for (ExportedBuffer& exportResource : m_ExportBuffers)
	{
		check(exportResource.pBuffer->pPhysicalResource);
		RefCountPtr<Buffer> pBuffer = exportResource.pBuffer->Get();
		*exportResource.pTarget = pBuffer;
	}

	// Move events from passes that are culled
	std::vector<uint32> eventsToStart;
	uint32 eventsToEnd = 0;
	RGPass* pLastActivePass = nullptr;
	for (RGPass* pPass : m_RenderPasses)
	{
		if (pPass->IsCulled)
		{
			while (pPass->NumEventsToEnd > 0 && pPass->EventsToStart.size() > 0)
			{
				--pPass->NumEventsToEnd;
				pPass->EventsToStart.pop_back();
			}
			for (uint32 eventIndex : pPass->EventsToStart)
				eventsToStart.push_back(eventIndex);
			eventsToEnd += pPass->NumEventsToEnd;
		}
		else
		{
			for (uint32 eventIndex : eventsToStart)
				pPass->EventsToStart.push_back(eventIndex);
			pPass->NumEventsToEnd += eventsToEnd;
			eventsToStart.clear();
			eventsToEnd = 0;
			pLastActivePass = pPass;
		}
	}
	pLastActivePass->NumEventsToEnd += eventsToEnd;
	check(eventsToStart.empty());
}

void RGGraph::Export(RGTexture* pTexture, RefCountPtr<Texture>* pTarget, TextureFlag additionalFlags)
{
	auto it = std::find_if(m_ExportTextures.begin(), m_ExportTextures.end(), [&](const ExportedTexture& tex) { return tex.pTarget == pTarget; });
	check(it == m_ExportTextures.end(), "Texture '%s' is exported to a target that has already been exported to by another texture ('%s').", pTexture->GetName(), it->pTexture->GetName());
	pTexture->IsExported = true;
	pTexture->Desc.Flags |= additionalFlags;
	m_ExportTextures.push_back({ pTexture, pTarget });
}

void RGGraph::Export(RGBuffer* pBuffer, RefCountPtr<Buffer>* pTarget, BufferFlag additionalFlags)
{
	auto it = std::find_if(m_ExportBuffers.begin(), m_ExportBuffers.end(), [&](const ExportedBuffer& buff) { return buff.pTarget == pTarget; });
	check(it == m_ExportBuffers.end(), "Buffer '%s' is exported to a target that has already been exported to by another texture ('%s').", pBuffer->GetName(), it->pBuffer->GetName());
	pBuffer->IsExported = true;
	pBuffer->Desc.Flags |= additionalFlags;
	m_ExportBuffers.push_back({ pBuffer, pTarget });
}

void RGGraph::PushEvent(const char* pName, const char* pFilePath, uint32 lineNumber)
{
	m_PendingEvents.push_back(AddEvent(pName, pFilePath, lineNumber));
}

void RGGraph::PopEvent()
{
	if (!m_PendingEvents.empty())
		m_PendingEvents.pop_back();
	else
		++m_RenderPasses.back()->NumEventsToEnd;
}

void RGGraph::Execute(RGResourcePool& resourcePool, GraphicsDevice* pDevice)
{
	PROFILE_CPU_SCOPE();

	Compile(resourcePool);

	if (m_EnableResourceTrackerView)
		DrawResourceTracker(m_EnableResourceTrackerView);
	if (m_pDumpGraphPath)
		DumpDebugGraph(m_pDumpGraphPath);

	// Group passes in jobs
	const uint32 maxPassesPerJob = 15;
	std::vector<Span<RGPass*>> passGroups;

	// Duplicate profile events that cross the border of jobs to retain event hierarchy
	uint32 firstPass = 0;
	uint32 currentGroupSize = 0;
	std::vector<uint32> activeEvents;
	RGPass* pLastPass = nullptr;

	for (uint32 passIndex = 0; passIndex < (uint32)m_RenderPasses.size(); ++passIndex)
	{
		RGPass* pPass = m_RenderPasses[passIndex];
		if (!pPass->IsCulled)
		{
			pPass->CPUEventsToStart = pPass->EventsToStart;
			pPass->NumCPUEventsToEnd = pPass->NumEventsToEnd;

			for (uint32 event : pPass->CPUEventsToStart)
				activeEvents.push_back(event);

			if (currentGroupSize == 0)
			{
				firstPass = pPass->ID;
				pPass->CPUEventsToStart = activeEvents;
			}

			for (uint32 i = 0; i < pPass->NumCPUEventsToEnd; ++i)
				activeEvents.pop_back();

			++currentGroupSize;
			if (currentGroupSize >= maxPassesPerJob)
			{
				pPass->NumCPUEventsToEnd += (uint32)activeEvents.size();
				passGroups.push_back(Span<RGPass*>(&m_RenderPasses[firstPass], passIndex - firstPass + 1));
				currentGroupSize = 0;
			}
			pLastPass = pPass;
		}
	}
	if (pLastPass->ID != firstPass)
	{
		passGroups.push_back(Span<RGPass*>(&m_RenderPasses[firstPass], pLastPass->ID - firstPass + 1));
		pLastPass->NumCPUEventsToEnd += (uint32)activeEvents.size();
	}

	std::vector<CommandContext*> contexts;
	TaskContext context;

	{
		PROFILE_CPU_SCOPE("Schedule Render Jobs");
		for (Span<RGPass*> passGroup : passGroups)
		{
			CommandContext* pContext = pDevice->AllocateCommandContext();
			TaskQueue::Execute([this, passGroup, pContext](int)
				{
					for (RGPass* pPass : passGroup)
					{
						if (!pPass->IsCulled)
							ExecutePass(pPass, *pContext);
					}
				}, context);
			contexts.push_back(pContext);
		}
	}

	{
		PROFILE_CPU_SCOPE("Wait Render Jobs");
		TaskQueue::Join(context);
	}

	{
		PROFILE_CPU_SCOPE("ExecuteCommandLists");
		CommandContext::Execute(contexts);
	}

	// Update exported resource names
	for (ExportedTexture& exportResource : m_ExportTextures)
		exportResource.pTexture->pPhysicalResource->SetName(exportResource.pTexture->GetName());
	for (ExportedBuffer& exportResource : m_ExportBuffers)
		exportResource.pBuffer->pPhysicalResource->SetName(exportResource.pBuffer->GetName());

	DestroyData();
}

void RGGraph::ExecutePass(RGPass* pPass, CommandContext& context)
{
	for (uint32 eventIndex : pPass->EventsToStart)
	{
		const RGEvent& event = m_Events[eventIndex];
		gGPUProfiler.BeginEvent(context.GetCommandList(), event.pName, event.pFilePath, event.LineNumber);
	}
	for (uint32 eventIndex : pPass->CPUEventsToStart)
	{
		const RGEvent& event = m_Events[eventIndex];
		gCPUProfiler.BeginEvent(event.pName, event.pFilePath, event.LineNumber);
	}

	{
		PROFILE_GPU_SCOPE(context.GetCommandList(), pPass->GetName());
		PROFILE_CPU_SCOPE(pPass->GetName());

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

	for(uint32 i = 0; i < pPass->NumEventsToEnd; ++i)
		gGPUProfiler.EndEvent(context.GetCommandList());
	for (uint32 i = 0; i < pPass->NumCPUEventsToEnd; ++i)
		gCPUProfiler.EndEvent();
}

void RGGraph::PrepareResources(RGPass* pPass, CommandContext& context)
{
	for (const RGPass::ResourceAccess& access : pPass->Accesses)
	{
		RGResource* pResource = access.pResource;
		check(pResource->pPhysicalResource, "Resource was not allocated during the graph compile phase");
		check(pResource->IsImported || pResource->IsExported || !pResource->pResourceReference, "If resource is not external, it's reference should be released during the graph compile phase");
		if(pResource->GetPhysical()->UseStateTracking())
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
