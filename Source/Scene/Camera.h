#pragma once

#include "Renderer/Renderer.h"

class Camera
{
public:
	virtual ~Camera() = default;
	virtual void Update() {};

	void SetPosition(const Vector3& position)			{ m_Position = position; }
	void SetRotation(const Quaternion& rotation)		{ m_Rotation = rotation; }
	void SetFOV(float fov)								{ m_FOV = fov; }

	Ray GetMouseRay(const FloatRect& viewport) const;

	void ApplyViewTransform(ViewTransform& viewTransform, bool jitter) const;
	const Vector3& GetPosition() const					{ return m_Position; }
	const Quaternion& GetRotation() const				{ return m_Rotation; }

protected:
	Vector3 m_Position;
	Quaternion m_Rotation;

private:
	float m_FOV = 60.0f * Math::PI / 180;
};

class FreeCamera : public Camera
{
private:
	virtual void Update() override;
	Vector3 m_Velocity;
};
