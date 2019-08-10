#pragma once

class Graphics;

class Camera
{
public:
	explicit Camera(Graphics* pGraphics);
	virtual ~Camera();

	virtual void Update() {};

	void SetPosition(const Vector3& position);
	void SetRotation(const Quaternion& rotation);

	const Vector3& GetPosition() const { return m_Position; }
	const Quaternion& GetRotation() const { return m_Rotation; }

	void SetFoV(float fov);
	void SetViewport(float x, float y, float width, float height);
	FloatRect GetViewport() const { return GetAbsoluteViewport(); }
	void SetClippingPlanes(float nearPlane, float farPlane);

	void SetOrthographic(bool orthographic);
	void SetOrthographicSize(float size);

	void SetNearPlane(float nearPlane);
	void SetFarPlane(float farPlane);

	float GetNear() const { return m_NearPlane; }
	float GetFar() const { return m_FarPlane; }

	float SetFoV() const { return m_FoV; }

	const Matrix& GetView() const;
	const Matrix& GetProjection() const;
	const Matrix& GetViewProjection() const;
	const Matrix& GetViewInverse() const;
	const Matrix& GetProjectionInverse() const;

protected:
	void OnDirty();
	Vector3 m_Position;
	Quaternion m_Rotation;

private:
	void UpdateMatrices() const;

	FloatRect GetAbsoluteViewport() const;

	Graphics* m_pGraphics = nullptr;

	float m_Size = 50.0f;
	float m_FoV = 60.0f * Math::PI / 180;
	float m_NearPlane = 1.0f;
	float m_FarPlane = 500.0f;

	FloatRect m_Viewport;
	mutable Matrix m_Projection;
	mutable Matrix m_View;
	mutable Matrix m_ViewProjection;
	mutable Matrix m_ViewInverse;
	mutable Matrix m_ProjectionInverse;

	bool m_Perspective = true;
	mutable bool m_Dirty = true;
};

class FreeCamera : public Camera
{
public:
	FreeCamera(Graphics* pGraphics);
private:
	virtual void Update() override;

	Vector3 m_Velocity;
};