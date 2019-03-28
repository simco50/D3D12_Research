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
	Vector4 Color;
	float Range;
	float SpotLightAngle;
	float Attenuation;
	int32 ShadowIndex = -1;

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light l;
		l.Enabled = true;
		l.Position = position;
		l.Direction = direction;
		l.Color = Vector4(color.x, color.y, color.z, intensity);
		l.LightType = Type::Directional;
		l.ShadowIndex = -1;
		return l;
	}

	static Light Point(const Vector3& position, float radius, float intensity = 1.0f, float attenuation = 0.5f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light l;
		l.Enabled = true;
		l.Position = position;
		l.Range = radius;
		l.Color = Vector4(color.x, color.y, color.z, intensity);
		l.Attenuation = attenuation;
		l.LightType = Type::Point;
		l.ShadowIndex = -1;
		return l;
	}

	static Light Spot(const Vector3& position, float range, const Vector3& direction, float angle = XM_PIDIV4, float intensity = 1.0f, float attenuation = 0.5f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light l;
		l.Enabled = true;
		l.Position = position;
		l.Range = range;
		l.Direction = direction;
		l.SpotLightAngle = angle;
		l.Color = Vector4(color.x, color.y, color.z, intensity);
		l.Attenuation = attenuation;
		l.LightType = Type::Spot;
		l.ShadowIndex = -1;
		return l;
	}
};