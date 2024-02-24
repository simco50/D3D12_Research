#pragma once
class Texture;

enum class LightType
{
	Directional,
	Point,
	Spot,
	MAX
};

struct Light
{
	LightType Type = LightType::MAX;
	float UmbraAngleDegrees = 0;
	float PenumbraAngleDegrees = 0;
	Color Colour = Colors::White;
	float Intensity = 1;
	float Range = 1;
	bool VolumetricLighting = false;
	uint32 MatrixIndex = DescriptorHandle::InvalidHeapIndex;
	std::vector<Texture*> ShadowMaps;
	Ref<Texture> pLightTexture = nullptr;
	int ShadowMapSize = 512;
	bool CastShadows = false;
};
