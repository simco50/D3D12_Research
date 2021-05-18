#pragma once

enum class LightType
{
	Directional,
	Point,
	Spot,
	MAX
};

struct Light
{
	enum Flags
	{
		None =						0,
		Enabled =					1 << 0,
		Shadows =					1 << 1,
		Volumetric =				1 << 2,
		PointAttenuation =			1 << 3,
		DirectionalAttenuation =	1 << 4,

		PointLight =				PointAttenuation,
		SpotLight =					PointAttenuation | DirectionalAttenuation,
		DirectionalLight =			None,
	};

	struct RenderData
	{
		Vector3 Position = Vector3::Zero;
		uint32 Flags = 0;
		Vector3 Direction = Vector3::Forward;
		uint32 Colour = 0xFFFFFFFF;
		Vector2 SpotlightAngles = Vector2::Zero;
		float Intensity = 1.0f;
		float Range = 1.0f;
		int32 ShadowIndex = -1;
		float InvShadowSize = 0;
		int LightTexture = -1;
	};
	
	Vector3 Position = Vector3::Zero;
	Vector3 Direction = Vector3::Forward;
	LightType Type = LightType::MAX;
	float UmbraAngleDegrees = 0;
	float PenumbraAngleDegrees = 0;
	Color Colour = Colors::White;
	float Intensity = 1;
	float Range = 1;
	bool VolumetricLighting = false;
	int LightTexture = -1;
	int ShadowIndex = -1;
	int ShadowMapSize = 512;
	bool CastShadows = false;

	RenderData GetData() const
	{
		RenderData data;
		data.Position = Position;
		data.Direction = Direction;
		data.SpotlightAngles.x = cos(PenumbraAngleDegrees * Math::DegreesToRadians / 2.0f);
		data.SpotlightAngles.y = cos(UmbraAngleDegrees * Math::DegreesToRadians / 2.0f);
		data.Colour = Math::EncodeColor(Colour);
		data.Intensity = Intensity;
		data.Range = Range;
		data.ShadowIndex = CastShadows ? ShadowIndex : -1;
		data.InvShadowSize = 1.0f / ShadowMapSize;
		data.LightTexture = LightTexture;
		if (VolumetricLighting)
		{
			data.Flags |= Flags::Volumetric;
		}
		if (Intensity > 0)
		{
			data.Flags |= Flags::Enabled;
		}
		if (CastShadows)
		{
			data.Flags |= Flags::Shadows;
		}
		if (Type == LightType::Point)
		{
			data.Flags |= Flags::PointLight;
		}
		else if (Type == LightType::Spot)
		{
			data.Flags |= Flags::SpotLight;
		}
		else if (Type == LightType::Directional)
		{
			data.Flags |= Flags::DirectionalLight;
		}
		return data;
	}

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Color& color = Colors::White)
	{
		Light l{};
		l.Position = position;
		l.Direction = direction;
		l.Type = LightType::Directional;
		l.Intensity = intensity;
		l.Colour = color;
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
		l.Direction = direction;
		l.PenumbraAngleDegrees = penumbraAngleInDegrees;
		l.UmbraAngleDegrees = umbraAngleInDegrees;
		l.Type = LightType::Spot;
		l.Intensity = intensity;
		l.Colour = color;
		return l;
	}
};
