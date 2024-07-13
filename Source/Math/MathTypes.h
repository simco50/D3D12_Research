 #pragma once

#include <SimpleMath.h>
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

template<typename T>
struct TVector2
{
	union
	{
		struct
		{
			T x, y;
		};
		T Values[2];
	};

	constexpr TVector2()
		: x(0), y(0)
	{}

	constexpr TVector2(T x, T y)
		: x(x), y(y)
	{}

	constexpr TVector2(T v)
		: x(v), y(v)
	{}

	constexpr TVector2(const T* pData)
		: x(pData[0]), y(pData[1])
	{}

	constexpr TVector2(const Vector2& v)
		: x((T)v.x), y((T)v.y)
	{}

	const T& operator[](int index) const { return Values[index]; }
	T& operator[](int index) { return Values[index]; }

	explicit operator Vector2() const { return Vector2((float)x, (float)y); }

	static TVector2 Zero() { return TVector2(0, 0); }
	static TVector2 One() { return TVector2(1, 1); }

	bool operator==(const TVector2& rhs) const { return x == rhs.x && y == rhs.y; }
	bool operator!=(const TVector2& rhs) const { return !operator==(rhs); }
};

template<typename T>
struct TVector3
{
	union
	{
		struct
		{
			T x, y, z;
		};
		T Values[3];
	};

	constexpr TVector3()
		: x(0), y(0), z(0)
	{}

	constexpr TVector3(T x, T y, T z)
		: x(x), y(y), z(z)
	{}

	constexpr TVector3(T v)
		: x(v), y(v), z(v)
	{}

	constexpr TVector3(const T* pData)
		: x(pData[0]), y(pData[1]), z(pData[2])
	{}

	constexpr TVector3(const Vector3& v)
		: x((T)v.x), y((T)v.y), z((T)v.z)
	{}

	const T& operator[](int index) const { return Values[index]; }
	T& operator[](int index) { return Values[index]; }

	explicit operator Vector3() const { return Vector3((float)x, (float)y, (float)z); }

	static TVector3 Zero() { return TVector3(0, 0, 0); }
	static TVector3 One() { return TVector3(1, 1, 1); }

	bool operator==(const TVector3& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
	bool operator!=(const TVector3& rhs) const { return !operator==(rhs); }
};

template<typename T>
struct TVector4
{
	union
	{
		struct
		{
			T x, y, z, w;
		};
		T Values[4];
	};

	constexpr TVector4()
		: x(0), y(0), z(0), w(0)
	{}

	constexpr TVector4(T x, T y, T z, T w)
		: x(x), y(y), z(z), w(w)
	{}

	constexpr TVector4(T v)
		: x(v), y(v), z(v), w(v)
	{}

	constexpr TVector4(const T* pData)
		: x(pData[0]), y(pData[1]), z(pData[2]), w(pData[3])
	{}

	constexpr TVector4(const Vector4& v)
		: x((T)v.x), y((T)v.y), z((T)v.z), w((T)v.w)
	{}

	const T& operator[](int index) const { return Values[index]; }
	T& operator[](int index) { return Values[index]; }

	explicit operator Vector4() const { return Vector4((float)x, (float)y, (float)z, (float)w); }

	static TVector4 Zero() { return TVector4(0, 0, 0, 0); }
	static TVector4 One() { return TVector4(1, 1, 1, 1); }

	bool operator==(const TVector4& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w; }
	bool operator!=(const TVector4& rhs) const { return !operator==(rhs); }
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

using Vector2i = TVector2<int32>;
using Vector3i = TVector3<int32>;
using Vector4i = TVector4<int32>;

using Vector2u = TVector2<uint32>;
using Vector3u = TVector3<uint32>;
using Vector4u = TVector4<uint32>;

// An exclusive range
template<typename T>
struct TRange
{
	TRange()
		: Begin(0), End(0)
	{}

	TRange(T begin, T end)
		: Begin(begin), End(end)
	{
		gAssert(begin <= end);
	}

	static bool Overlaps(const TRange& lhs, const TRange& rhs)
	{
		gAssert(lhs.Begin < lhs.End);
		gAssert(rhs.Begin < rhs.End);
		return lhs.Begin <= rhs.End && lhs.End >= rhs.Begin;
	}

	static bool Combine(const TRange& lhs, const TRange& rhs, TRange& combinedRange)
	{
		if (!Overlaps(lhs, rhs))
			return false;
		return TRange(
			lhs.Begin < rhs.Begin ? lhs.Begin : rhs.Begin,
			lhs.End > rhs.End ? lhs.End : rhs.End);
	}

	bool Overlaps(const TRange& rhs) const
	{
		return Overlaps(*this, rhs);
	}

	T GetLength() const { gAssert(Begin <= End); return End - Begin; }

	T Begin;
	T End;
};

using IRange = TRange<int>;
using URange = TRange<uint32>;
using FRange = TRange<float>;
