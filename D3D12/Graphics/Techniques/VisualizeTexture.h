#pragma once

#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

class VisualizeTexture
{
public:
	VisualizeTexture(GraphicsDevice* pDevice);
	void Capture(RGGraph& graph, RGTexture* pTexture);
	void RenderUI(const ImVec2& viewportOrigin, const ImVec2& viewportSize);

private:
	std::string SourceName;
	TextureDesc SourceDesc;
	RefCountPtr<Texture> pVisualizeTexture;
	int CubeFaceIndex = 0;
	float RangeMin = 0.0f;
	float RangeMax = 1.0f;
	bool VisibleChannels[4] = { true, true, true, true };
	int MipLevel = 0;
	float Slice = 0.0f;
	bool XRay = false;
	float Scale = 1.0f;

	RefCountPtr<PipelineState> m_pVisualizePSO;
	RefCountPtr<RootSignature> m_pVisualizeRS;
};
