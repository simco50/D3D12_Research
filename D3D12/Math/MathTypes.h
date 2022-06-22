 #pragma once

#define __d3d12_h__
#include "SimpleMath.h"
using BoundingBox = DirectX::BoundingBox;
using OrientedBoundingBox = DirectX::BoundingOrientedBox;
using BoundingFrustum = DirectX::BoundingFrustum;
using BoundingSphere = DirectX::BoundingSphere;
using Vector2 = DirectX::SimpleMath::Vector2;
using Vector3 = DirectX::SimpleMath::Vector3;
using Vector4 = DirectX::SimpleMath::Vector4;
using Matrix = DirectX::SimpleMath::Matrix;
using Quaternion = DirectX::SimpleMath::Quaternion;
using Color = DirectX::SimpleMath::Color;
using Ray = DirectX::SimpleMath::Ray;

#include <DirectXPackedVector.h>
using PackedVector2 = DirectX::PackedVector::XMHALF2;
using PackedVector3 = DirectX::PackedVector::XMHALF4;
using PackedVector4 = DirectX::PackedVector::XMHALF4;

#pragma warning(push)
#pragma warning(disable: 4201) // nonstandard extension used: nameless struct/union

struct Vector2i
{
	union
	{
		struct
		{
			int32 x, y;
		};
		int32 Values[2];
	};

	Vector2i()
		: x(0), y(0)
	{}

	Vector2i(int32 x, int32 y)
		: x(x), y(y)
	{}

	Vector2i(int32 v)
		: x(v), y(v)
	{}

	Vector2i(const int32* pData)
		: x(pData[0]), y(pData[1])
	{}

	Vector2i(const Vector2& v)
		: x((int32)v.x), y((int32)v.y)
	{}

	const int32& operator[](int index) const { return Values[index]; }
	int32& operator[](int index) { return Values[index]; }

	explicit operator Vector2() const { return Vector2((float)x, (float)y); }

	static Vector2i Zero() { return Vector2i(0, 0); }
	static Vector2i One() { return Vector2i(1, 1); }

	bool operator==(const Vector2i& rhs) const { return x == rhs.x && y == rhs.y; }
	bool operator!=(const Vector2i& rhs) const { return !operator==(rhs); }
};

struct Vector3i
{
	union
	{
		struct
		{
			int32 x, y, z;
		};
		int32 Values[3];
	};

	Vector3i()
		: x(0), y(0), z(0)
	{}

	Vector3i(int32 x, int32 y, int32 z)
		: x(x), y(y), z(z)
	{}

	Vector3i(int32 v)
		: x(v), y(v), z(v)
	{}

	Vector3i(const int32* pData)
		: x(pData[0]), y(pData[1]), z(pData[2])
	{}

	Vector3i(const Vector3& v)
		: x((int32)v.x), y((int32)v.y), z((int32)v.z)
	{}

	const int32& operator[](int index) const { return Values[index]; }
	int32& operator[](int index) { return Values[index]; }

	explicit operator Vector3() const { return Vector3((float)x, (float)y, (float)z); }

	static Vector3i Zero() { return Vector3i(0, 0, 0); }
	static Vector3i One() { return Vector3i(1, 1, 1); }

	bool operator==(const Vector3i& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
	bool operator!=(const Vector3i& rhs) const { return !operator==(rhs); }
};

struct Vector4i
{
	union
	{
		struct
		{
			int32 x, y, z, w;
		};
		int32 Values[4];
	};

	Vector4i()
		: x(0), y(0), z(0), w(0)
	{}

	Vector4i(int32 x, int32 y, int32 z, int32 w)
		: x(x), y(y), z(z), w(w)
	{}

	Vector4i(int32 v)
		: x(v), y(v), z(v), w(v)
	{}

	Vector4i(const int32* pData)
		: x(pData[0]), y(pData[1]), z(pData[2]), w(pData[3])
	{}

	Vector4i(const Vector4& v)
		: x((int32)v.x), y((int32)v.y), z((int32)v.z), w((int32)v.w)
	{}

	const int32& operator[](int index) const { return Values[index]; }
	int32& operator[](int index) { return Values[index]; }

	explicit operator Vector4() const { return Vector4((float)x, (float)y, (float)z, (float)w); }

	static Vector4i Zero() { return Vector4i(0, 0, 0, 0); }
	static Vector4i One() { return Vector4i(1, 1, 1, 1); }

	bool operator==(const Vector4i& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w; }
	bool operator!=(const Vector4i& rhs) const { return !operator==(rhs); }
};

template<typename T>
struct TRect
{
	TRect()
		: Left(T()), Top(T()), Right(T()), Bottom(T())
	{}

	TRect(const T left, const T top, const T right, const T bottom)
		: Left(left), Top(top), Right(right), Bottom(bottom)
	{}

	template<typename U>
	TRect(const TRect<U>& other)
		: Left((T)other.Left), Top((T)other.Top), Right((T)other.Right), Bottom((T)other.Bottom)
	{}

	T Left;
	T Top;
	T Right;
	T Bottom;

	T GetWidth() const { return Right - Left; }
	T GetHeight() const { return Bottom - Top; }
	T GetAspect() const
	{
		return GetWidth() / GetHeight();
	}

	TRect Scale(const float scale) const
	{
		return TRect(Left * scale, Top * scale, Right * scale, Bottom * scale);
	}

	TRect Scale(const float scaleX, const float scaleY) const
	{
		return TRect(Left * scaleX, Top * scaleY, Right * scaleX, Bottom * scaleY);
	}

	bool operator==(const TRect& other) const
	{
		return Left == other.Left && Top == other.Top && Right == other.Right && Bottom == other.Bottom;
	}

	bool operator!=(const TRect& other) const
	{
		return Left != other.Left || Top != other.Top || Right != other.Right || Bottom != other.Bottom;
	}
};

using FloatRect = TRect<float>;
using IntRect = TRect<int>;

#pragma warning(pop)
