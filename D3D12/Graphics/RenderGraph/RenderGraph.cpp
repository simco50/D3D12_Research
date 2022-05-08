#include "stdafx.h"
#include "RenderGraph.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/Profiler.h"
#include "Core/CommandLine.h"

RGPass& RGPass::Read(Span<RGResource*> resources)
{
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
	if (EnumHasAnyFlags(Flags, RGPassFlag::Copy))
		state = D3D12_RESOURCE_STATE_COPY_SOURCE;
	for (RGResource* resource : resources)
	{
		Accesses.push_back({ state, resource });
	}
	return *this;
}

RGPass& RGPass::Write(Span<RGResource*> resources)
{
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (EnumHasAnyFlags(Flags, RGPassFlag::Copy))
		state = D3D12_RESOURCE_STATE_COPY_DEST;

	for (RGResource* pHandle : resources)
	{
		Accesses.push_back({ state, pHandle });
	}

	return *this;
}

RGPass& RGPass::ReadWrite(Span<RGResource*> resources)
{
	check(EnumHasAnyFlags(Flags, RGPassFlag::Raster | RGPassFlag::Compute));
	for (RGResource* pHandle : resources)
	{
		Accesses.push_back({ D3D12_RESOURCE_STATE_UNORDERED_ACCESS, pHandle});
	}
	return *this;
}

RGPass& RGPass::RenderTarget(RGTexture* pResource, RenderPassAccess access)
{
	Accesses.push_back({ D3D12_RESOURCE_STATE_RENDER_TARGET, pResource });
	RenderTargets.push_back({ pResource, access });
	return *this;
}

