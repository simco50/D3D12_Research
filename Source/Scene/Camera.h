#pragma once

#include "Renderer/Renderer.h"

struct Transform;

class Camera
{
public:
	static void UpdateMovement(Transform& transform, Camera& camera);
	static Ray GetMouseRay(const FloatRect& viewport, const Matrix& clipToWorld);

	float FOV = 60.0f * Math::PI / 180;
	Vector3 Velocity;
};
