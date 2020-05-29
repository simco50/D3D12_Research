#include "stdafx.h"
#include "MathHelp.h"

namespace Math
{
	float RandomRange(float min, float max)
	{
		float random = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		float diff = max - min;
		float r = random * diff;
		return min + r;
	}

	int RandomRange(int min, int max)
	{
		return min + rand() % (max - min + 1);
	}

	float Lerp(float a, float b, float t)
	{
		return a + t * (b - a);
	}

	float InverseLerp(float a, float b, float value)
	{
		return (value - a) / (b - a);
	}

	Matrix CreatePerspectiveMatrix(float FoV, float aspectRatio, float nearPlane, float farPlane)
	{
#ifdef WORLD_RIGHT_HANDED
		return DirectX::XMMatrixPerspectiveFovRH(FoV, aspectRatio, nearPlane, farPlane);
#else
		return DirectX::XMMatrixPerspectiveFovLH(FoV, aspectRatio, nearPlane, farPlane);
#endif
	}

	Matrix CreatePerspectiveOffCenterMatrix(float left, float right, float bottom, float top, float nearPlane, float farPlane)
	{
#ifdef WORLD_RIGHT_HANDED
		return DirectX::XMMatrixPerspectiveOffCenterRH(left, right, bottom, top, nearPlane, farPlane);
#else
		return DirectX::XMMatrixPerspectiveOffCenterLH(left, right, bottom, top, nearPlane, farPlane);
#endif
	}

	Matrix CreateOrthographicMatrix(float width, float height, float nearPlane, float farPlane)
	{
#ifdef WORLD_RIGHT_HANDED
		return DirectX::XMMatrixOrthographicRH(width, height, nearPlane, farPlane);
#else
		return DirectX::XMMatrixOrthographicLH(width, height, nearPlane, farPlane);
#endif
	}

	Matrix CreateOrthographicOffCenterMatrix(float left, float right, float bottom, float top, float nearPlane, float farPlane)
	{
#ifdef WORLD_RIGHT_HANDED
		return DirectX::XMMatrixOrthographicOffCenterRH(left, right, bottom, top, nearPlane, farPlane);
#else
		return DirectX::XMMatrixOrthographicOffCenterLH(left, right, bottom, top, nearPlane, farPlane);
#endif
	}

	void GetProjectionClipPlanes(const Matrix& projection, float& nearPlane, float& farPlane)
	{
		nearPlane = -projection._43 / projection._33;
		farPlane = nearPlane * projection._33 / (projection._33 - 1);
	}

	void ReverseZProjection(Matrix& projection)
	{
		float n, f;
		GetProjectionClipPlanes(projection, n, f);
		std::swap(n, f);
		projection._33 = f / (f - n);
		projection._43 = -projection._33 * n;
	}

	DirectX::SimpleMath::Vector3 ScaleFromMatrix(const Matrix& m)
	{
		return Vector3(
			sqrtf(m._11 * m._11 + m._21 * m._21 + m._31 * m._31),
			sqrtf(m._12 * m._12 + m._22 * m._22 + m._32 * m._32),
			sqrtf(m._13 * m._13 + m._23 * m._23 + m._33 * m._33));
	}

	DirectX::SimpleMath::Quaternion LookRotation(const Vector3& direction)
	{
		Vector3 v;
		direction.Normalize(v);
		float pitch = asin(-v.y);
		float yaw = atan2(v.x, v.z);
		return Quaternion::CreateFromYawPitchRoll(yaw, pitch, 0);
	}

	std::string ToBase(unsigned int number, unsigned int base)
	{
		std::stringstream nr;
		unsigned int count = 0;
		while (number != 0)
		{
			unsigned int mod = number % base;
			if (mod > 9)
			{
				nr << (char)('A' + mod - 10);
			}
			else
			{
				nr << mod;
			}
			number /= base;
			++count;
		}
		for (; count <= 8; ++count)
		{
			nr << '0';
		}
		if (base == 2)
		{
			nr << "b0";
		}
		else if (base == 8)
		{
			nr << "c0";
		}
		else if (base == 16)
		{
			nr << "x0";
		}
		std::string out = nr.str();
		std::reverse(out.begin(), out.end());
		return out;
	}

	std::string ToBinary(unsigned int number)
	{
		return Math::ToBase(number, 2);
	}

	std::string ToHex(unsigned int number)
	{
		return Math::ToBase(number, 16);
	}

	DirectX::SimpleMath::Vector3 RandVector()
	{
		Matrix randomMatrix = DirectX::XMMatrixRotationRollPitchYaw(Math::RandomRange(-PI, PI), Math::RandomRange(-PI, PI), Math::RandomRange(-PI, PI));
		return Vector3::Transform(Vector3(1, 0, 0), randomMatrix);
	}

	DirectX::SimpleMath::Vector3 RandCircleVector()
	{
		Vector3 output;
		output.z = 0;
		output.y = cos(RandomRange(-PI, PI));
		output.x = sin(RandomRange(-PI, PI));
		return output;
	}

	Color MakeFromColorTemperature(float Temp)
	{
		Temp = Clamp(Temp, 15000.0f, 1000.0f);

		//[Krystek85] Algorithm works in the CIE 1960 (UCS) space,
		float u = (0.860117757f + 1.54118254e-4f * Temp + 1.28641212e-7f * Temp * Temp) / (1.0f + 8.42420235e-4f * Temp + 7.08145163e-7f * Temp * Temp);
		float v = (0.317398726f + 4.22806245e-5f * Temp + 4.20481691e-8f * Temp * Temp) / (1.0f - 2.89741816e-5f * Temp + 1.61456053e-7f * Temp * Temp);

		//UCS to xyY
		float x = 3.0f * u / (2.0f * u - 8.0f * v + 4.0f);
		float y = 2.0f * v / (2.0f * u - 8.0f * v + 4.0f);
		float z = 1.0f - x - y;

		//xyY to XYZ
		float Y = 1.0f;
		float X = Y / y * x;
		float Z = Y / y * z;

		// XYZ to RGB - BT.709
		float R = 3.2404542f * X + -1.5371385f * Y + -0.4985314f * Z;
		float G = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
		float B = 0.0556434f * X + -0.2040259f * Y + 1.0572252f * Z;

		return Color(R, G, B);
	}

}