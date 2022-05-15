#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/Profiler.h"
#include "Core/CommandLine.h"

bool HasWriteResourceState(D3D12_RESOURCE_STATES state)
{
	return EnumHasAnyFlags(state,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
		D3D12_RESOURCE_STATE_RENDER_TARGET |
		D3D12_RESOURCE_STATE_DEPTH_WRITE |
		D3D12_RESOURCE_STATE_COPY_DEST |
		D3D12_RESOURCE_STATE_RESOLVE_DEST |
		D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE |
		D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE |
		D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE
	);
};

RGPass& RGPass::Read(Span<RGResource*> resources)
{
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
	if (EnumHasAnyFlags(Flags, RGPassFlag::Copy))
		state = D3D12_RESOURCE_STATE_COPY_SOURCE;
	for (RGResource* pResource : resources)
	{
		bool isIndirectArgs = pResource->Type == RGResourceType::Buffer && EnumHasAllFlags(static_cast<RGBuffer*>(pResource)->GetDesc().Usage, BufferFlag::IndirectArguments);

		AddAccess(pResource, isIndirectArgs ? D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT : state);
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
		AddAccess(pResource, state);
	}

	return *this;
}

RGPass& RGPass::RenderTarget(RGTexture* pResource, RenderPassAccess access)
{
	AddAccess(pResource, D3D12_RESOURCE_STATE_RENDER_TARGET);
	RenderTargets.push_back({ pResource, access, nullptr });
	return *this;
}

RGPass& RGPass::RenderTarget(RGTexture* pResource, RenderTargetLoadAction loadAction, RGTexture* pResolveTarget)
{
	AddAccess(pResource, D3D12_RESOURCE_STATE_RENDER_TARGET);
	AddAccess(pResolveTarget, D3D12_RESOURCE_STATE_RESOLVE_DEST);
	RenderTargets.push_back({ pResource, (RenderPassAccess)CombineRenderTargetAction(loadAction, RenderTargetStoreAction::Resolve), pResolveTarget });
	return *this;
}

RGPass& RGPass::DepthStencil(RGTexture* pResource, RenderPassAccess depthAccess, bool writeDepth, RenderPassAccess stencilAccess)
{
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
		checkf(!HasWriteResourceState(it->Access) || !HasWriteResourceState(state), "Resource (%s) may only have 1 write state", pResource->Name);
		it->Access |= state;
	}
	else
	{
		Accesses.push_back({ pResource, state });
	}
}

RGPass& RGGraph::AddCopyPass(const char* pName, RGResource* pSource, RGResource* pTarget)
{
	return AddPass(pName, RGPassFlag::Copy)
		.Read(pSource)
		.Write(pTarget)
		.Bind([=](CommandContext& context, const RGPassResources& resources)
			{
				context.CopyResource(pSource->pResource, pTarget->pResource);
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
	constexpr bool PassCulling = true;

	if (PassCulling)
	{
		auto WritesTo = [&](RGResource* pResource, RGPass* pPass)
		{
			for (RGPass::ResourceAccess& access : pPass->Accesses)
			{
				if (access.pResource == pResource && HasWriteResourceState(access.Access))
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
					if (access.pResource->IsImported && HasWriteResourceState(access.Access))
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
						{
							pPass->PassDependencies.push_back(pIterPass);
						}
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
		{
			pPass->IsCulled = false;
		}
	}

	// Tell the resources when they're first/last accessed and apply usage flags
	for (const RGPass* pPass : m_RenderPasses)
	{
		if (pPass->IsCulled)
			continue;

		for (const RGPass::ResourceAccess& access : pPass->Accesses)
		{
			RGResource* pResource = access.pResource;
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
			if (pResource->pResourceReference == nullptr)
			{
				if (pResource->Type == RGResourceType::Texture)
				{
					pResource->SetResource(m_ResourcePool.Allocate(pResource->Name, static_cast<RGTexture*>(pResource)->Desc));
				}
				else if (pResource->Type == RGResourceType::Buffer)
				{
					pResource->SetResource(m_ResourcePool.Allocate(pResource->Name, static_cast<RGBuffer*>(pResource)->Desc));
				}
			}
			check(pResource->pResourceReference);
		}

		for (const RGPass::ResourceAccess& access : pPass->Accesses)
		{
			RGResource* pResource = access.pResource;
			if (!pResource->IsImported && !pResource->IsExported && pResource->pLastAccess == pPass)
			{
				check(pResource->pResourceReference);
				pResource->Release();
			}
		}
	}
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

		if (!pPass->IsCulled)
		{
			ExecutePass(pPass, *pContext);
		}
	}
	m_LastSyncPoint = pContext->Execute(false);

	for (ExportedTexture& exportResource : m_ExportTextures)
	{
		RefCountPtr<Texture> pTexture = exportResource.pTexture->Get();
		pTexture->SetName(exportResource.pTexture->Name);
		*exportResource.pTarget = pTexture;
	}

	for (ExportedBuffer& exportResource : m_ExportBuffers)
	{
		RefCountPtr<Buffer> pBuffer = exportResource.pBuffer->Get();
		pBuffer->SetName(exportResource.pBuffer->Name);
		*exportResource.pTarget = pBuffer;
	}

	DestroyData();
	return m_LastSyncPoint;
}

void RGGraph::ExecutePass(RGPass* pPass, CommandContext& context)
{
	PrepareResources(pPass, context);

	if (pPass->pExecuteCallback)
	{
		GPU_PROFILE_SCOPE_CONDITIONAL(pPass->Name, &context, !EnumHasAnyFlags(pPass->Flags, RGPassFlag::Invisible));
		RGPassResources resources(*pPass);
		pPass->pExecuteCallback->Execute(context, resources);
	}
}

void RGGraph::PrepareResources(RGPass* pPass, CommandContext& context)
{
	for (const RGPass::ResourceAccess& access : pPass->Accesses)
	{
		RGResource* pResource = access.pResource;
		checkf(pResource->pResource, "Resource was not allocated during the graph compile phase");
		checkf(pResource->IsImported || pResource->IsExported || !pResource->pResourceReference, "If resource is not external, it's reference should be released during the graph compile phase");
		context.InsertResourceBarrier(pResource->pResource, access.Access);
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
		targetInfo.Target = renderTarget.pResource->Get();
		targetInfo.Access = renderTarget.Access;
		if (renderTarget.pResolveTarget)
			targetInfo.ResolveTarget = renderTarget.pResolveTarget->Get();
	}
	if (m_Pass.DepthStencilTarget.pResource)
	{
		passInfo.DepthStencilTarget.Target = m_Pass.DepthStencilTarget.pResource->Get();
		passInfo.DepthStencilTarget.Access = m_Pass.DepthStencilTarget.Access;
		passInfo.DepthStencilTarget.StencilAccess = m_Pass.DepthStencilTarget.StencilAccess;
		passInfo.DepthStencilTarget.Write = m_Pass.DepthStencilTarget.Write;
	}
	return passInfo;
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
