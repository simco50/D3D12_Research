#include "stdafx.h"
#include "Camera.h"
#include "Core/Input.h"
#include "ImGuizmo.h"

void Camera::SetPosition(const Vector3& position)
{
	m_Position = position;
	OnDirty();
}

void Camera::SetRotation(const Quaternion& rotation)
{
	m_Rotation = rotation;
	OnDirty();
}

void Camera::SetViewport(const FloatRect& rect)
{
	m_Transform.Viewport = rect;
	OnDirty();
}

void Camera::SetFoV(float fov)
{
	m_Transform.FoV = fov;
	OnDirty();
}

void Camera::SetClippingPlanes(float nearPlane, float farPlane)
{
	m_Transform.NearPlane = nearPlane;
	m_Transform.FarPlane = farPlane;
	OnDirty();
}

void Camera::SetOrthographic(bool orthographic, float size)
{
	m_Transform.Perspective = !orthographic;
	if (orthographic)
	{
		m_Transform.OrthographicSize = size;
	}
	OnDirty();
}

void Camera::SetNearPlane(float nearPlane)
{
	m_Transform.NearPlane = nearPlane;
	OnDirty();
}

void Camera::SetFarPlane(float farPlane)
{
	m_Transform.FarPlane = farPlane;
	OnDirty();
}

void Camera::SetJitterWeight(float weight)
{
	m_Transform.JitterWeight = weight;
	OnDirty();
}

const Matrix& Camera::GetView() const
{
	UpdateMatrices();
	return m_Transform.View;
}

const Matrix& Camera::GetProjection() const
{
	UpdateMatrices();
	return m_Transform.Projection;
}

const Matrix& Camera::GetViewProjection() const
{
	UpdateMatrices();
	return m_Transform.ViewProjection;
}

Matrix Camera::GetViewProjectionInverse() const
{
	return GetProjectionInverse() * GetViewInverse();
}

const Matrix& Camera::GetViewInverse() const
{
	UpdateMatrices();
	return m_Transform.ViewInverse;
}

const Matrix& Camera::GetProjectionInverse() const
{
	UpdateMatrices();
	return m_Transform.ProjectionInverse;
}

const BoundingFrustum& Camera::GetFrustum() const
{
	UpdateMatrices();
	return m_Transform.Frustum;
}

void Camera::OnDirty()
{
	m_Dirty = true;
}

void Camera::UpdateMatrices() const
{
	if (m_Dirty)
	{
		if (m_UpdatePrevMatrices)
		{
			m_Transform.ViewProjectionFrozen = m_Transform.ViewProjection;
		}

		m_Transform.ViewInverse = Matrix::CreateFromQuaternion(m_Rotation) * Matrix::CreateTranslation(m_Position);
		m_Transform.ViewInverse.Invert(m_Transform.View);
		float aspect = m_Transform.Viewport.GetWidth() / m_Transform.Viewport.GetHeight();
		if (m_Transform.Perspective)
		{
			m_Transform.Projection = Math::CreatePerspectiveMatrix(m_Transform.FoV, aspect, m_Transform.NearPlane, m_Transform.FarPlane);
		}
		else
		{
			m_Transform.Projection = Math::CreateOrthographicMatrix(m_Transform.OrthographicSize * aspect, m_Transform.OrthographicSize, m_Transform.NearPlane, m_Transform.FarPlane);
		}

#if 0
		constexpr Math::HaltonSequence<16, 2> x;
		constexpr Math::HaltonSequence<16, 3> y;

		m_Transform.Jitter.x = m_Transform.JitterWeight * x[m_Transform.JitterIndex];
		m_Transform.Jitter.y = m_Transform.JitterWeight * y[m_Transform.JitterIndex];
		m_Transform.Projection.m[2][0] += (m_Transform.Jitter.x * 2.0f - 1.0f) / m_Transform.Viewport.GetWidth();
		m_Transform.Projection.m[2][1] += (m_Transform.Jitter.y * 2.0f - 1.0f) / m_Transform.Viewport.GetHeight();
#endif

		m_Transform.Projection.Invert(m_Transform.ProjectionInverse);
		m_Transform.ViewProjection = m_Transform.View * m_Transform.Projection;
		m_Transform.Frustum = Math::CreateBoundingFrustum(m_Transform.Projection, m_Transform.View);
		m_Transform.Position = m_Position;
		m_Dirty = false;
	}
}

void Camera::Update()
{
	m_Transform.PositionPrev = m_Transform.Position;
	m_Transform.ViewProjectionPrev = m_Transform.ViewProjection;
	m_Transform.PreviousJitter = m_Transform.Jitter;
	++m_Transform.JitterIndex;
}

void FreeCamera::Update()
{
	Camera::Update();

	Vector3 movement;
	if (Input::Instance().IsMouseDown(VK_RBUTTON))
	{
		if (!ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing())
		{
			Vector2 mouseDelta = Input::Instance().GetMouseDelta();
			Quaternion yr = Quaternion::CreateFromYawPitchRoll(0, mouseDelta.y * Time::DeltaTime() * 0.1f, 0);
			Quaternion pr = Quaternion::CreateFromYawPitchRoll(mouseDelta.x * Time::DeltaTime() * 0.1f, 0, 0);
			m_Rotation = yr * m_Rotation * pr;
		}

		movement.x -= (int)Input::Instance().IsKeyDown('A');
		movement.x += (int)Input::Instance().IsKeyDown('D');
		movement.z -= (int)Input::Instance().IsKeyDown('S');
		movement.z += (int)Input::Instance().IsKeyDown('W');
		movement.y -= (int)Input::Instance().IsKeyDown('Q');
		movement.y += (int)Input::Instance().IsKeyDown('E');
		movement = Vector3::Transform(movement, m_Rotation);
	}
	m_Velocity = Vector3::SmoothStep(m_Velocity, movement, 0.2f);

	m_Position += m_Velocity * Time::DeltaTime() * 4.0f;
	OnDirty();
}

Ray Camera::GetMouseRay() const
{
	Ray ray;
	Vector2 mousePos = Input::Instance().GetMousePosition();
	Vector2 ndc;
	float hw = (float)m_Transform.Viewport.GetWidth() / 2.0f;
	float hh = (float)m_Transform.Viewport.GetHeight() / 2.0f;
	ndc.x = (mousePos.x - hw) / hw;
	ndc.y = (hh - mousePos.y) / hh;

	Vector3 nearPoint, farPoint;
	Matrix viewProjInverse;
	m_Transform.ViewProjection.Invert(viewProjInverse);
	nearPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 1), viewProjInverse);
	farPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 0), viewProjInverse);
	ray.position = Vector3(nearPoint.x, nearPoint.y, nearPoint.z);

	ray.direction = farPoint - nearPoint;
	ray.direction.Normalize();
	return ray;
}
