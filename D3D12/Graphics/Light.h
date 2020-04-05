#pragma once

struct Light
{
	enum class Type : uint32
	{
		Directional,
		Point,
		Spot,
		MAX
	};

	Vector3 Position;
	int Enabled;
	Vector3 Direction;
	Type LightType;
	Vector2 SpotlightAngles;
	uint32 Colour;
	float Intensity;
	float Range;
	int32 ShadowIndex = -1;

	void SetColor(const Color& c)
	{
		Colour = (uint32)(c.x * 255) << 24 |
			(uint32)(c.y * 255) << 16 |
			(uint32)(c.z * 255) << 8 |
			(uint32)(c.w * 255) << 0;
	}

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Color& color = Color(1, 1, 1, 1))
	{
		Light l;
		l.Enabled = true;
		l.Position = position;
		l.Direction = direction;
		l.LightType = Type::Directional;
		l.Intensity = intensity;
		l.SetColor(color);
		return l;
	}

	static Light Point(const Vector3& position, float radius, float intensity = 1.0f, const Color& color = Color(1, 1, 1, 1))
	{
		Light l;
		l.Enabled = true;
		l.Position = position;
		l.Range = radius;
		l.LightType = Type::Point;
		l.Intensity = intensity;
		l.SetColor(color);
		return l;
	}

	static Light Spot(const Vector3& position, float range, const Vector3& direction, float umbraAngleInDegrees = 60, float penumbraAngleInDegrees = 40, float intensity = 1.0f, const Color& color = Color(1, 1, 1, 1))
	{
		Light l;
		l.Enabled = true;
		l.Position = position;
		l.Range = range;
		l.Direction = direction;
		l.SpotlightAngles.x = cos(penumbraAngleInDegrees / 2.0f * Math::PI / 180.0f);
		l.SpotlightAngles.y = cos(umbraAngleInDegrees / 2.0f * Math::PI / 180.0f);
		l.LightType = Type::Spot;
		l.Intensity = intensity;
		l.SetColor(color);
		return l;
	}
};