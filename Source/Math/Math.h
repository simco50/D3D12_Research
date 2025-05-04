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

	inline String PrettyPrintDataSize(uint64 sizeInBytes)
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
	constexpr T AlignUp(T value, T alignment)
	{
		return (value + ((T)alignment - 1)) & ~(alignment - 1);
	}

	template <typename T>
	constexpr bool IsAligned(T value, int alignment)
	{
		gAssert((alignment & (alignment - 1)) == 0, "Alignment is not a power of two");
		return ((((std::intptr_t)value) & (alignment - 1)) == 0);
	}

	float Lerp(float t, float a, float b);
	float InverseLerp(float value, float rangeMin, float rangeMax);
	float RemapRange(float value, float sourceRangeMin, float sourceRangeMax, float targetRangeMin, float targetRangeMax);

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

	String ToBase(unsigned int number, unsigned int base, bool addPrefix = true);
	inline String ToBinary(uint32 number, bool addPrefix = true)
	{
		return ToBase(number, 2, addPrefix);
	}
	inline String ToHex(uint32 number, bool addPrefix = true)
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

	/*
		Packing/Encoding Functions
	*/

	inline uint16 F32toF16(float value)
	{
		return DirectX::PackedVector::XMConvertFloatToHalf(value);
	}

	inline uint32 Pack_RG16_FLOAT(const Vector2& v)
	{
		return F32toF16(v.x) | (F32toF16(v.y) << 16u);
	}

	inline Vector2u Pack_RGBA16_FLOAT(const Vector4& v)
	{
		return Vector2u(Pack_RG16_FLOAT(Vector2(v.x, v.y)), Pack_RG16_FLOAT(Vector2(v.z, v.w)));
	}

	constexpr inline uint16 Encode_R16_SNORM(float value)
	{
		return static_cast<uint16>(Clamp(value >= 0.0f ? (value * 32767.0f + 0.5f) : (value * 32767.0f - 0.5f), -32768.0f, 32767.0f));
	}

	constexpr inline uint32 Pack_RG16_SNORM(const Vector2& v)
	{
		return Encode_R16_SNORM(Clamp(v.x, -1.0f, 1.0f)) | (Encode_R16_SNORM(Clamp(v.y, -1.0f, 1.0f)) << 16u);
	}

	constexpr inline Vector2u Pack_RGBA16_SNORM(const Vector4& v)
	{
		return Vector2u(Pack_RG16_SNORM(Vector2(v.x, v.y)), Pack_RG16_SNORM(Vector2(v.z, v.w)));
	}

	inline uint32 Pack_RGBA8_SNORM(const Vector4& v)
	{
		return
			((uint8)roundf(Clamp(v.x, -1.0f, 1.0) * 127.0f) << 0) |
			((uint8)roundf(Clamp(v.y, -1.0f, 1.0) * 127.0f) << 8) |
			((uint8)roundf(Clamp(v.z, -1.0f, 1.0) * 127.0f) << 16) |
			((uint8)roundf(Clamp(v.w, -1.0f, 1.0) * 127.0f) << 24);
	}

	inline uint32 Pack_RGBA8_UNORM(const Vector4& v)
	{
		return
			((uint8)roundf(Clamp(v.x, 0.0f, 1.0) * 255.0f) << 0) |
			((uint8)roundf(Clamp(v.y, 0.0f, 1.0) * 255.0f) << 8) |
			((uint8)roundf(Clamp(v.z, 0.0f, 1.0) * 255.0f) << 16) |
			((uint8)roundf(Clamp(v.w, 0.0f, 1.0) * 255.0f) << 24);
	}

	constexpr inline Vector4 Unpack_RGBA8_UNORM(uint32 v)
	{
		constexpr float rcp_255 = 1.0f / 255.0f;
		return Vector4(
			(float)((v << 24) >> 24) * rcp_255,
			(float)((v << 16) >> 24) * rcp_255,
			(float)((v << 8) >> 24) * rcp_255,
			(float)((v << 0) >> 24) * rcp_255
		);
	}

	inline uint32 Pack_R11G11B10_FLOAT(const Vector3& xyz)
	{
		uint32 r = (F32toF16(xyz.x) << 17) & 0xFFE00000;
		uint32 g = (F32toF16(xyz.y) << 6) & 0x001FFC00;
		uint32 b = (F32toF16(xyz.z) >> 5) & 0x000003FF;
		return r | g | b;
	}

	inline uint32 Pack_RGBE8_UNORM(const Vector3& color)
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

	inline Vector3 Unpack_RGBE8_UNORM(uint32 encoded)
	{
		Color c = Unpack_RGBA8_UNORM(encoded);
		float exponent = c.w * 255 - 128;
		return Vector3(c.x, c.y, c.z) * exp2(exponent);
	}

	inline uint32 Pack_RGB10A2_SNORM(const Vector4& v)
	{
		return
			((int32)(roundf(Clamp(v.x, -1.0f, 1.0f) * 511.0f)) & 0x3FF) << 0 |
			((int32)(roundf(Clamp(v.y, -1.0f, 1.0f) * 511.0f)) & 0x3FF) << 10 |
			((int32)(roundf(Clamp(v.z, -1.0f, 1.0f) * 511.0f)) & 0x3FF) << 20 |
			((int32)roundf(Clamp(v.w, -1.0f, 1.0f)) << 30);
	}

	inline Vector4 Unpack_RGB10A2_SNORM(uint32 v)
	{
		const float scaleXYZ = 1.0f / 511.0f;
		int32 signedV = (int32)v;
		return Vector4(
			((signedV << 22) >> 22) * scaleXYZ,
			((signedV << 12) >> 22) * scaleXYZ,
			((signedV << 2) >> 22) * scaleXYZ,
			((signedV << 0) >> 30) * 1.0f
		);
	}

	constexpr inline uint32 DivideAndRoundUp(uint32 nominator, uint32 denominator)
	{
		return (nominator + denominator - 1) / denominator;
	}

	constexpr inline uint32 NextPowerOfTwo(uint32 v)
	{
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}

	Color MakeFromColorTemperature(float Temp);

	// From https://github.com/stolk/hsvbench
	inline Color HSVtoRGB(float h, float s, float v)
	{
		const float h6 = 6.0f * h;
		const float r = fabsf(h6 - 3.0f) - 1.0f;
		const float g = 2.0f - fabsf(h6 - 2.0f);
		const float b = 2.0f - fabsf(h6 - 4.0f);

		const float is = 1.0f - s;
		Color rgba;
		rgba.x = v * (s * Clamp(r, 0.0f, 1.0f) + is);
		rgba.y = v * (s * Clamp(g, 0.0f, 1.0f) + is);
		rgba.z = v * (s * Clamp(b, 0.0f, 1.0f) + is);
		rgba.w = 1.0f;
		return rgba;
	}

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
