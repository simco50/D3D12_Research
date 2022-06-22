#pragma once

namespace Colors
{
	constexpr Color Transparent =	Color(0.0f, 0.0f, 0.0f, 0.0f);
	constexpr Color White =			Color(1.0f, 1.0f, 1.0f, 1.0f);
	constexpr Color Black =			Color(0.0f, 0.0f, 0.0f, 1.0f);
	constexpr Color Red =			Color(1.0f, 0.0f, 0.0f, 1.0f);
	constexpr Color Green =			Color(0.0f, 1.0f, 0.0f, 1.0f);
	constexpr Color Blue =			Color(0.0f, 0.0f, 1.0f, 1.0f);
	constexpr Color Yellow =		Color(1.0f, 1.0f, 0.0f, 1.0f);
	constexpr Color Magenta =		Color(1.0f, 0.0f, 1.0f, 1.0f);
	constexpr Color Cyan =			Color(0.0f, 1.0f, 1.0f, 1.0f);
	constexpr Color Gray =			Color(0.5f, 0.5f, 0.5f, 1.0f);
};

namespace Math
{
	constexpr float PI = 3.14159265358979323846f;
	constexpr float INV_PI = 0.31830988618379067154f;
	constexpr float INV_2PI = 0.15915494309189533577f;
	constexpr float INV_4PI = 0.07957747154594766788f;
	constexpr float PI_DIV_2 = 1.57079632679489661923f;
	constexpr float PI_DIV_4 = 0.78539816339744830961f;
	constexpr float SQRT_2 = 1.41421356237309504880f;

	constexpr float RadiansToDegrees = 180.0f / PI;
	constexpr float DegreesToRadians = PI / 180.0f;

	inline constexpr float Radians(float degrees) { return degrees * DegreesToRadians; }
	inline constexpr float Degrees(float radians) { return radians * RadiansToDegrees; }

	constexpr float BytesToKiloBytes = 1.0f / (1 << 10);
	constexpr float BytesToMegaBytes = 1.0f / (1 << 20);
	constexpr float BytesToGigaBytes = 1.0f / (1 << 30);

	constexpr uint32 KilobytesToBytes = 1 << 10;
	constexpr uint32 MegaBytesToBytes = 1 << 20;
	constexpr uint32 GigaBytesToBytes = 1 << 30;

	inline std::string PrettyPrintDataSize(uint64 sizeInBytes)
	{
		if (sizeInBytes > 1 << 30)
		{
			return Sprintf("%.2f GB", (float)sizeInBytes * BytesToGigaBytes);
		}
		if (sizeInBytes > 1 << 20)
		{
			return Sprintf("%.2f MB", (float)sizeInBytes * BytesToMegaBytes);
		}
		if (sizeInBytes > 1 << 10)
		{
			return Sprintf("%.2f KB", (float)sizeInBytes * BytesToKiloBytes);
		}
		return Sprintf("%.2f B", (float)sizeInBytes);
	}

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

	float RandomRange(float min, float max);

	int RandomRange(int min, int max);

	template<typename T, typename U, typename V>
	constexpr T Clamp(const T value, const U low, const V high)
	{
		if (value > high)
			return (T)high;
		else if (value < low)
			return (T)low;
		return value;
	}

