#pragma once

#pragma pack(push)
#pragma pack(16) 
struct Light
{
	Vector3 Position;
	int Enabled;
	Vector3 Direction;
	uint32 Type;
	float Range;
	float SpotLightAngle;
	float Attenuation;
	float Intensity;
	Vector4 Color;

	static Light Directional(const Vector3& position, const Vector3& direction, float intensity = 1.0f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light l;
		l.Enabled = true;
		l.Position = position;
		l.Direction = direction;
		l.Intensity = intensity;
		l.Color = color;
		l.Type = 0;
		return l;
	}

	static Light Point(const Vector3& position, float radius, float intensity = 1.0f, float attenuation = 0.5f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light l;
		l.Enabled = true;
		l.Position = position;
		l.Range = radius;
		l.Intensity = intensity;
		l.Color = color;
		l.Attenuation = attenuation;
		l.Type = 1;
		return l;
	}

	static Light Cone(const Vector3& position, float range, const Vector3& direction, float angle = XM_PIDIV4, float intensity = 1.0f, float attenuation = 0.5f, const Vector4& color = Vector4(1, 1, 1, 1))
	{
		Light l;
		l.Enabled = true;
		l.Position = position;
		l.Range = range;
		l.Direction = direction;
		l.SpotLightAngle = angle;
		l.Intensity = intensity;
		l.Color = color;
		l.Attenuation = attenuation;
		l.Type = 2;
		return l;
	}
};
#pragma pack(pop)