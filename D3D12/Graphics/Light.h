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
	Vector3 Position = Vector3::Zero;
	Quaternion Rotation = Quaternion::Identity;
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

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Color& color = Colors::White)
	{
		Light l{};
		l.Position = position;
		l.Rotation = Quaternion::LookRotation(direction, Vector3::Up);
		l.Type = LightType::Directional;
		l.Intensity = intensity;
		l.Colour = color;
		l.Range = std::numeric_limits<float>::max();
		return l;
	}

	static Light Point(const Vector3& position, float radius, float intensity = 1.0f, const Color& color = Colors::White)
	{
		Light l{};
		l.Position = position;
		l.Range = radius;
		l.Type = LightType::Point;
		l.Intensity = intensity;
		l.Colour = color;
		return l;
	}

	static Light Spot(const Vector3& position, float range, const Vector3& direction, float umbraAngleInDegrees = 60, float penumbraAngleInDegrees = 40, float intensity = 1.0f, const Color& color = Colors::White)
	{
		Light l{};
		l.Position = position;
		l.Range = range;
		l.Rotation = Quaternion::LookRotation(direction, Vector3::Up);
		l.PenumbraAngleDegrees = penumbraAngleInDegrees;
		l.UmbraAngleDegrees = umbraAngleInDegrees;
		l.Type = LightType::Spot;
		l.Intensity = intensity;
		l.Colour = color;
		return l;
	}
};
