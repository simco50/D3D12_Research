#include "stdafx.h"
#include "Math.h"

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

	float Lerp(float t, float a, float b)
	{
		return a + t * (b - a);
	}

	float InverseLerp(float value, float rangeMin, float rangeMax)
	{
		return (value - rangeMin) / (rangeMax - rangeMin);
	}

	float RemapRange(float value, float sourceRangeMin, float sourceRangeMax, float targetRangeMin, float targetRangeMax)
	{
		value = Clamp(value, sourceRangeMin, sourceRangeMax);
		float t = InverseLerp(value, sourceRangeMin, sourceRangeMax);
		return Lerp(t, targetRangeMin, targetRangeMax);
	}

	// Create left-handed DX style perspective matrix
	// FoV is vertical FoV in radians
	Matrix CreatePerspectiveMatrix(float FoV, float aspectRatio, float nearZ, float farZ)
	{
		float sinFov = sinf(FoV * 0.5f);
		float cosFov = cosf(FoV * 0.5f);

		float B = cosFov / sinFov;
		float A = B / aspectRatio;
		float C = farZ / (farZ - nearZ);
		float D = 1.0f; // Needs to be -1 for right handed
		float E = -nearZ * C; // Positive in right handed

		return Matrix(
			A, 0, 0, 0,
			0, B, 0, 0,
			0, 0, C, D,
			0, 0, E, 0
		);
	}

	// Create left-handed DX style perspective off center matrix
	// FoV is vertical FoV in radians
	Matrix CreatePerspectiveOffCenterMatrix(float left, float right, float bottom, float top, float nearZ, float farZ)
	{
		Matrix m;
		float near2 = nearZ * nearZ;
		float oneOverWidth = 1.0f / (right - left);
		float oneOverHeight = 1.0f / (bottom - top);

		float A = near2 * oneOverWidth;
		float B = near2 * oneOverHeight;
		float C = farZ / (farZ - nearZ);
		float D = 1.0f; // Needs to be -1 for right handed
		float E = -nearZ * C; // Positive in right handed

		float F = -(left + right) * oneOverWidth; // Positive in right handed
		float G = -(top + bottom) * oneOverHeight; // Positive in right handed

		return Matrix(
			A,	0,	0,	0,
			0,	B,	0,	0,
			F,	G,	C,	D,
			0,	0,	E,	0
		);
	}

	Matrix CreateOrthographicMatrix(float width, float height, float nearZ, float farZ)
	{
		return Matrix(
			2.0f / width,	0,				0,							0,
			0,				2.0f / height,	0,							0,
			0,				0,				1.0f / (farZ - nearZ),		0,
			0,				0,				-nearZ / (farZ - nearZ),	1
		);
	}

	Matrix CreateOrthographicOffCenterMatrix(float left, float right, float bottom, float top, float nearZ, float farZ)
	{
		float rcpWidth = 1.0f / (right - left);
		float rcpHeight = 1.0f / (top - bottom);
		float rcpZRange = 1.0f / (farZ - nearZ);

		float A = -(left + right) * rcpWidth;
		float B = -(top + bottom) * rcpHeight;

		return Matrix(
			2.0f * rcpWidth,	0,					0,						0,
			0,					2.0f * rcpHeight,	0,						0,
			0,					0,					rcpZRange,				0,
			A,					B,					-nearZ * rcpZRange,		1
		);
	}

	Matrix CreateLookToMatrix(const Vector3& position, const Vector3& direction, const Vector3& up)
	{
		Vector3 z;
		direction.Normalize(z);
		Vector3 x = up.Cross(z);
		x.Normalize();
		Vector3 y = z.Cross(x);

		Vector3 p(
			x.Dot(-position),
			y.Dot(-position),
			z.Dot(-position)
		);

		return Matrix(
			x.x, y.x, z.x, 0,
			x.y, y.y, z.y, 0,
			x.z, y.z, z.z, 0,
			p.x, p.y, p.z, 1
		);
	}

	BoundingFrustum CreateBoundingFrustum(const Matrix& projection, const Matrix& view)
	{
		BoundingFrustum frustum;
		BoundingFrustum::CreateFromMatrix(frustum, projection);
		if (frustum.Far < frustum.Near)
			std::swap(frustum.Far, frustum.Near);
		frustum.Transform(frustum, view.Invert());
		return frustum;
	}

	void GetProjectionClipPlanes(const Matrix& projection, float& nearZ, float& farZ)
	{
		nearZ = -projection._43 / projection._33;
		farZ = nearZ * projection._33 / (projection._33 - 1);
	}

	void ReverseZProjection(Matrix& projection)
	{
		float n, f;
		GetProjectionClipPlanes(projection, n, f);
		std::swap(n, f);
		projection._33 = f / (f - n);
		projection._43 = -projection._33 * n;
	}

	Vector3 ScaleFromMatrix(const Matrix& m)
	{
		return Vector3(
			sqrtf(m._11 * m._11 + m._21 * m._21 + m._31 * m._31),
			sqrtf(m._12 * m._12 + m._22 * m._22 + m._32 * m._32),
			sqrtf(m._13 * m._13 + m._23 * m._23 + m._33 * m._33));
	}

	Quaternion LookRotation(const Vector3& direction, const Vector3& up)
	{
		Matrix m = CreateLookToMatrix(Vector3::Zero, direction, up);
		return Quaternion::CreateFromRotationMatrix(m);
	}

	String ToBase(unsigned int number, unsigned int base, bool addPrefix /*= true*/)
	{
		char buffer[16];
		memset(buffer, 0, 16);
		char* pCurrent = buffer;
		uint32 count = 0;
		while (number != 0)
		{
			unsigned int mod = number % base;
			if (mod > 9)
			{
				*pCurrent++ = (char)('A' + mod - 10);
			}
			else
			{
				*pCurrent++ = '0' + (char)mod;
			}
			number /= base;
			++count;
		}
		constexpr uint32 minPadding = 8;
		for (; count <= minPadding; ++count)
		{
			*pCurrent++ = '0';
		}
		if (addPrefix)
		{
			if (base == 2)
			{
				*pCurrent++ = 'b';
				*pCurrent = '0';
			}
			else if (base == 8)
			{
				*pCurrent++ = 'c';
				*pCurrent = '0';
			}
			else if (base == 16)
			{
				*pCurrent++ = 'x';
				*pCurrent = '0';
			}
		}
		String out = buffer;
		std::reverse(out.begin(), out.end());
		return out;
	}

	Vector3 RandVector()
	{
		Matrix randomMatrix = DirectX::XMMatrixRotationRollPitchYaw(Math::RandomRange(-PI, PI), Math::RandomRange(-PI, PI), Math::RandomRange(-PI, PI));
		return Vector3::Transform(Vector3(1, 0, 0), randomMatrix);
	}

	Vector3 RandCircleVector()
	{
		Vector3 output;
		output.z = 0;
		output.y = cos(RandomRange(-PI, PI));
		output.x = sin(RandomRange(-PI, PI));
		return output;
	}

	Color MakeFromColorTemperature(float Temp)
	{
		constexpr float MAX_TEMPERATURE = 15000.0f;
		constexpr float MIN_TEMPERATURE = 1000.0f;
		Temp = Clamp(Temp, MIN_TEMPERATURE, MAX_TEMPERATURE);

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
