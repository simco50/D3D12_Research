#pragma once

#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

class PipelineState;
class GraphicsDevice;

class JumpFlood
{
public:
	JumpFlood(GraphicsDevice* pDevice);
	~JumpFlood();

	RGTexture* Execute(RGGraph& graph, RGTexture* pInput, uint32 size);

private:
	Ref<PipelineState> m_pInit;
	Ref<PipelineState> m_pJumpFlood;
};
