#pragma once

enum class LdrMaterialType
{
	None,
	Chrome,
	Speckle,
	Pearlescent,
	Rubber,
	Metal,
	Glitter,
};

struct LdrMaterial
{
	char Name[128];
	uint32 Code = 0;
	uint32 Color = 0;
	uint32 EdgeColor = 0;
	uint32 Emissive = 0;
	uint8 Alpha = 0xFF;
	uint8 Luminance = 0;
	LdrMaterialType Type = LdrMaterialType::None;

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
	char Name[128];
	Matrix Transform;
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

	char Name[128];
	std::vector<uint32> Colors;
	std::vector<Vector3> Vertices;
	std::vector<Vector3> Normals;
	std::vector<uint32> Indices;
	std::vector<LdrSubfile> Subfiles;
	Type PartType;

	bool HasData() const
	{
		return Vertices.size() > 0 || Subfiles.size() > 0;
	}
};

struct LdrModel
{
	struct Instance
	{
		int Index;
		int Color;
		Matrix Transform;
	};

	std::vector<LdrPart*> Parts;
	std::vector<Instance> Instances;
};

enum LdrQuality
{
	Normal,
	Low,
	High,
};

struct LdrData
{
	const char* pDatabasePath = "D:/References/ldraw/ldraw/";
	std::vector<std::unique_ptr<LdrPart>> Parts;
	LdrQuality Quality = LdrQuality::Normal;
	std::map<StringHash, int> PartMap;
	std::vector<LdrMaterial> Materials;
	std::map<int, int> MaterialMap;
	LdrMaterial DefaultMaterial;

	struct DatabaseLocation
	{
		const char* pLocation;
		LdrPart::Type Type;
	};

	std::vector<DatabaseLocation> DatabaseLocations;
};

bool LdrInit(LdrData* pData);
	
bool LdrLoadModel(const char* pFile, LdrData* pData, LdrModel& outModel);

const LdrMaterial& LdrGetMaterial(int code, LdrData* pData);
