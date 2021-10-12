 #pragma once

#define __d3d12_h__
#include "SimpleMath/SimpleMath.h"
using BoundingBox = DirectX::BoundingBox;
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

template<typename T>
struct TIntVector2
{
	TIntVector2()
		: x(0), y(0)
	{}
	TIntVector2(T x, T y)
		: x(x), y(y)
	{}
	TIntVector2(const Vector2& v)
		: x((T)v.x), y((T)v.y)
	{}
	T x, y;
};

template<typename T>
struct TIntVector3
{
	TIntVector3()
		: x(0), y(0), z(0)
	{}
	TIntVector3(T x, T y, T z)
		: x(x), y(y), z(z)
	{}
	TIntVector3(const Vector3& v)
		: x((T)v.x), y((T)v.y), z((T)v.z)
	{}
	T x, y, z;
};

template<typename T>
struct TIntVector4
{
	TIntVector4()
		: x(0), y(0), z(0), w(0)
	{}
	TIntVector4(T x, T y, T z, T w)
		: x(x), y(y), z(z), w(w)
	{}
	TIntVector4(const Vector4& v)
		: x((T)v.x), y((T)v.y), z((T)v.z), w((T)v.w)
	{}
	T x, y, z, w;
};

using IntVector2 = TIntVector2<int32>;
using IntVector3 = TIntVector3<int32>;
using IntVector4 = TIntVector4<int32>;

template<typename T>
struct RectT
{
	RectT()
		: Left(T()), Top(T()), Right(T()), Bottom(T())
	{}

	RectT(const T left, const T top, const T right, const T bottom)
		: Left(left), Top(top), Right(right), Bottom(bottom)
	{}

	template<typename U>
	RectT(const RectT<U>& other)
		: Left((T)other.Left), Top((T)other.Top), Right((T)other.Right), Bottom((T)other.Bottom)
	{

	}

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

	RectT Scale(const float scale) const
	{
		return RectT(Left * scale, Top * scale, Right * scale, Bottom * scale);
	}

	RectT Scale(const float scaleX, const float scaleY) const
	{
		return RectT(Left * scaleX, Top * scaleY, Right * scaleX, Bottom * scaleY);
	}

	bool operator==(const RectT& other) const
	{
		return Left == other.Left && Top == other.Top && Right == other.Right && Bottom == other.Bottom;
	}

	bool operator!=(const RectT& other) const
	{
		return Left != other.Left || Top != other.Top || Right != other.Right || Bottom != other.Bottom;
	}

	static RectT ZERO()
	{
		return RectT();
	}
};

using FloatRect = RectT<float>;
using IntRect = RectT<int>;
