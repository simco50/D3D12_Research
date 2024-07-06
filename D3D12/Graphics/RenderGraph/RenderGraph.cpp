#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/RHI/Device.h"
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
	check(EnumHasAllFlags(Flags, RGPassFlag::Raster));
	AddAccess(pResource, D3D12_RESOURCE_STATE_RENDER_TARGET);
	if(pResolveTarget && pResolveTarget != pResource)
		AddAccess(pResolveTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);

	RenderTargets.push_back({ pResource, flags, pResolveTarget });
	return *this;
}

RGPass& RGPass::DepthStencil(RGTexture* pResource, RenderPassDepthFlags flags)
{
	check(EnumHasAllFlags(Flags, RGPassFlag::Raster));
	check(!DepthStencilTarget.pResource, "Depth Target already assigned");
	AddAccess(pResource, EnumHasAllFlags(flags, RenderPassDepthFlags::ReadOnly) ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE);
	DepthStencilTarget = { pResource, flags };
	return *this;
}

void RGPass::AddAccess(RGResource* pResource, D3D12_RESOURCE_STATES state)
{
	check(pResource);
	auto it = std::find_if(Accesses.begin(), Accesses.end(), [=](const ResourceAccess& access) { return pResource == access.pResource; });
	if (it != Accesses.end())
	{
		if (EnumHasAllFlags(it->Access, state))
			return;

		check(it->Access == state || !D3D::HasWriteResourceState(it->Access), "Resource '%s' may not have any other states when it already has a write state (%s)", pResource->GetName(), D3D::ResourceStateToString(it->Access));
		check(it->Access == state || !D3D::HasWriteResourceState(state), "Resource '%s' may not use a write state (%s) while it already has another state (%s)", pResource->GetName(), D3D::ResourceStateToString(state), D3D::ResourceStateToString(it->Access));
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

void RGGraph::Compile(RGResourcePool& resourcePool, const RGGraphOptions& options)
{
	PROFILE_CPU_SCOPE();

	check(!m_IsCompiled);

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

	// Tell the resources when they're first/last accessed and apply usage flags
	for (const RGPass* pPass : m_Passes)
	{
		if (pPass->IsCulled)
			continue;

		for (const RGPass::ResourceAccess& access : pPass->Accesses)
		{
			RGResource* pResource = access.pResource;
			pResource->FirstAccess = pResource->FirstAccess.IsValid() ? pResource->FirstAccess : pPass->ID;
			pResource->LastAccess = pPass->ID;

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

	{

		PROFILE_CPU_SCOPE("Resource Allocation");

		// Go through all resources accesses and allocate on first access and de-allocate on last access
		// It's important to make the distinction between the Ref allocation and the Raw resource itself.
		// A de-allocate returns the resource back to the pool by resetting the Ref however the Raw resource keeps a reference to it to use during execution.
		// This is how we can "alias" resources (exact match only for now) and allocate our resources during compilation so that execution is thread-safe.
		for (RGPass* pPass : m_Passes)
		{
			if (pPass->IsCulled)
				continue;

			for (const RGPass::ResourceAccess& access : pPass->Accesses)
			{
				RGResource* pResource = access.pResource;
				if (!pResource->GetPhysicalUnsafe())
				{
					check(pResource->FirstAccess == pPass->ID);

					Ref<DeviceResource> pPhysicalResource;
					if (pResource->GetType() == RGResourceType::Texture)
						pPhysicalResource = resourcePool.Allocate(pResource->GetName(), static_cast<RGTexture*>(pResource)->GetDesc());
					else if (pResource->GetType() == RGResourceType::Buffer)
						pPhysicalResource = resourcePool.Allocate(pResource->GetName(), static_cast<RGBuffer*>(pResource)->GetDesc());
					else
						noEntry();
					pResource->SetResource(pPhysicalResource);
				}
				check(pResource->GetPhysicalUnsafe());


				if (pResource->GetPhysicalUnsafe()->UseStateTracking())
				{
					uint32 subResource = 0xFFFFFFFF;

					// If state tracking, add a transition in this pass and keep track of the resource state
					if (options.StateTracking)
					{
						D3D12_RESOURCE_STATES beforeState = pResource->GetPhysicalUnsafe()->GetResourceState(subResource);
						D3D12_RESOURCE_STATES afterState = access.Access;
						if (D3D::NeedsTransition(beforeState, afterState, true))
						{
							pPass->Transitions.push_back({ .pResource = pResource, .BeforeState = beforeState, .AfterState = afterState, .SubResource = subResource });
							pResource->GetPhysicalUnsafe()->SetResourceState(afterState, subResource);
						}
					}
					else
					{
						pPass->Transitions.push_back({ .pResource = pResource, .BeforeState = D3D12_RESOURCE_STATE_UNKNOWN, .AfterState = access.Access, .SubResource = subResource });
					}
				}
			}

			if (options.ResourceAliasing)
			{
				for (const RGPass::ResourceAccess& access : pPass->Accesses)
				{
					RGResource* pResource = access.pResource;
					if (!pResource->IsImported && !pResource->IsExported && pResource->LastAccess == pPass->ID)
					{
						pResource->Release();
					}
				}
			}
		}

		// If not aliasing resources, all resources still have a referece, so release it.
		if (!options.ResourceAliasing)
		{
			for (RGResource* pResource : m_Resources)
			{
				if (!pResource->IsImported && !pResource->IsExported && pResource->IsAllocated())
				{
					pResource->Release();
				}
			}
		}
	}

	// Export resources first so they can be available during pass execution.
	for (ExportedTexture& exportResource : m_ExportTextures)
	{
		check(exportResource.pTexture->GetPhysicalUnsafe(), "Exported texture doesn't have a physical resource assigned");
		Ref<Texture> pTexture = (Texture*)exportResource.pTexture->GetPhysicalUnsafe();
		pTexture->SetName(exportResource.pTexture->GetName());
		*exportResource.pTarget = pTexture;
	}
	for (ExportedBuffer& exportResource : m_ExportBuffers)
	{
		check(exportResource.pBuffer->GetPhysicalUnsafe(), "Exported buffer doesn't have a physical resource assigned");
		Ref<Buffer> pBuffer = (Buffer*)exportResource.pBuffer->GetPhysicalUnsafe();
		pBuffer->SetName(exportResource.pBuffer->GetName());
		*exportResource.pTarget = pBuffer;
	}

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
		pLastActivePass->NumEventsToEnd += eventsToEnd;
		check(eventsToStart.empty());
	}

	{
		PROFILE_CPU_SCOPE("Pass Grouping");

		if (options.Jobify)
		{
			// Group passes in jobs
			const uint32 maxPassesPerJob = options.CommandlistGroupSize;

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
			pLastPass->NumCPUEventsToEnd += (uint32)activeEvents.size();
		}
		else
		{
			m_PassExecuteGroups.push_back(Span<const RGPass*>(m_Passes.data(), (uint32)m_Passes.size()));
		}
	}

	m_IsCompiled = true;
}

void RGGraph::Export(RGTexture* pTexture, Ref<Texture>* pTarget, TextureFlag additionalFlags)
{
	auto it = std::find_if(m_ExportTextures.begin(), m_ExportTextures.end(), [&](const ExportedTexture& tex) { return tex.pTarget == pTarget; });
	check(it == m_ExportTextures.end(), "Texture '%s' is exported to a target that has already been exported to by another texture ('%s').", pTexture->GetName(), it->pTexture->GetName());
	pTexture->IsExported = true;
	pTexture->Desc.Flags |= additionalFlags;
	m_ExportTextures.push_back({ pTexture, pTarget });
}

void RGGraph::Export(RGBuffer* pBuffer, Ref<Buffer>* pTarget, BufferFlag additionalFlags)
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
		++m_Passes.back()->NumEventsToEnd;
}

void RGGraph::Execute(GraphicsDevice* pDevice)
{
	PROFILE_CPU_SCOPE();

	check(m_IsCompiled);

	Array<CommandContext*> contexts;
	contexts.reserve(m_PassExecuteGroups.size());

	if (m_PassExecuteGroups.size() > 1)
	{
		TaskContext context;

		{
			PROFILE_CPU_SCOPE("Schedule Render Jobs");
			for (Span<const RGPass*> passGroup : m_PassExecuteGroups)
			{
				CommandContext* pContext = pDevice->AllocateCommandContext();
				TaskQueue::Execute([this, passGroup, pContext](int)
					{
						for (const RGPass* pPass : passGroup)
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

	CommandContext::Execute(contexts);

	// Update exported resource names
	for (ExportedTexture& exportResource : m_ExportTextures)
		exportResource.pTexture->GetPhysicalUnsafe()->SetName(exportResource.pTexture->GetName());
	for (ExportedBuffer& exportResource : m_ExportBuffers)
		exportResource.pBuffer->GetPhysicalUnsafe()->SetName(exportResource.pBuffer->GetName());

	DestroyData();
}

void RGGraph::ExecutePass(const RGPass* pPass, CommandContext& context) const
{
	for (RGEventID eventIndex : pPass->EventsToStart)
	{
		const RGEvent& event = m_Events[eventIndex.GetIndex()];
		gGPUProfiler.BeginEvent(context.GetCommandList(), event.pName, event.pFilePath, event.LineNumber);
	}
	for (RGEventID eventIndex : pPass->CPUEventsToStart)
	{
		const RGEvent& event = m_Events[eventIndex.GetIndex()];
		gCPUProfiler.BeginEvent(event.pName, event.pFilePath, event.LineNumber);
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
	for (const RGPass::ResourceTransition& transition : pPass->Transitions)
	{
		RGResource* pResource = transition.pResource;

		check(pResource->GetPhysicalUnsafe(), "Resource was not allocated during the graph compile phase");
		check(pResource->IsImported || pResource->IsExported || !pResource->IsAllocated(), "If resource is not external, it's reference should be released during the graph compile phase");

		context.InsertResourceBarrier(pResource->GetPhysicalUnsafe(), transition.BeforeState, transition.AfterState, transition.SubResource);
	}

	context.FlushResourceBarriers();
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
		targetInfo.Target = (Texture*)renderTarget.pResource->GetPhysicalUnsafe();

		if (renderTarget.pResolveTarget && renderTarget.pResource != renderTarget.pResolveTarget)
			targetInfo.Flags |= RenderPassColorFlags::Resolve;

		if (renderTarget.pResolveTarget)
			targetInfo.ResolveTarget = (Texture*)renderTarget.pResolveTarget->GetPhysicalUnsafe();
	}
	if (m_Pass.DepthStencilTarget.pResource)
	{
		passInfo.DepthStencilTarget.Target = (Texture*)m_Pass.DepthStencilTarget.pResource->GetPhysicalUnsafe();
		passInfo.DepthStencilTarget.Flags = m_Pass.DepthStencilTarget.Flags;
	}
	return passInfo;
}

DeviceResource* RGResources::GetResource(const RGResource* pResource, D3D12_RESOURCE_STATES requiredAccess) const
{
	check(std::find_if(m_Pass.Accesses.begin(), m_Pass.Accesses.end(), [=](const RGPass::ResourceAccess& access) { return access.pResource == pResource && (requiredAccess == 0 || (access.Access & requiredAccess) != 0); }) != m_Pass.Accesses.end());
	return pResource->GetPhysicalUnsafe();
}

Ref<Texture> RGResourcePool::Allocate(const char* pName, const TextureDesc& desc)
{
	for (PooledTexture& texture : m_TexturePool)
	{
		Ref<Texture>& pTexture = texture.pResource;
		if (pTexture->GetNumRefs() == 1 && pTexture->GetDesc().IsCompatible(desc))
		{
			texture.LastUsedFrame = m_FrameIndex;
			pTexture->SetName(pName);
			return pTexture;
		}
	}
	return m_TexturePool.emplace_back(PooledTexture{ GetParent()->CreateTexture(desc, pName), m_FrameIndex }).pResource;
}

Ref<Buffer> RGResourcePool::Allocate(const char* pName, const BufferDesc& desc)
{
	for (PooledBuffer& buffer : m_BufferPool)
	{
		Ref<Buffer>& pBuffer = buffer.pResource;
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

	RGBuffer* CreatePersistent(RGGraph& graph, const char* pName, const BufferDesc& bufferDesc, Ref<Buffer>* pStorageTarget, bool doExport)
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

	RGTexture* CreatePersistent(RGGraph& graph, const char* pName, const TextureDesc& textureDesc, Ref<Texture>* pStorageTarget, bool doExport)
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
