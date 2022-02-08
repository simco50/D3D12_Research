#pragma once

#include <vector>
#include <unordered_map>
#include <memory>

using uint32 = unsigned int;
using uint8 = unsigned char;

enum class LdrResult
{
	Error_FileParseError = -2,
	Error_FileNotFound = -1,
	Success = 0,
	Warning_PartNotFound,
};

struct LdrName
{
	static constexpr uint32 SIZE = 128;

	LdrName() { Data[0] = 0; }
	LdrName(const char* pText) { strcpy_s(Data, SIZE, pText); }

	const char* c_str() const { return Data; }
	bool operator==(const LdrName& rhs) const { return strcmp(Data, rhs.Data) == 0; }
	bool operator<(const LdrName& rhs) const { return strcmp(Data, rhs.Data) < 0; }

	char Data[SIZE];

	struct Hasher
	{
		static constexpr uint32 val_const{ 0x811c9dc5 };
		static constexpr uint32 prime_const{ 0x1000193 };

		static inline constexpr uint32 Hash_Internal(const char* const str, const uint32 value) noexcept
		{
			return (str[0] == '\0') ? value : Hash_Internal(&str[1], (value ^ (unsigned long long)(str[0])) * prime_const);
		}

		constexpr size_t operator()(const LdrName& name) const { return Hash_Internal(name.Data, val_const); }
	};
};

struct LdrMatrix
{
	union
	{
		float m[4][4];
		float v[16];
	};

	LdrMatrix(float m00 = 1, float m01 = 0, float m02 = 0, float m03 = 0,
		float m10 = 0, float m11 = 1, float m12 = 0, float m13 = 0,
		float m20 = 0, float m21 = 0, float m22 = 1, float m23 = 0,
		float m30 = 0, float m31 = 0, float m32 = 0, float m33 = 1)
	{
		m[0][0] = m00; m[0][1] = m01; m[0][2] = m02; m[0][3] = m03;
		m[1][0] = m10; m[1][1] = m11; m[1][2] = m12; m[1][3] = m13;
		m[2][0] = m20; m[2][1] = m21; m[2][2] = m22; m[2][3] = m23;
		m[3][0] = m30; m[3][1] = m31; m[3][2] = m32; m[3][3] = m33;
	}

	static LdrMatrix CreateScale(float x, float y, float z)
	{
		return LdrMatrix(
			x, 0, 0, 0,
			0, y, 0, 0,
			0, 0, z, 0,
			0, 0, 0, 1
		);
	}

	LdrMatrix operator*(const LdrMatrix& rhs) const
	{
		LdrMatrix out;
		out.m[0][0] = m[0][0] * rhs.m[0][0] + m[0][1] * rhs.m[1][0] + m[0][2] * rhs.m[2][0] + m[0][3] * rhs.m[3][0];
		out.m[0][1] = m[0][0] * rhs.m[0][1] + m[0][1] * rhs.m[1][1] + m[0][2] * rhs.m[2][1] + m[0][3] * rhs.m[3][1];
		out.m[0][2] = m[0][0] * rhs.m[0][2] + m[0][1] * rhs.m[1][2] + m[0][2] * rhs.m[2][2] + m[0][3] * rhs.m[3][2];
		out.m[0][3] = m[0][0] * rhs.m[0][3] + m[0][1] * rhs.m[1][3] + m[0][2] * rhs.m[2][3] + m[0][3] * rhs.m[3][3];

		out.m[1][0] = m[1][0] * rhs.m[0][0] + m[1][1] * rhs.m[1][0] + m[1][2] * rhs.m[2][0] + m[1][3] * rhs.m[3][0];
		out.m[1][1] = m[1][0] * rhs.m[0][1] + m[1][1] * rhs.m[1][1] + m[1][2] * rhs.m[2][1] + m[1][3] * rhs.m[3][1];
		out.m[1][2] = m[1][0] * rhs.m[0][2] + m[1][1] * rhs.m[1][2] + m[1][2] * rhs.m[2][2] + m[1][3] * rhs.m[3][2];
		out.m[1][3] = m[1][0] * rhs.m[0][3] + m[1][1] * rhs.m[1][3] + m[1][2] * rhs.m[2][3] + m[1][3] * rhs.m[3][3];

		out.m[2][0] = m[2][0] * rhs.m[0][0] + m[2][1] * rhs.m[1][0] + m[2][2] * rhs.m[2][0] + m[2][3] * rhs.m[3][0];
		out.m[2][1] = m[2][0] * rhs.m[0][1] + m[2][1] * rhs.m[1][1] + m[2][2] * rhs.m[2][1] + m[2][3] * rhs.m[3][1];
		out.m[2][2] = m[2][0] * rhs.m[0][2] + m[2][1] * rhs.m[1][2] + m[2][2] * rhs.m[2][2] + m[2][3] * rhs.m[3][2];
		out.m[2][3] = m[2][0] * rhs.m[0][3] + m[2][1] * rhs.m[1][3] + m[2][2] * rhs.m[2][3] + m[2][3] * rhs.m[3][3];

		out.m[3][0] = m[3][0] * rhs.m[0][0] + m[3][1] * rhs.m[1][0] + m[3][2] * rhs.m[2][0] + m[3][3] * rhs.m[3][0];
		out.m[3][1] = m[3][0] * rhs.m[0][1] + m[3][1] * rhs.m[1][1] + m[3][2] * rhs.m[2][1] + m[3][3] * rhs.m[3][1];
		out.m[3][2] = m[3][0] * rhs.m[0][2] + m[3][1] * rhs.m[1][2] + m[3][2] * rhs.m[2][2] + m[3][3] * rhs.m[3][2];
		out.m[3][3] = m[3][0] * rhs.m[0][3] + m[3][1] * rhs.m[1][3] + m[3][2] * rhs.m[2][3] + m[3][3] * rhs.m[3][3];

		return out;
	}