RGPass& RGPass::DepthStencil(RGTexture* pResource, RenderPassAccess depthAccess, bool writeDepth, RenderPassAccess stencilAccess)
{
	checkf(!DepthStencilTarget.pResource, "Depth Target already assigned");
	Accesses.push_back({ writeDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_DEPTH_READ, pResource });
	DepthStencilTarget = { pResource, depthAccess, stencilAccess, writeDepth };
	return *this;
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
#define ENABLE_CULLING 0

#if ENABLE_CULLING

	auto IsWrite = [](D3D12_RESOURCE_STATES state)
	{
		switch (state)
		{
		case D3D12_RESOURCE_STATE_PRESENT:
		case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
		case D3D12_RESOURCE_STATE_RENDER_TARGET:
		case D3D12_RESOURCE_STATE_DEPTH_WRITE:
		case D3D12_RESOURCE_STATE_COPY_DEST:
		case D3D12_RESOURCE_STATE_RESOLVE_DEST:
			return true;
		}
		return false;
	};

	auto WritesTo = [&](RGHandleT handle, RGPass* pPass)
	{
		for (RGPass::RGAccess& access : pPass->Accesses)
		{
			if(IsWrite(access.Access))
				return true;
		}
		return false;
	};

	std::vector<RGPass*> cullStack;
	for (RGPass* pPass : m_RenderPasses)
	{
		if (EnumHasAllFlags(pPass->Flags, RGPassFlag::NeverCull))
		{
			cullStack.push_back(pPass);
		}

		for (RGPass::RGAccess access : pPass->Accesses)
		{
			for (RGPass* pIterPass : m_RenderPasses)
			{
				if (pIterPass != pPass && WritesTo(access.Resource, pIterPass))
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

#else

	for (RGPass* pPass : m_RenderPasses)
	{
		pPass->IsCulled = false;
	}

#endif

	auto ApplyResourceFlag = [](RGResource* pResource, D3D12_RESOURCE_STATES state) {
		if (pResource->Type == RGResourceType::Buffer)
		{
			BufferDesc& desc = pResource->DescBuffer;
			if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
				desc.Usage |= BufferFlag::UnorderedAccess;
			if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE))
				desc.Usage |= BufferFlag::ShaderResource;
		}
		else if (pResource->Type == RGResourceType::Texture)
		{
			TextureDesc& desc = pResource->DescTexture;
			if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
				desc.Usage |= TextureFlag::UnorderedAccess;
			if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE))
				desc.Usage |= TextureFlag::ShaderResource;
			if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_DEPTH_WRITE))
				desc.Usage |= TextureFlag::DepthStencil;
			if (EnumHasAnyFlags(state, D3D12_RESOURCE_STATE_RENDER_TARGET))
				desc.Usage |= TextureFlag::RenderTarget;
		}
	};

	// Tell the resources when they're first/last accessed
	for (RGPass* pPass : m_RenderPasses)
	{
		if (!pPass->IsCulled)
		{
			for (RGPass::RGAccess access : pPass->Accesses)
			{
				RGResource* pResource = access.Resource;
				pResource->pFirstAccess = pResource->pFirstAccess ? pResource->pFirstAccess : pPass;
				pResource->pLastAccess = pPass;

				ApplyResourceFlag(pResource, access.Access);
			}
		}
	}

	auto ConditionallyAllocateResource = [&](const RGPass* pPass, RGResource* pResource) {
		if (!pResource->IsImported && pResource->pFirstAccess == pPass && pResource->pResourceReference == nullptr)
		{
			if (pResource->Type == RGResourceType::Texture)
			{
				pResource->SetResource(m_ResourcePool.Allocate(pResource->Name, pResource->DescTexture));
			}
			else if (pResource->Type == RGResourceType::Buffer)
			{
				pResource->SetResource(m_ResourcePool.Allocate(pResource->Name, pResource->DescBuffer));
			}
		}
		check(pResource->pResourceReference);
	};

	auto ConditionallyReleaseResource = [&](const RGPass* pPass, RGResource* pResource) {
		if (!pResource->IsImported && !pResource->IsExported && pResource->pLastAccess == pPass)
		{
			check(pResource->pResourceReference);
			pResource->Release();
		}
	};

	// Go through all resources accesses and allocate on first access and de-allocate on last access
	// It's important to make the distinction between the RefCountPtr allocation and the Raw resource itself.
	// A de-allocate returns the resource back to the pool by resetting the RefCountPtr however the Raw resource keeps a reference to it to use during execution.
	// This is how we can "alias" resources (exact match only for now) and allocate our resources during compilation so that execution is thread-safe.
	for (RGPass* pPass : m_RenderPasses)
	{
		if (pPass->IsCulled)
			continue;

		for (RGPass::RGAccess& access : pPass->Accesses)
		{
			ConditionallyAllocateResource(pPass, access.Resource);
		}

		for (RGPass::RGAccess& access : pPass->Accesses)
		{
			ConditionallyReleaseResource(pPass, access.Resource);
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

	for (ExportedResource<Texture>& exportResource : m_ExportTextures)
	{
		*exportResource.pTarget = exportResource.pResource->Get();
	}

	for (ExportedResource<Buffer>& exportResource : m_ExportBuffers)
	{
		*exportResource.pTarget = exportResource.pResource->Get();
	}

	DestroyData();
	return m_LastSyncPoint;
}

void RGGraph::ExecutePass(RGPass* pPass, CommandContext& context)
{
	PrepareResources(pPass, context);

	if (pPass->ExecuteCallback.IsBound())
	{
		GPU_PROFILE_SCOPE_CONDITIONAL(pPass->Name, &context, !EnumHasAnyFlags(pPass->Flags, RGPassFlag::Invisible));
		RGPassResources resources(*this, *pPass);
		pPass->ExecuteCallback.Execute(context, resources);
	}
}

void RGGraph::PrepareResources(RGPass* pPass, CommandContext& context)
{
	for (const RGPass::RGAccess& access : pPass->Accesses)
	{
		RGResource* pResource = access.Resource;
		check(pResource->pResource);
		check(pResource->IsImported || pResource->IsExported || !pResource->pResourceReference);

		context.InsertResourceBarrier(pResource->pResource, access.Access);
	}

	context.FlushResourceBarriers();
}

void RGGraph::DestroyData()
{
	m_RenderPasses.clear();
	m_Resources.clear();
}

RenderPassInfo RGPassResources::GetRenderPassInfo() const
{
	RenderPassInfo passInfo;
	for (RGPass::RenderTargetAccess renderTarget : m_Pass.RenderTargets)
	{
		passInfo.RenderTargets[passInfo.RenderTargetCount].Target = renderTarget.pResource->Get();
		passInfo.RenderTargets[passInfo.RenderTargetCount].Access = renderTarget.Access;
		passInfo.RenderTargetCount++;
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
