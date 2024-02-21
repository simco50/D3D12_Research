#include "stdafx.h"
#include "Camera.h"
#include "Core/Input.h"

void Camera::Update()
{
	// Update previous data
	m_Transform.PositionPrev = m_Transform.Position;
	m_Transform.ViewProjectionPrev = m_Transform.ViewProjection;
	m_Transform.JitterPrev = m_Transform.Jitter;
	++m_Transform.JitterIndex;

	// Update current data
	m_Transform.ViewInverse = Matrix::CreateFromQuaternion(m_Rotation) * Matrix::CreateTranslation(m_Position);
	m_Transform.ViewInverse.Invert(m_Transform.View);
	float aspect = m_Transform.Viewport.GetWidth() / m_Transform.Viewport.GetHeight();
	m_Transform.Projection = Math::CreatePerspectiveMatrix(m_Transform.FoV, aspect, m_Transform.NearPlane, m_Transform.FarPlane);
	m_Transform.UnjtteredViewProjection = m_Transform.View * m_Transform.Projection;

	if (m_Jitter)
	{
		constexpr Math::HaltonSequence<16, 2> x;
		constexpr Math::HaltonSequence<16, 3> y;

		m_Transform.Jitter.x = (x[m_Transform.JitterIndex] * 2.0f - 1.0f) / m_Transform.Viewport.GetWidth();
		m_Transform.Jitter.y = (y[m_Transform.JitterIndex] * 2.0f - 1.0f) / m_Transform.Viewport.GetHeight();
		m_Transform.Projection.m[2][0] += m_Transform.Jitter.x;
		m_Transform.Projection.m[2][1] += m_Transform.Jitter.y;
	}
	else
	{
		m_Transform.Jitter = Vector2::Zero;
	}

	m_Transform.Projection.Invert(m_Transform.ProjectionInverse);
	m_Transform.ViewProjection = m_Transform.View * m_Transform.Projection;
	m_Transform.PerspectiveFrustum = Math::CreateBoundingFrustum(m_Transform.Projection, m_Transform.View);
	m_Transform.Position = m_Position;
}

void FreeCamera::Update()
{
	Vector3 movement;
	if (Input::Instance().IsMouseDown(VK_RBUTTON))
	{
		if (!ImGui::IsAnyItemActive())
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

	Camera::Update();
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

	Matrix viewProjInverse;
	m_Transform.ViewProjection.Invert(viewProjInverse);
	Vector3 nearPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 1), viewProjInverse);
	Vector3 farPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 0), viewProjInverse);
	ray.position = Vector3(nearPoint.x, nearPoint.y, nearPoint.z);

	ray.direction = farPoint - nearPoint;
	ray.direction.Normalize();
	return ray;
}
