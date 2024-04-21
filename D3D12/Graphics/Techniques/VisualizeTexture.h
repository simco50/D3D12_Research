#pragma once

#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"

struct CaptureTextureContext
{
	// Input
	int					CubeFaceIndex = 0;
	float				RangeMin = 0.0f;
	float				RangeMax = 1.0f;
	bool				VisibleChannels[4] = { true, true, true, true };
	int					MipLevel = 0;
	float				Slice = 0.0f;

	// Private
	std::string			SourceName;
	TextureDesc			SourceDesc;
	float				Scale = 1.0f;
	bool				XRay = false;
	Vector2u			HoveredPixel;

	// Resources
	Ref<Texture>		pTextureTarget;
	Ref<Buffer>			pReadbackBuffer;
	Ref<Buffer>			pPickBuffer;
	uint32				ReadbackIndex = 0;

	struct PickData
	{
		Vector4 DataFloat;
		Vector4u DataUInt;
	} Pick;
};


class CaptureTextureSystem
{
public:
	CaptureTextureSystem(GraphicsDevice* pDevice);
	void Capture(RGGraph& graph, CaptureTextureContext& captureContext, RGTexture* pTexture);
	void RenderUI(CaptureTextureContext& captureContext, const ImVec2& viewportOrigin, const ImVec2& viewportSize);

private:
	Ref<PipelineState> m_pVisualizePSO;
	Ref<RootSignature> m_pVisualizeRS;
};
