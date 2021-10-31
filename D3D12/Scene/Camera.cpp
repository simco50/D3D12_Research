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
	m_Viewport = rect;
	OnDirty();
}

void Camera::SetFoV(float fov)
{
	m_FoV = fov;
	OnDirty();
}

void Camera::SetClippingPlanes(float nearPlane, float farPlane)
{
	m_NearPlane = nearPlane;
	m_FarPlane = farPlane;
	OnDirty();
}

void Camera::SetOrthographic(bool orthographic, float size)
{
	m_Perspective = !orthographic;
	if (orthographic)
	{
		m_OrthographicSize = size;
	}
	OnDirty();
}

void Camera::SetNearPlane(float nearPlane)
{
	m_NearPlane = nearPlane;
	OnDirty();
}

void Camera::SetFarPlane(float farPlane)
{
	m_FarPlane = farPlane;
	OnDirty();
}

void Camera::SetJitterWeight(float weight)
{
	m_JitterWeight = weight;
	OnDirty();
}

const Matrix& Camera::GetView() const
{
	UpdateMatrices();
	return m_View;
}

const Matrix& Camera::GetProjection() const
{
	UpdateMatrices();
	return m_Projection;
}

const Matrix& Camera::GetViewProjection() const
{
	UpdateMatrices();
	return m_ViewProjection;
}

const Matrix Camera::GetViewProjectionInverse() const
{
	return GetProjectionInverse() * GetViewInverse();
}

const Matrix& Camera::GetViewInverse() const
{
	UpdateMatrices();
	return m_ViewInverse;
}

const Matrix& Camera::GetProjectionInverse() const
{
	UpdateMatrices();
	return m_ProjectionInverse;
}

const BoundingFrustum& Camera::GetFrustum() const
{
	UpdateMatrices();
	return m_Frustum;
}

void Camera::OnDirty()
{
	m_Dirty = true;
}

void Camera::UpdateMatrices() const
{
	if (m_Dirty)
	{
		m_ViewInverse = Matrix::CreateFromQuaternion(m_Rotation) * Matrix::CreateTranslation(m_Position);
		m_ViewInverse.Invert(m_View);
		float aspect = m_Viewport.GetWidth() / m_Viewport.GetHeight();
		if (m_Perspective)
		{
			m_Projection = Math::CreatePerspectiveMatrix(m_FoV, aspect, m_NearPlane, m_FarPlane);
		}
		else
		{
			m_Projection = Math::CreateOrthographicMatrix(m_OrthographicSize * aspect, m_OrthographicSize, m_NearPlane, m_FarPlane);
		}

#if 0
		constexpr Math::HaltonSequence<8, 2> x;
		constexpr Math::HaltonSequence<8, 3> y;

		m_Jitter.x = m_JitterWeight * x[m_JitterIndex];
		m_Jitter.y = m_JitterWeight * y[m_JitterIndex];
		m_Projection.m[3][0] += (m_Jitter.x * 2.0f - 1.0f) / m_Viewport.GetWidth();
		m_Projection.m[3][1] += (m_Jitter.y * 2.0f - 1.0f) / m_Viewport.GetHeight();
#endif

		m_Projection.Invert(m_ProjectionInverse);
		m_ViewProjection = m_View * m_Projection;
		m_Dirty = false;

		Matrix p = m_Projection;
		if (m_FarPlane < m_NearPlane)
		{
			Math::ReverseZProjection(p);
		}
		BoundingFrustum::CreateFromMatrix(m_Frustum, p);
		m_Frustum.Transform(m_Frustum, m_ViewInverse);
	}
}

void Camera::Update()
{
	m_PreviousViewProjection = GetViewProjection();
	m_PreviousJitter = m_Jitter;
	++m_JitterIndex;
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

Ray Camera::GetMouseRay(uint32 windowWidth, uint32 windowHeight) const
{
	Ray ray;
	Vector2 mousePos = Input::Instance().GetMousePosition();
	Vector2 ndc;
	float hw = (float)windowWidth / 2.0f;
	float hh = (float)windowHeight / 2.0f;
	ndc.x = (mousePos.x - hw) / hw;
	ndc.y = (hh - mousePos.y) / hh;

	Vector3 nearPoint, farPoint;
	Matrix viewProjInverse;
	m_ViewProjection.Invert(viewProjInverse);
	nearPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 1), viewProjInverse);
	farPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 0), viewProjInverse);
	ray.position = Vector3(nearPoint.x, nearPoint.y, nearPoint.z);

	ray.direction = farPoint - nearPoint;
	ray.direction.Normalize();
	return ray;
}
