#pragma once
#include "ShaderCommon.h"

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

	ShaderInterop::Light GetData() const
	{
		ShaderInterop::Light data;
		data.Position = Position;
		data.Direction = Direction;
		data.SpotlightAngles.x = cos(PenumbraAngleDegrees * Math::DegreesToRadians / 2.0f);
		data.SpotlightAngles.y = cos(UmbraAngleDegrees * Math::DegreesToRadians / 2.0f);
		data.Color = Math::EncodeColor(Colour);
		data.Intensity = Intensity;
		data.Range = Range;
		data.ShadowIndex = CastShadows ? ShadowIndex : -1;
		data.InvShadowSize = 1.0f / ShadowMapSize;
		data.LightTexture = LightTexture;
		data.Flags = 0;
		if (VolumetricLighting)
		{
			data.Flags |= ShaderInterop::LF_Volumetrics;
		}
		if (Intensity > 0)
		{
			data.Flags |= ShaderInterop::LF_Enabled;
		}
		if (CastShadows)
		{
			data.Flags |= ShaderInterop::LF_CastShadow;
		}
		if (Type == LightType::Point)
		{
			data.Flags |= ShaderInterop::LF_PointLight;
		}
		else if (Type == LightType::Spot)
		{
			data.Flags |= ShaderInterop::LF_SpotLight;
		}
		else if (Type == LightType::Directional)
		{
			data.Flags |= ShaderInterop::LF_DirectionalLight;
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
