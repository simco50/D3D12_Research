#pragma once

namespace Math
{
	constexpr float PI = 3.141592654f;
	constexpr float INVPI = 0.318309886f;
	constexpr float INV2PI = 0.159154943f;
	constexpr float PIDIV2 = 1.570796327f;
	constexpr float PIDIV4 = 0.785398163f;

	constexpr float ToDegrees = 180.0f / PI;
	constexpr float ToRadians = PI / 180.0f;

	template<typename T>
	constexpr T Max(const T& a, const T& b)
	{
		return a < b ? b : a;
	}

	template<typename T>
	constexpr T Min(const T& a, const T& b)
	{
		return a < b ? a : b;
	}

	template<typename T>
	constexpr T Max3(const T& a, const T& b, const T& c)
	{
		if (a < b)
			return b < c ? c : b;
		return a < c ? c : b;
	}

	template<typename T>
	constexpr T Min3(const T& a, const T& b, const T& c)
	{
		if (a < b)
			return a < c ? a : c;
		return b < c ? b : c;
	}

	float RandomRange(float min, float max);

	int RandomRange(int min, int max);

	template<typename T>
	constexpr T Clamp(const T value, const T hi, const T lo)
	{
		if (value > hi)
			return hi;
		else if (value < lo)
			return lo;
		return value;
	}

	template<typename T>
	constexpr void ClampMin(T& value, const T lo)
	{
		if (value < lo)
			value = lo;
	}

	template<typename T>
	constexpr T ClampMin(const T value, const T lo)
	{
		return value < lo ? lo : value;
	}

	template<typename T>
	constexpr void ClampMax(T& value, const T hi)
	{
		if (value > hi)
			value = hi;
	}

	template<typename T>
	constexpr T ClampMax(const T value, const T hi)
	{
		return value > hi ? hi : value;
	}

	template<typename T>
	constexpr T Average(const T& a, const T& b)
	{
		return (a + b) / (T)2;
	}

	template<typename T>
	constexpr T Average3(const T& a, const T& b, const T& c)
	{
		return (a + b + c) / (T)3;
	}

	template<typename T>
	constexpr void Clamp01(T& value)
	{
		if (value > 1)
			value = 1;
		else if (value < 0)
			value = 0;
	}

	template<typename T>
	T Clamp01(const T value)
	{
		if (value > 1)
			return 1;
		else if (value < 0)
			return 0;
		return value;
	}

	float Lerp(float a, float b, float t);

	float InverseLerp(float a, float b, float value);

	Matrix CreatePerspectiveMatrix(float FoV, float aspectRatio, float nearPlane, float farPlane);
	Matrix CreatePerspectiveOffCenterMatrix(float left, float right, float bottom, float top, float nearPlane, float farPlane);
	Matrix CreateOrthographicMatrix(float width, float height, float nearPlane, float farPlane);
	Matrix CreateOrthographicOffCenterMatrix(float left, float right, float bottom, float top, float nearPlane, float farPlane);

	Vector3 ScaleFromMatrix(const Matrix& m);

	Quaternion LookRotation(const Vector3& direction);

	std::string ToBase(unsigned int number, unsigned int base);

	std::string ToBinary(unsigned int number);

	std::string ToHex(unsigned int number);

	Vector3 RandVector();

	Vector3 RandCircleVector();

	using HexColor = unsigned int;
	//Helper class to easily convert between 4 float colors and unsigned int hex colors
	struct HexColorConverter
	{
		Color operator()(HexColor color) const
		{
			Color output;
			//unsigned int layout: AAAA RRRR GGGG BBBB
			output.x = (float)((color >> 16) & 0xFF) / 255.0f;
			output.y = (float)((color >> 8) & 0xFF) / 255.0f;
			output.z = (float)(color & 0xFF) / 255.0f;
			output.w = (float)((color >> 24) & 0xFF) / 255.0f;
			return output;
		}

		HexColor operator()(const Color& color) const
		{
			HexColor output = 0;
			//unsigned int layout: AAAA RRRR GGGG BBBB
			output |= (unsigned char)(color.x * 255.0f) << 16;
			output |= (unsigned char)(color.y * 255.0f) << 8;
			output |= (unsigned char)(color.z * 255.0f);
			output |= (unsigned char)(color.w * 255.0f) << 24;
			return output;
		}
	};

	inline int32 RoundUp(float value)
	{
		return (int32)ceil(value);
	}
};