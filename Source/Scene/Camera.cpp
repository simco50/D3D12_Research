#include "stdafx.h"
#include "Camera.h"
#include "Core/Input.h"
#include "Scene/World.h"

void Camera::UpdateMovement(Transform& transform, Camera& camera)
{
	Vector3 movement;
	if (Input::Instance().IsMouseDown(VK_RBUTTON))
	{
		if (!ImGui::IsAnyItemActive())
		{
			Vector2 mouseDelta = Input::Instance().GetMouseDelta();
			Quaternion yr = Quaternion::CreateFromYawPitchRoll(0, mouseDelta.y * Time::DeltaTime() * 0.1f, 0);
			Quaternion pr = Quaternion::CreateFromYawPitchRoll(mouseDelta.x * Time::DeltaTime() * 0.1f, 0, 0);
			transform.Rotation = yr * transform.Rotation * pr;
		}

		movement.x -= (int)Input::Instance().IsKeyDown('A');
		movement.x += (int)Input::Instance().IsKeyDown('D');
		movement.z -= (int)Input::Instance().IsKeyDown('S');
		movement.z += (int)Input::Instance().IsKeyDown('W');
		movement.y -= (int)Input::Instance().IsKeyDown('Q');
		movement.y += (int)Input::Instance().IsKeyDown('E');
		movement = Vector3::Transform(movement, transform.Rotation);
	}
	camera.Velocity = Vector3::SmoothStep(camera.Velocity, movement, 0.2f);

	transform.Position += camera.Velocity * Time::DeltaTime() * 4.0f;
}

Ray Camera::GetMouseRay(const FloatRect& viewport, const Matrix& clipToWorld)
{
	Ray ray;
	Vector2 mousePos = Input::Instance().GetMousePosition();
	Vector2 ndc;
	float hw = (float)viewport.GetWidth() / 2.0f;
	float hh = (float)viewport.GetHeight() / 2.0f;
	ndc.x = (mousePos.x - hw) / hw;
	ndc.y = (hh - mousePos.y) / hh;

	Vector3 nearPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 1), clipToWorld);
	Vector3 farPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 0), clipToWorld);
	ray.position = Vector3(nearPoint.x, nearPoint.y, nearPoint.z);

	ray.direction = farPoint - nearPoint;
	ray.direction.Normalize();
	return ray;
}
