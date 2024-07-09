#include "stdafx.h"
#include "Camera.h"
#include "Core/Input.h"

void Camera::ApplyViewTransform(ViewTransform& transform, bool jitter) const
{
	// Update previous data
	transform.PositionPrev = transform.Position;
	transform.ViewProjectionPrev = transform.ViewProjection;
	transform.JitterPrev = transform.Jitter;

	// Update current data
	transform.ViewInverse = Matrix::CreateFromQuaternion(m_Rotation) * Matrix::CreateTranslation(m_Position);
	transform.ViewInverse.Invert(transform.View);
	float aspect = transform.Viewport.GetWidth() / transform.Viewport.GetHeight();
	transform.Projection = Math::CreatePerspectiveMatrix(transform.FoV, aspect, transform.NearPlane, transform.FarPlane);
	transform.UnjtteredViewProjection = transform.View * transform.Projection;

	if (jitter)
	{
		constexpr Math::HaltonSequence<16, 2> x;
		constexpr Math::HaltonSequence<16, 3> y;

		transform.Jitter.x = (x[transform.JitterIndex] * 2.0f - 1.0f) / transform.Viewport.GetWidth();
		transform.Jitter.y = (y[transform.JitterIndex] * 2.0f - 1.0f) / transform.Viewport.GetHeight();
		transform.Projection.m[2][0] += transform.Jitter.x;
		transform.Projection.m[2][1] += transform.Jitter.y;
		++transform.JitterIndex;
	}
	else
	{
		transform.Jitter = Vector2::Zero;
	}

	transform.Projection.Invert(transform.ProjectionInverse);
	transform.ViewProjection = transform.View * transform.Projection;
	transform.PerspectiveFrustum = Math::CreateBoundingFrustum(transform.Projection, transform.View);
	transform.Position = m_Position;
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

Ray Camera::GetMouseRay(const FloatRect& viewport) const
{
	Ray ray;
	Vector2 mousePos = Input::Instance().GetMousePosition();
	Vector2 ndc;
	float hw = (float)viewport.GetWidth() / 2.0f;
	float hh = (float)viewport.GetHeight() / 2.0f;
	ndc.x = (mousePos.x - hw) / hw;
	ndc.y = (hh - mousePos.y) / hh;

	ViewTransform transform;
	ApplyViewTransform(transform, false);

	Matrix viewProjInverse;
	transform.ViewProjection.Invert(viewProjInverse);
	Vector3 nearPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 1), viewProjInverse);
	Vector3 farPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 0), viewProjInverse);
	ray.position = Vector3(nearPoint.x, nearPoint.y, nearPoint.z);

	ray.direction = farPoint - nearPoint;
	ray.direction.Normalize();
	return ray;
}
