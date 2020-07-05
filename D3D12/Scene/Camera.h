#pragma once

class Camera
{
public:
	virtual ~Camera() = default;

	virtual void Update() {};

	void SetPosition(const Vector3& position);
	void SetRotation(const Quaternion& rotation);

	const Vector3& GetPosition() const { return m_Position; }
	const Quaternion& GetRotation() const { return m_Rotation; }

	void SetDirty() { m_Dirty = true; }
	void SetFoV(float fov);
	void SetClippingPlanes(float nearPlane, float farPlane);
	void SetAspectRatio(float aspectRatio);

	void SetOrthographic(bool orthographic, float size = -1.0);

	void SetNearPlane(float nearPlane);
	void SetFarPlane(float farPlane);

	float GetNear() const { return m_NearPlane; }
	float GetFar() const { return m_FarPlane; }
	float GetFoV() const { return m_FoV; }

	const Matrix& GetView() const;
	const Matrix& GetProjection() const;
	const Matrix& GetViewProjection() const;
	const Matrix& GetViewInverse() const;
	const Matrix& GetProjectionInverse() const;
	const BoundingFrustum& GetFrustum() const;

protected:
	void OnDirty();
	Vector3 m_Position;
	Quaternion m_Rotation;

private:
	void UpdateMatrices() const;

	float m_Size = 50.0f;
	float m_FoV = 60.0f * Math::PI / 180;
	float m_NearPlane = 1.0f;
	float m_FarPlane = 500.0f;
	float m_OrthographicSize = 1;
	float m_AspectRatio = 1.0f;

	mutable Matrix m_Projection;
	mutable Matrix m_View;
	mutable Matrix m_ViewProjection;
	mutable Matrix m_ViewInverse;
	mutable Matrix m_ProjectionInverse;
	mutable BoundingFrustum m_Frustum;

	bool m_Perspective = true;
	mutable bool m_Dirty = true;
};

class FreeCamera : public Camera
{
private:
	virtual void Update() override;
	Vector3 m_Velocity;
};