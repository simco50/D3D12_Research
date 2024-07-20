#include "stdafx.h"
#include "Mesh.h"

Vector4 AnimationChannel::Evaluate(float time) const
{
	auto it = std::lower_bound(KeyFrames.begin(), KeyFrames.end(), time);
	if (it == KeyFrames.begin())
		return Data.front();
	if (it == KeyFrames.end())
		return Data.back();

	int i = (int)(it - KeyFrames.begin());

	if (Interpolation == Interpolation::Linear)
	{
		float t = Math::InverseLerp(time, KeyFrames[i - 1], KeyFrames[i]);
		if (Path == PathType::Rotation)
			return Quaternion::Slerp(GetVertex(i - 1), GetVertex(i), t);
		return Vector4::Lerp(GetVertex(i - 1), GetVertex(i), t);
	}
	else if (Interpolation == Interpolation::Step)
	{
		return GetVertex(i - 1);
	}
	else if (Interpolation == Interpolation::Cubic)
	{
		float dt = KeyFrames[i] - KeyFrames[i - 1];
		float t = (time - KeyFrames[i - 1]) / dt;
		Vector4 prevTan = GetInTangent(i - 1) * dt;
		Vector4 nextTan = GetOutTangent(i) * dt;
		return Vector4::Hermite(GetVertex(i - 1), prevTan, GetVertex(i), nextTan, t);
	}
	gUnreachable();
	return Vector4::Zero;
}
