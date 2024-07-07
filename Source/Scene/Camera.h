#pragma once

#include "Renderer/SceneView.h"

class Camera
{
public:
	virtual ~Camera() = default;

	virtual void Update();

	void SetPosition(const Vector3& position)			{ m_Position = position; }
	void SetRotation(const Quaternion& rotation)		{ m_Rotation = rotation; }

	void SetViewport(const FloatRect& rect)				{ m_Transform.Viewport = rect; }
	void SetFoV(float fov)								{ m_Transform.FoV = fov; }
	void SetNearPlane(float nearPlane)					{ m_Transform.NearPlane = nearPlane; }
	void SetFarPlane(float farPlane)					{ m_Transform.FarPlane = farPlane; }
	void SetJitter(bool jitter)							{ m_Jitter = jitter; }

	Ray GetMouseRay() const;

	const ViewTransform& GetViewTransform() const		{ return m_Transform; }
	const Vector3& GetPosition() const					{ return m_Position; }
	const Quaternion& GetRotation() const				{ return m_Rotation; }

protected:
	Vector3 m_Position;
	Quaternion m_Rotation;

private:
	bool m_Jitter = true;
	ViewTransform m_Transform;
};

class FreeCamera : public Camera
{
private:
	virtual void Update() override;
	Vector3 m_Velocity;
};