	template<typename T>
	constexpr T Average(const T& a, const T& b)
	{
		return (a + b) / (T)2;
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

	template<typename T>
	T AlignUp(T value, T alignment)
	{
		return (value + ((T)alignment - 1)) & ~(alignment - 1);
	}

	float Lerp(float a, float b, float t);

	float InverseLerp(float a, float b, float value);

	Matrix CreatePerspectiveMatrix(float FoV, float aspectRatio, float nearPlane, float farPlane);
	Matrix CreatePerspectiveOffCenterMatrix(float left, float right, float bottom, float top, float nearPlane, float farPlane);
	Matrix CreateOrthographicMatrix(float width, float height, float nearPlane, float farPlane);
	Matrix CreateOrthographicOffCenterMatrix(float left, float right, float bottom, float top, float nearPlane, float farPlane);
	Matrix CreateLookToMatrix(const Vector3& position, const Vector3& direction, const Vector3& up);
	BoundingFrustum CreateBoundingFrustum(const Matrix& projection, const Matrix& view = Matrix::Identity);

	void GetProjectionClipPlanes(const Matrix& projection, float& nearPlane, float& farPlane);
	void ReverseZProjection(Matrix& projection);

	Vector3 ScaleFromMatrix(const Matrix& m);

	Quaternion LookRotation(const Vector3& direction, const Vector3& up = Vector3::Up);

	std::string ToBase(unsigned int number, unsigned int base, bool addPrefix = true);
	inline std::string ToBinary(uint32 number, bool addPrefix = true)
	{
		return ToBase(number, 2, addPrefix);
	}
	inline std::string ToHex(uint32 number, bool addPrefix = true)
	{
		return ToBase(number, 16, addPrefix);
	}

	Vector3 RandVector();

	Vector3 RandCircleVector();

	template<typename T>
	inline T Floor(const T& v)
	{
		return (T)floor(v);
	}

	template<>
	inline Vector3 Floor(const Vector3& v)
	{
		return Vector3(Floor(v.x), Floor(v.y), Floor(v.z));
	}

	template<typename T>
	inline T Ceil(const T& v)
	{
		return (T)ceil(v);
	}

	template<>
	inline Vector3 Ceil(const Vector3& v)
	{
		return Vector3(Ceil(v.x), Ceil(v.y), Ceil(v.z));
	}

	inline uint32 EncodeRGBA(float r, float g, float b, float a = 1.0f)
	{
		uint32 output = 0;
		//unsigned int layout: RRRR GGGG BBBB AAAA
		output |= (unsigned char)(Clamp01(r) * 255.0f) << 24;
		output |= (unsigned char)(Clamp01(g) * 255.0f) << 16;
		output |= (unsigned char)(Clamp01(b) * 255.0f) << 8;
		output |= (unsigned char)(Clamp01(a) * 255.0f) << 0;
		return output;
	}

	inline uint32 EncodeRGBA(const Color& color)
	{
		return EncodeRGBA(color.x, color.y, color.z, color.w);
	}

	inline Color DecodeRGBA(uint32 color)
	{
		Color output;
		constexpr float rcp_255 = 1.0f / 255.0f;
		//unsigned int layout: RRRR GGGG BBBB AAAA
		output.x = (float)((color >> 24) & 0xFF) * rcp_255;
		output.y = (float)((color >> 16) & 0xFF) * rcp_255;
		output.z = (float)((color >> 8) & 0xFF) * rcp_255;
		output.w = (float)((color >> 0) & 0xFF) * rcp_255;
		return output;
	}

	inline uint32 EncodeRGBE(const Vector3& color)
	{
		float maxComponent = Max(Max(color.x, color.y), color.z);
		float exponent = Ceil(log2(maxComponent));
		uint32 output = 0;
		output |= (unsigned char)(color.x / exp2(exponent) * 255) << 24;
		output |= (unsigned char)(color.y / exp2(exponent) * 255) << 16;
		output |= (unsigned char)(color.z / exp2(exponent) * 255) << 8;
		output |= (unsigned char)((exponent + 128)) << 0;
		return output;
	}

	inline Vector3 DecodeRGBE(uint32 encoded)
	{
		Color c = DecodeRGBA(encoded);
		float exponent = c.w * 255 - 128;
		return Vector3(c.x, c.y, c.z) * exp2(exponent);
	}

	inline int32 RoundUp(float value)
	{
		return (int32)Ceil(value);
	}

	inline uint32 DivideAndRoundUp(uint32 nominator, uint32 denominator)
	{
		return (uint32)Ceil((float)nominator / denominator);
	}

	Color MakeFromColorTemperature(float Temp);

	struct Halton
	{
		static constexpr int FloorConstExpr(const float val)
		{
			// casting to int truncates the value, which is floor(val) for positive values,
			// but we have to substract 1 for negative values (unless val is already floored == recasted int val)
			const auto val_int = (int64_t)val;
			const float fval_int = (float)val_int;
			return (int)(val >= (float)0 ? fval_int : (val == fval_int ? val : fval_int - (float)1));
		}

		constexpr float operator()(int index, int base) const
		{
			float f = 1;
			float r = 0;
			while (index > 0)
			{
				f = f / base;
				r = r + f * (index % base);
				index = FloorConstExpr((float)index / base);
			}
			return r;
		}
	};

	template<uint32 SIZE, uint32 BASE>
	struct HaltonSequence
	{
		constexpr HaltonSequence() 
			: Sequence{}
		{
			Halton generator;
			for (uint32 i = 0; i < SIZE; ++i)
			{
				Sequence[i] = generator(i + 1, BASE);
			}
		}

		constexpr float operator[](int32 index) const
		{
			return Sequence[index % SIZE];
		}

	private:

		float Sequence[SIZE];
	};
};
