#pragma once
#include "RHI/RHI.h"
#include "RenderGraph/RenderGraphDefinitions.h"

struct RenderView;
struct SceneTextures;
struct LightCull3DData;

struct VolumetricFogData
{
	Ref<Texture> pFogHistory;
};

struct FogVolume
{
	Vector3 Extents;
	Vector3 Color;
	float DensityChange;
	float DensityBase;
};

class VolumetricFog
{
public:
	VolumetricFog(GraphicsDevice* pDevice);
	~VolumetricFog();

	RGTexture* RenderFog(RGGraph& graph, const RenderView* pView, const LightCull3DData& cullData, VolumetricFogData& fogData);

private:
	GraphicsDevice* m_pDevice;

	//Volumetric Fog
	Ref<PipelineState> m_pInjectVolumeLightPSO;
	Ref<PipelineState> m_pAccumulateVolumeLightPSO;
};
