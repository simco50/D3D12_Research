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
	struct RenderData
	{
		Vector3 Position = Vector3::Zero;
		int Enabled = 1;
		Vector3 Direction = Vector3::Forward;
		LightType Type = LightType::MAX;
		Vector2 SpotlightAngles = Vector2::Zero;
		uint32 Colour = 0xFFFFFFFF;
		float Intensity = 1.0f;
		float Range = 1.0f;
		int32 ShadowIndex = -1;
		float InvShadowSize = 0;
		int VolumetricLight = 1;
		int LightTexture = -1;
		int pad[3];
	};
	
	Vector3 Position = Vector3::Zero;
	Vector3 Direction = Vector3::Forward;
	LightType Type = LightType::MAX;
	float UmbraAngle = 0;
	float PenumbraAngle = 0;
	Color Colour = Color(1, 1, 1, 1);
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
		data.Enabled = Intensity > 0;
		data.Direction = Direction;
		data.Type = Type;
		data.SpotlightAngles.x = cos(PenumbraAngle * Math::ToRadians / 2.0f);
		data.SpotlightAngles.y = cos(UmbraAngle * Math::ToRadians / 2.0f);
		data.Colour = Math::EncodeColor(Colour);
		data.Intensity = Intensity;
		data.Range = Range;
		data.ShadowIndex = CastShadows ? ShadowIndex : -1;
		data.InvShadowSize = 1.0f / ShadowMapSize;
		data.VolumetricLight = VolumetricLighting;
		data.LightTexture = LightTexture;
		return data;
	}

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Color& color = Color(1, 1, 1, 1))
	{
		Light l{};
		l.Position = position;
		l.Direction = direction;
		l.Type = LightType::Directional;
		l.Intensity = intensity;
		l.Colour = color;
		return l;
	}

	static Light Point(const Vector3& position, float radius, float intensity = 1.0f, const Color& color = Color(1, 1, 1, 1))
	{
		Light l{};
		l.Position = position;
		l.Range = radius;
		l.Type = LightType::Point;
		l.Intensity = intensity;
		l.Colour = color;
		return l;
	}

	static Light Spot(const Vector3& position, float range, const Vector3& direction, float umbraAngleInDegrees = 60, float penumbraAngleInDegrees = 40, float intensity = 1.0f, const Color& color = Color(1, 1, 1, 1))
	{
		Light l{};
		l.Position = position;
		l.Range = range;
		l.Direction = direction;
		l.PenumbraAngle = penumbraAngleInDegrees;
		l.UmbraAngle = umbraAngleInDegrees;
		l.Type = LightType::Spot;
		l.Intensity = intensity;
		l.Colour = color;
		return l;
	}
};
