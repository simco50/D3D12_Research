#pragma once
#include "Graphics/RHI/RHI.h"
#include "Graphics/RenderGraph/RenderGraphDefinitions.h"
#include "Graphics/SceneView.h"

struct SceneView;
struct SceneTextures;
struct LightCull3DData;

struct VolumetricFogData
{
	RefCountPtr<Texture> pFogHistory;
};

class VolumetricFog
{
public:
	VolumetricFog(GraphicsDevice* pDevice);
	~VolumetricFog();

	RGTexture* RenderFog(RGGraph& graph, const SceneView* pView, const LightCull3DData& cullData, VolumetricFogData& fogData);

private:
	GraphicsDevice* m_pDevice;

	RefCountPtr<RootSignature> m_pCommonRS;

	//Volumetric Fog
	RefCountPtr<PipelineState> m_pInjectVolumeLightPSO;
	RefCountPtr<PipelineState> m_pAccumulateVolumeLightPSO;
};
