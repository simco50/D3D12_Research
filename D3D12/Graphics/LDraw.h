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
		uint32 Index;
		uint32 Color;
		Matrix Transform;
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
	std::map<StringHash, uint32> PartMap;
	std::vector<LdrMaterial> Materials;
	std::map<uint32, uint32> MaterialMap;
	LdrMaterial DefaultMaterial;

	struct DatabaseLocation
	{
		const char* pLocation;
		LdrPart::Type Type;
	};

	std::vector<DatabaseLocation> DatabaseLocations;
};

bool LdrInit(const LdrConfig* pConfig, LdrState* pData);
	
bool LdrLoadModel(const char* pFile, LdrState* pData, LdrModel& outModel);

const LdrMaterial& LdrGetMaterial(uint32 code, LdrState* pData);