	float Determinant3x3() const
	{
		return m[0][0] * m[1][1] * m[2][2]
			+ m[1][0] * m[2][1] * m[0][2]
			+ m[2][0] * m[0][1] * m[1][2]
			- m[2][0] * m[1][1] * m[0][2]
			- m[0][0] * m[2][1] * m[1][2]
			- m[1][0] * m[0][1] * m[2][2];
	}
};

struct LdrVector
{
	LdrVector(float x = .0f, float y = .0f, float z = .0f)
		: x(x), y(y), z(z)
	{}

	float Length() const { return sqrtf(x * x + y * y + z * z); }

	LdrVector Normalize() const
	{
		float lenInv = 1.0f / Length();
		return LdrVector(x * lenInv, y * lenInv, z * lenInv);
	}

	float Dot(const LdrVector& rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z; }

	LdrVector Cross(const LdrVector& rhs) const
	{
		return LdrVector(
			y * rhs.z - z * rhs.y,
			z * rhs.x - x * rhs.z,
			x * rhs.y - y * rhs.x);
	}

	LdrVector Transform(const LdrMatrix& m) const
	{
		return LdrVector(
			x * m.m[0][0] + y * m.m[1][0] + z * m.m[2][0] + 1 * m.m[3][0],
			x * m.m[0][1] + y * m.m[1][1] + z * m.m[2][1] + 1 * m.m[3][1],
			x * m.m[0][2] + y * m.m[1][2] + z * m.m[2][2] + 1 * m.m[3][2]
		);
	}

	LdrVector operator+(const LdrVector& rhs) const { return LdrVector(x + rhs.x, y + rhs.y, z + rhs.z); }
	LdrVector operator-(const LdrVector& rhs) const { return LdrVector(x - rhs.x, y - rhs.y, z - rhs.z); }
	bool operator<(const LdrVector& rhs) const
	{
		return ((x < rhs.x)
			|| ((x == rhs.x) && (y < rhs.y))
			|| ((x == rhs.x) && (y == rhs.y) && (z < rhs.z)));
	}
	bool operator==(const LdrVector& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }

	float x, y, z;
};

enum class LdrMaterialFinish
{
	None,
	Chrome,
	Pearlescent,
	Rubber,
	Metallic,
	MatteMetallic,
	Speckle,
	Glitter,
};

struct LdrMaterial
{
	LdrName Name;
	uint32 Code = 0;
	uint32 Color = 0;
	uint32 EdgeColor = 0;
	uint8 Luminance = 0;
	LdrMaterialFinish Type = LdrMaterialFinish::None;

	union
	{
		struct Glitter
		{
			uint32 Color;
			float Fraction;
			float VFraction;
			float Size;
		} Glitter;
		struct Speckle
		{
			uint32 Color;
			float Fraction;
			float MinSize;
			float MaxSize;
		} Speckle;
	};
};

struct LdrSubfile
{
	LdrName Name;
	LdrMatrix Transform;
	uint32 Color;
	bool Invert;
};

struct LdrPart
{
	enum class Type
	{
		LocalModel,
		Primitive,
		Part,
		Subpart,
	};

	LdrName Name;
	Type PartType;
	bool IsMultiMaterial = false;
	std::vector<uint32> Colors;
	std::vector<LdrVector> Vertices;
	std::vector<LdrVector> Normals;
	std::vector<uint32> Indices;
	std::vector<LdrSubfile> Subfiles;

	bool HasData() const
	{
		return Vertices.size() > 0 || Subfiles.size() > 0;
	}
};

struct LdrModel
{
	struct Instance
	{
		uint32 Index;
		uint32 Color;
		LdrMatrix Transform;
	};

	std::vector<LdrPart*> Parts;
	std::vector<Instance> Instances;
};

enum class LdrQuality
{
	Normal,
	Low,
	High,
};

struct LdrConfig
{
	const char* pDatabasePath;
	LdrQuality Quality = LdrQuality::Normal;
	std::vector<std::pair<const char*, const char*>> ReplacementMap;
};

struct LdrState
{
	LdrConfig Config;
	std::vector<std::unique_ptr<LdrPart>> Parts;
	std::unordered_map<LdrName, uint32, LdrName::Hasher> PartMap;
	std::vector<LdrMaterial> Materials;
	std::unordered_map<uint32, uint32> MaterialMap;
	LdrMaterial DefaultMaterial;

	struct DatabaseLocation
	{
		const char* pLocation;
		LdrPart::Type Type;
	};

	std::vector<DatabaseLocation> DatabaseLocations;
};

LdrResult LdrInit(const LdrConfig* pConfig, LdrState* pData);
	
LdrResult LdrLoadModel(const char* pFile, LdrState* pData, LdrModel& outModel);

const LdrMaterial& LdrGetMaterial(uint32 code, const LdrState* pData);

uint32 LdrResolveVertexColor(uint32 partColor, uint32 vertexColor, const LdrState* pData);

void LdrDecodeARGB(uint32 color, float* pColor);
