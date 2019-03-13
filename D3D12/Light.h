#pragma once

#pragma pack(push)
#pragma pack(16) 
struct Light
{
	int Enabled;
	Vector3 Position;
	Vector3 Direction;
	float Intensity;
	Vector4 Color;
	float Range;
	float SpotLightAngle;
	float Attenuation;
	uint32 Type;

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
};
#pragma pack(pop)