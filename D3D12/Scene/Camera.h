#pragma once

#include "Graphics/SceneView.h"

class Camera
{
public:
	virtual ~Camera() = default;

	virtual void Update();

	void SetPosition(const Vector3& position);
	void SetRotation(const Quaternion& rotation);

	const Vector3& GetPosition() const { return m_Position; }
	const Quaternion& GetRotation() const { return m_Rotation; }

	void SetViewport(const FloatRect& rect);
	void SetDirty() { m_Dirty = true; }
	void SetFoV(float fov);
	void SetClippingPlanes(float nearPlane, float farPlane);

	void SetOrthographic(bool orthographic, float size = -1.0);

	void SetNearPlane(float nearPlane);
	void SetFarPlane(float farPlane);

	void SetJitterWeight(float weight);
	void SetLockPrevTransform(bool lock) { m_UpdatePrevMatrices = !lock; }
	bool GetLockPrevTransform() const { return !m_UpdatePrevMatrices; }

	const ViewTransform& GetViewTransform() const { return m_Transform; }

	float GetNear() const { return m_Transform.NearPlane; }
	float GetFar() const { return m_Transform.FarPlane; }
	float GetFoV() const { return m_Transform.FoV; }

	const Vector2& GetJitter() const { return m_Transform.Jitter; }
	const Vector2& GetPreviousJitter() const { return m_Transform.PreviousJitter; }
	const Matrix& GetView() const;
	const Matrix& GetProjection() const;
	const Matrix& GetViewProjection() const;
	Matrix GetViewProjectionInverse() const;
	const Matrix& GetViewInverse() const;
	const Matrix& GetProjectionInverse() const;
	const Matrix& GetPreviousViewProjection() const { return m_Transform.PreviousViewProjection; }
	const BoundingFrustum& GetFrustum() const;
	Ray GetMouseRay() const;

protected:
	void OnDirty();
	Vector3 m_Position;
	Quaternion m_Rotation;

private:
	void UpdateMatrices() const;

	bool m_UpdatePrevMatrices = true;
	mutable ViewTransform m_Transform;
	mutable bool m_Dirty = true;
};

class FreeCamera : public Camera
{
private:
	virtual void Update() override;
	Vector3 m_Velocity;
};
