#include "stdafx.h"
#include "RenderGraph.h"
#include "Core/Profiler.h"
#include "Core/TaskQueue.h"
#include "RHI/Device.h"
#include "RHI/CommandContext.h"
#include "RHI/CommandQueue.h"
#include "RenderGraph/RenderGraphAllocator.h"

#define RG_TRACK_TRANSITIONS 0

#if RG_TRACK_TRANSITIONS
constexpr static const char* pLogResourceName = "Depth Stencil";
constexpr static const char* pLogPassName = "";

static bool sDoLogTransition(const RGPass* pPass, const RGResource* pResource)
{
	return (strlen(pLogPassName) == 0 || strcmp(pLogPassName, pPass->GetName()) == 0) && (strlen(pLogResourceName) == 0 || strcmp(pLogResourceName, pResource->GetName()) == 0);
}

#define RG_LOG_RESOURCE_EVENT(fmt, ...) if (sDoLogTransition(pPass, pResource)) E_LOG(Warning, "[%s:%s] " fmt, pPass->GetName(), pResource->GetName(), __VA_ARGS__)
#else
#define RG_LOG_RESOURCE_EVENT(fmt, ...) do { UNUSED_VAR(pResource); UNUSED_VAR(pPass); } while (0)
#endif

RGPass& RGPass::Read(Span<RGResource*> resources)
{
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
	if (EnumHasAnyFlags(Flags, RGPassFlag::Copy))
		state = D3D12_RESOURCE_STATE_COPY_SOURCE;

	for (RGResource* pResource : resources)
	{
		if (pResource)
		{
			D3D12_RESOURCE_STATES resourceState = state;
			if (pResource->GetType() == RGResourceType::Buffer && EnumHasAllFlags(static_cast<RGBuffer*>(pResource)->GetDesc().Flags, BufferFlag::IndirectArguments))
				resourceState |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

			AddAccess(pResource, resourceState);
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

RGPass& RGPass::RenderTarget(RGTexture* pResource, RenderPassColorFlags flags, RGTexture* pResolveTarget)
{
	gAssert(EnumHasAllFlags(Flags, RGPassFlag::Raster));
	AddAccess(pResource, D3D12_RESOURCE_STATE_RENDER_TARGET);
	if(pResolveTarget && pResolveTarget != pResource)
		AddAccess(pResolveTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);

	RenderTargets.push_back({ pResource, flags, pResolveTarget });
	return *this;
}

RGPass& RGPass::DepthStencil(RGTexture* pResource, RenderPassDepthFlags flags)
{
	gAssert(EnumHasAllFlags(Flags, RGPassFlag::Raster));
	gAssert(!DepthStencilTarget.pResource, "Depth Target already assigned");
	AddAccess(pResource, EnumHasAllFlags(flags, RenderPassDepthFlags::ReadOnly) ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE);
	DepthStencilTarget = { pResource, flags };
	return *this;
}

void RGPass::AddAccess(RGResource* pResource, D3D12_RESOURCE_STATES state)
{
	gAssert(pResource);
	auto it = std::find_if(Accesses.begin(), Accesses.end(), [=](const ResourceAccess& access) { return pResource == access.pResource; });
	if (it != Accesses.end())
	{
		if (EnumHasAllFlags(it->Access, state))
			return;

		gAssert(it->Access == state || !D3D::HasWriteResourceState(it->Access), "Resource '%s' may not have any other states when it already has a write state (%s)", pResource->GetName(), D3D::ResourceStateToString(it->Access));
		gAssert(it->Access == state || !D3D::HasWriteResourceState(state), "Resource '%s' may not use a write state (%s) while it already has another state (%s)", pResource->GetName(), D3D::ResourceStateToString(state), D3D::ResourceStateToString(it->Access));
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

void RGGraph::Compile(RGResourceAllocator& resourceAllocator, const RGGraphOptions& options)
{
	PROFILE_CPU_SCOPE();

	gAssert(!m_IsCompiled);

	m_Options = options;

	if (options.PassCulling)
	{
		PROFILE_CPU_SCOPE("Pass Culling");

		Array<RGPassID> cullStack;
		cullStack.reserve(m_Passes.size());

		for (RGPass* pPass : m_Passes)
		{
			for (const RGPass::ResourceAccess& access : pPass->Accesses)
			{
				// Add a pass dependency to the last pass that wrote to this resource
				if (access.pResource->LastWrite.IsValid())
				{
					if (std::find_if(pPass->PassDependencies.begin(), pPass->PassDependencies.end(), [&](RGPassID id) { return access.pResource->LastWrite == id; }) == pPass->PassDependencies.end())
						pPass->PassDependencies.push_back(access.pResource->LastWrite);
				}

				// If the resource is written to in this pass, update the LastWrite pass
				if (D3D::HasWriteResourceState(access.Access))
					access.pResource->LastWrite = pPass->ID;
			}

			// If pass is marked for never cull, immediately add it to the stack
			if (EnumHasAllFlags(pPass->Flags, RGPassFlag::NeverCull))
				cullStack.push_back(pPass->ID);
		}

		for (RGResource* pResource : m_Resources)
		{
			if (pResource->LastWrite.IsValid() && (pResource->IsExported || pResource->IsImported))
				cullStack.push_back(pResource->LastWrite);
		}

		while (!cullStack.empty())
		{
			RGPassID pass = cullStack.back();
			cullStack.pop_back();

			RGPass* pPass = m_Passes[pass.GetIndex()];
			if (pPass->IsCulled)
			{
				cullStack.insert(cullStack.end(), pPass->PassDependencies.begin(), pPass->PassDependencies.end());
				pPass->IsCulled = false;
			}
		}
	}
	else
	{
		for (RGPass* pPass : m_Passes)
			pPass->IsCulled = false;
	}

	{
		PROFILE_CPU_SCOPE("Compute Resource Usage");

		// Tell the resources when they're first/last accessed and apply usage flags
		for (const RGPass* pPass : m_Passes)
		{
			if (pPass->IsCulled)
				continue;

			for (const RGPass::ResourceAccess& access : pPass->Accesses)
			{
				RGResource* pResource  = access.pResource;
				pResource->FirstAccess = pResource->FirstAccess.IsValid() ? pResource->FirstAccess : pPass->ID;
				pResource->LastAccess  = pPass->ID;

				D3D12_RESOURCE_STATES state = access.Access;
				if (pResource->GetType() == RGResourceType::Buffer)
				{
					BufferDesc& desc = static_cast<RGBuffer*>(pResource)->Desc;
					if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
						desc.Flags |= BufferFlag::UnorderedAccess;
					if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE))
						desc.Flags |= BufferFlag::ShaderResource;
				}
				else if (pResource->GetType() == RGResourceType::Texture)
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
	}

	{
		PROFILE_CPU_SCOPE("Resource Allocation");

		for (RGPass* pPass : m_Passes)
		{
			if (pPass->IsCulled)
				continue;

			for (const RGPass::ResourceAccess& access : pPass->Accesses)
			{
				// Record resource transition
				RGResource* pResource = access.pResource;

				if (pResource->FirstAccess == pPass->ID)
					resourceAllocator.AllocateResource(pResource);

				D3D12_RESOURCE_STATES afterState = access.Access;
				DeviceResource*		  pPhysical	 = pResource->GetPhysicalUnsafe();
				if (pPhysical->UseStateTracking())
				{
					uint32 subResource = 0xFFFFFFFF;
					D3D12_RESOURCE_STATES beforeState = pPhysical->GetResourceState(subResource);

					if (D3D::NeedsTransition(beforeState, afterState, true))
					{
						RG_LOG_RESOURCE_EVENT("Recorded transition from %s to %s", D3D::ResourceStateToString(beforeState), D3D::ResourceStateToString(afterState));

						pPass->Transitions.push_back({ .pResource = pResource, .BeforeState = beforeState, .AfterState = afterState, .SubResource = subResource });
						pPhysical->SetResourceState(afterState, subResource);
					}
				}

				// If the resource is not imported, it will require an aliasing barrier on the first use
				if (!pResource->IsImported && pResource->FirstAccess == pPass->ID)
				{
					RGPass::AliasBarrier barrier;
					barrier.pResource = pResource;

					// If the resource is a rendertarget/depthstencil, it will need a discard
					if (pResource->GetType() == RGResourceType::Texture)
					{
						RGTexture* pTexture = static_cast<RGTexture*>(access.pResource);
						if (EnumHasAnyFlags(pTexture->GetDesc().Flags, TextureFlag::RenderTarget | TextureFlag::DepthStencil))
						{
							barrier.NeedsDiscard	   = true;
							barrier.DiscardSourceState = afterState;

							RG_LOG_RESOURCE_EVENT("Recorder discard", pPass->pName);
						}
					}
					pPass->AliasBarriers.push_back(barrier);

					RG_LOG_RESOURCE_EVENT("Recorded aliasing barrier", pPass->pName);
				}
			}

			for (const RGPass::ResourceAccess& access : pPass->Accesses)
			{
				RGResource* pResource = access.pResource;

				if (pResource->LastAccess == pPass->ID)
					resourceAllocator.ReleaseResource(pResource);
			}
		}
	}

	resourceAllocator.DrawDebugView(URange(0, (uint32)m_Passes.size()));

	{
		PROFILE_CPU_SCOPE("Event Resolving");

		// Move events from passes that are culled
		Array<RGEventID> eventsToStart;
		uint32 eventsToEnd = 0;
		RGPass* pLastActivePass = nullptr;
		for (RGPass* pPass : m_Passes)
		{
			if (pPass->IsCulled)
			{
				while (pPass->NumEventsToEnd > 0 && pPass->EventsToStart.size() > 0)
				{
					--pPass->NumEventsToEnd;
					pPass->EventsToStart.pop_back();
				}
				for (RGEventID eventIndex : pPass->EventsToStart)
					eventsToStart.push_back(eventIndex);
				eventsToEnd += pPass->NumEventsToEnd;
			}
			else
			{
				for (RGEventID eventIndex : eventsToStart)
					pPass->EventsToStart.push_back(eventIndex);
				pPass->NumEventsToEnd += eventsToEnd;
				eventsToStart.clear();
				eventsToEnd = 0;
				pLastActivePass = pPass;
			}
		}
		if (pLastActivePass)
			pLastActivePass->NumEventsToEnd += eventsToEnd;
		gAssert(eventsToStart.empty());
	}

	{
		PROFILE_CPU_SCOPE("Pass Grouping");

		// Group passes in jobs
		const uint32 maxPassesPerJob = options.Jobify ? options.CommandlistGroupSize : 0xFFFFFFFF;

		// Duplicate profile events that cross the border of jobs to retain event hierarchy
		RGPassID firstPass;
		uint32 currentGroupSize = 0;
		Array<RGEventID> activeEvents;
		RGPass* pLastPass = nullptr;

		for (uint32 passIndex = 0; passIndex < (uint32)m_Passes.size(); ++passIndex)
		{
			RGPass* pPass = m_Passes[passIndex];
			if (!pPass->IsCulled)
			{
				pPass->CPUEventsToStart = pPass->EventsToStart;
				pPass->NumCPUEventsToEnd = pPass->NumEventsToEnd;

				for (RGEventID event : pPass->CPUEventsToStart)
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
					m_PassExecuteGroups.push_back(Span<const RGPass*>(&m_Passes[firstPass.GetIndex()], passIndex - firstPass.GetIndex() + 1));
					currentGroupSize = 0;
				}
				pLastPass = pPass;
			}
		}
		if (currentGroupSize > 0)
			m_PassExecuteGroups.push_back(Span<const RGPass*>(&m_Passes[firstPass.GetIndex()], (uint32)m_Passes.size() - firstPass.GetIndex()));
		if (pLastPass)
			pLastPass->NumCPUEventsToEnd += (uint32)activeEvents.size();
	}

	m_IsCompiled = true;
}

void RGGraph::Export(RGTexture* pTexture, Ref<Texture>* pTarget, TextureFlag additionalFlags)
{
	auto it = std::find_if(m_ExportTextures.begin(), m_ExportTextures.end(), [&](const ExportedTexture& tex) { return tex.pTarget == pTarget; });
	gAssert(it == m_ExportTextures.end(), "Texture '%s' is exported to a target that has already been exported to by another texture ('%s').", pTexture->GetName(), it->pTexture->GetName());
	pTexture->IsExported = true;
	pTexture->Desc.Flags |= additionalFlags;
	m_ExportTextures.push_back({ pTexture, pTarget });
}

void RGGraph::Export(RGBuffer* pBuffer, Ref<Buffer>* pTarget, BufferFlag additionalFlags)
{
	auto it = std::find_if(m_ExportBuffers.begin(), m_ExportBuffers.end(), [&](const ExportedBuffer& buff) { return buff.pTarget == pTarget; });
	gAssert(it == m_ExportBuffers.end(), "Buffer '%s' is exported to a target that has already been exported to by another texture ('%s').", pBuffer->GetName(), it->pBuffer->GetName());
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
		++m_Passes.back()->NumEventsToEnd;
}

void RGGraph::Execute(GraphicsDevice* pDevice)
{
	PROFILE_CPU_SCOPE();

	gAssert(m_IsCompiled);

	Array<CommandContext*> contexts;
	contexts.reserve(m_PassExecuteGroups.size());

	if (m_PassExecuteGroups.size() > 1)
	{
		TaskContext context;

		{
			PROFILE_CPU_SCOPE("Schedule Render Jobs");
			for (Span<const RGPass*> passGroup : m_PassExecuteGroups)
			{
				CommandContext* pContext  = pDevice->AllocateCommandContext();
				auto			executeFn = [this, passGroup, pContext](int) {
					   for (const RGPass* pPass : passGroup)
					   {
						   if (!pPass->IsCulled)
							   ExecutePass(pPass, *pContext);
					   };
				};
#if RG_TRACK_TRANSITIONS
				executeFn(0);
#else

				if (m_Options.SingleThread)
					executeFn(0);
				else
					TaskQueue::Execute(executeFn, context);
#endif
				contexts.push_back(pContext);
			}
		}

		{
			PROFILE_CPU_SCOPE("Wait Render Jobs");
			TaskQueue::Join(context);
		}
	}
	else
	{
		PROFILE_CPU_SCOPE("Schedule Render Jobs");

		CommandContext* pContext = pDevice->AllocateCommandContext();
		for (const RGPass* pPass : m_PassExecuteGroups[0])
		{
			if (!pPass->IsCulled)
				ExecutePass(pPass, *pContext);
		}
		contexts.push_back(pContext);
	}

	pDevice->GetGraphicsQueue()->ExecuteCommandLists(contexts);

	// Export resources at the end of execution
	for (ExportedTexture& exportResource : m_ExportTextures)
	{
		gAssert(exportResource.pTexture->GetPhysicalUnsafe(), "Exported texture doesn't have a physical resource assigned");
		Ref<Texture> pTexture = (Texture*)exportResource.pTexture->GetPhysicalUnsafe();
		pTexture->SetName(exportResource.pTexture->GetName());
		*exportResource.pTarget = pTexture;
	}
	for (ExportedBuffer& exportResource : m_ExportBuffers)
	{
		gAssert(exportResource.pBuffer->GetPhysicalUnsafe(), "Exported buffer doesn't have a physical resource assigned");
		Ref<Buffer> pBuffer = (Buffer*)exportResource.pBuffer->GetPhysicalUnsafe();
		pBuffer->SetName(exportResource.pBuffer->GetName());
		*exportResource.pTarget = pBuffer;
	}

	DestroyData();
}

void RGGraph::ExecutePass(const RGPass* pPass, CommandContext& context) const
{
	for (RGEventID eventIndex : pPass->EventsToStart)
	{
		const RGEvent& event = m_Events[eventIndex.GetIndex()];
		gGPUProfiler.BeginEvent(context.GetCommandList(), event.pName, 0, event.pFilePath, event.LineNumber);
	}
	for (RGEventID eventIndex : pPass->CPUEventsToStart)
	{
		const RGEvent& event = m_Events[eventIndex.GetIndex()];
		gCPUProfiler.BeginEvent(event.pName, 0, event.pFilePath, event.LineNumber);
	}

	{
		PROFILE_GPU_SCOPE(context.GetCommandList(), pPass->GetName());
		PROFILE_CPU_SCOPE(pPass->GetName());

		PrepareResources(pPass, context);

		if (pPass->pExecuteCallback)
		{
			RGResources resources(*pPass);

			bool useRenderPass = EnumHasAllFlags(pPass->Flags, RGPassFlag::Raster);
			if (useRenderPass)
				context.BeginRenderPass(resources.GetRenderPassInfo());

			pPass->pExecuteCallback->Execute(context, resources);

			if (useRenderPass)
				context.EndRenderPass();

#define TEST_STATE_LEAKING 0
#if TEST_STATE_LEAKING
			context.ClearState();
#endif
		}
	}

	for(uint32 i = 0; i < pPass->NumEventsToEnd; ++i)
		gGPUProfiler.EndEvent(context.GetCommandList());
	for (uint32 i = 0; i < pPass->NumCPUEventsToEnd; ++i)
		gCPUProfiler.EndEvent();
}

void RGGraph::PrepareResources(const RGPass* pPass, CommandContext& context) const
{
	PROFILE_CPU_SCOPE();

	for (const RGPass::AliasBarrier& barrier: pPass->AliasBarriers)
	{
		const RGResource* pResource = barrier.pResource;
		context.InsertAliasingBarrier(pResource->GetPhysicalUnsafe());
		RG_LOG_RESOURCE_EVENT("Executed aliasing barrier");
	}

	for (const RGPass::ResourceTransition& transition : pPass->Transitions)
	{
		RGResource* pResource = transition.pResource;

		gAssert(pResource->GetPhysicalUnsafe(), "Resource was not allocated during the graph compile phase");

		context.InsertResourceBarrier(pResource->GetPhysicalUnsafe(), transition.BeforeState, transition.AfterState, transition.SubResource);

		RG_LOG_RESOURCE_EVENT("Executed transition from %s to %s", D3D::ResourceStateToString(transition.BeforeState), D3D::ResourceStateToString(transition.AfterState));
	}

	context.FlushResourceBarriers();

	for (const RGPass::AliasBarrier& barrier : pPass->AliasBarriers)
	{
		if (barrier.NeedsDiscard)
		{
			const RGResource* pResource = barrier.pResource;

			RG_LOG_RESOURCE_EVENT("Executed discard");

			const RGTexture* pTexture = static_cast<const RGTexture*>(barrier.pResource);

			D3D12_RESOURCE_STATES state		  = barrier.DiscardSourceState;
			D3D12_RESOURCE_STATES targetState = EnumHasAllFlags(pTexture->GetDesc().Flags, TextureFlag::RenderTarget) ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_DEPTH_WRITE;

			context.InsertResourceBarrier(pTexture->GetPhysicalUnsafe(), state, targetState);
			context.FlushResourceBarriers();
			context.GetCommandList()->DiscardResource(pTexture->GetPhysicalUnsafe()->GetResource(), nullptr);
			context.InsertResourceBarrier(pTexture->GetPhysicalUnsafe(), targetState, state);
		}
	}
}

void RGGraph::DestroyData()
{
	m_Passes.clear();
	m_Resources.clear();
	m_ExportTextures.clear();
	m_ExportBuffers.clear();
}

RenderPassInfo RGResources::GetRenderPassInfo() const
{
	RenderPassInfo passInfo;
	for (const RGPass::RenderTargetAccess& renderTarget : m_Pass.RenderTargets)
	{
		RenderPassInfo::RenderTargetInfo& targetInfo = passInfo.RenderTargets[passInfo.RenderTargetCount++];
		targetInfo.ArrayIndex = 0;
		targetInfo.MipLevel = 0;
		targetInfo.Flags = renderTarget.Flags;
		targetInfo.pTarget = (Texture*)renderTarget.pResource->GetPhysicalUnsafe();

		if (renderTarget.pResolveTarget && renderTarget.pResource != renderTarget.pResolveTarget)
			targetInfo.Flags |= RenderPassColorFlags::Resolve;

		if (renderTarget.pResolveTarget)
			targetInfo.pResolveTarget = (Texture*)renderTarget.pResolveTarget->GetPhysicalUnsafe();
	}
	if (m_Pass.DepthStencilTarget.pResource)
	{
		passInfo.DepthStencilTarget.pTarget = (Texture*)m_Pass.DepthStencilTarget.pResource->GetPhysicalUnsafe();
		passInfo.DepthStencilTarget.Flags = m_Pass.DepthStencilTarget.Flags;
	}
	return passInfo;
}

DeviceResource* RGResources::GetResource(const RGResource* pResource, D3D12_RESOURCE_STATES requiredAccess) const
{
	gAssert(std::find_if(m_Pass.Accesses.begin(), m_Pass.Accesses.end(), [=](const RGPass::ResourceAccess& access) { return access.pResource == pResource && (requiredAccess == 0 || (access.Access & requiredAccess) != 0); }) != m_Pass.Accesses.end());
	return pResource->GetPhysicalUnsafe();
}

namespace RGUtils
{
	RGPass& AddCopyPass(RGGraph& graph, RGResource* pSource, RGResource* pTarget)
	{
		return graph.AddPass(Sprintf("Copy [%s -> %s]", pSource->GetName(), pTarget->GetName()).c_str(), RGPassFlag::Copy)
			.Read(pSource)
			.Write(pTarget)
			.Bind([=](CommandContext& context, const RGResources& resources)
				{
					context.CopyResource(resources.Get(pSource), resources.Get(pTarget));
				});
	}

	RGPass& AddResolvePass(RGGraph& graph, RGTexture* pSource, RGTexture* pTarget)
	{
		return graph.AddPass(Sprintf("Resolve [%s -> %s]", pSource->GetName(), pTarget->GetName()).c_str(), RGPassFlag::Raster)
			.RenderTarget(pSource, RenderPassColorFlags::None, pTarget);
	}

	RGBuffer* CreatePersistent(RGGraph& graph, const char* pName, const BufferDesc& bufferDesc, Ref<Buffer>* pStorageTarget, bool* pOutIsNew)
	{
		gAssert(pStorageTarget);
		RGBuffer* pBuffer = nullptr;
		if (pStorageTarget->Get())
		{
			if (pStorageTarget->Get()->GetDesc().IsCompatible(bufferDesc))
				pBuffer = graph.Import(*pStorageTarget);
		}
		if (pOutIsNew)
			*pOutIsNew = pBuffer == nullptr;
		if (!pBuffer)
		{
			pBuffer = graph.Create(pName, bufferDesc);
		}
		graph.Export(pBuffer, pStorageTarget);
		return pBuffer;
	}

	RGTexture* CreatePersistent(RGGraph& graph, const char* pName, const TextureDesc& textureDesc, Ref<Texture>* pStorageTarget, bool* pOutIsNew)
	{
		gAssert(pStorageTarget);
		RGTexture* pTexture = nullptr;
		if (pStorageTarget->Get())
		{
			if (pStorageTarget->Get()->GetDesc().IsCompatible(textureDesc))
				pTexture = graph.TryImport(*pStorageTarget);
		}
		if (pOutIsNew)
			*pOutIsNew = pTexture == nullptr;
		if (!pTexture)
		{
			pTexture = graph.Create(pName, textureDesc);
		}
		graph.Export(pTexture, pStorageTarget);
		return pTexture;
	}

	void DoUpload(RGGraph& graph, RGBuffer* pTarget, const void* pSource, uint32 size)
	{
		void* pSrc = graph.Allocate(size);
		memcpy(pSrc, pSource, size);

		graph.AddPass("Upload", RGPassFlag::Copy)
			.Write(pTarget)
			.Bind([=](CommandContext& context, const RGResources& resources)
				{
					ScratchAllocation alloc = context.AllocateScratch(size);
					memcpy(alloc.pMappedMemory, pSrc, size);
					context.CopyBuffer(alloc.pBackingResource, resources.Get(pTarget), size, alloc.Offset, 0);
				});
	}
}
