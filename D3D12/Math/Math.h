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

	constexpr float ToKiloBytes = 1.0f / (1 << 10);
	constexpr float ToMegaBytes = 1.0f / (1 << 20);
	constexpr float ToGigaBytes = 1.0f / (1 << 30);

	constexpr uint32 FromKilobytes = 1 << 10;
	constexpr uint32 FromMegaBytes = 1 << 20;
	constexpr uint32 FromGigaBytes = 1 << 30;

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

	template<typename T>
	constexpr T Clamp(const T value, const T low, const T high)
	{
		if (value > high)
			return high;
		else if (value < low)
			return low;
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

	void GetProjectionClipPlanes(const Matrix& projection, float& nearPlane, float& farPlane);
	void ReverseZProjection(Matrix& projection);

	Vector3 ScaleFromMatrix(const Matrix& m);

	Quaternion LookRotation(const Vector3& direction);

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

	inline uint32 EncodeColor(float r, float g, float b, float a = 1.0f)
	{
		uint32 output = 0;
		//unsigned int layout: RRRR GGGG BBBB AAAA
		output |= (unsigned char)(Clamp01(r) * 255.0f) << 24;
		output |= (unsigned char)(Clamp01(g) * 255.0f) << 16;
		output |= (unsigned char)(Clamp01(b) * 255.0f) << 8;
		output |= (unsigned char)(Clamp01(a) * 255.0f) << 0;
		return output;
	}

	inline uint32 EncodeColor(const Color& color)
	{
		return EncodeColor(color.x, color.y, color.z, color.w);
	}

	inline Color DecodeColor(uint32 color)
	{
		Color output;
		//unsigned int layout: RRRR GGGG BBBB AAAA
		output.x = (float)((color >> 24) & 0xFF) / 255.0f;
		output.y = (float)((color >> 16) & 0xFF) / 255.0f;
		output.z = (float)((color >> 8) & 0xFF) / 255.0f;
		output.w = (float)((color >> 0) & 0xFF) / 255.0f;
		return output;
	}

	inline int32 RoundUp(float value)
	{
		return (int32)ceil(value);
	}

	inline uint32 DivideAndRoundUp(uint32 nominator, uint32 denominator)
	{
		return (uint32)ceil((float)nominator / denominator);
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
			for (int i = 0; i < SIZE; ++i)
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
