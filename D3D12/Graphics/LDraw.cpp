#include "stdafx.h"
#include "LDraw.h"

constexpr int MATERIAL_CODE_INHERIT = 16;
constexpr int MATERIAL_CODE_COMPLEMENT = 24;

namespace Util
{
	inline void ToLower(char* pStr)
	{
		while (*pStr)
		{
			*pStr = (char)tolower(*pStr);
			++pStr;
		}
	};

	struct FileReader
	{
		bool Open(const char* pPath)
		{
			if (pData)
			{
				delete[] pData;
				pData = nullptr;
			}
			FILE* pFile = nullptr;
			fopen_s(&pFile, pPath, "r");
			if (!pFile)
				return false;
			fseek(pFile, 0, SEEK_END);
			size_t size = ftell(pFile);
			pData = new char[size];
			pCurrentLine = pData;
			fseek(pFile, 0, SEEK_SET);
			size_t read = fread(pData, sizeof(char), size, pFile);
			pData[read] = '\0';
			fclose(pFile);
			return true;
		}

		~FileReader()
		{
			delete[] pData;
		}

		bool GetLine(const char** pOutLine)
		{
			char* pLine = pCurrentLine;
			pNextLine = strchr(pLine, '\n');
			if (pNextLine)
			{
				*pNextLine = '\0';
				*pOutLine = pCurrentLine;
				pCurrentLine = pNextLine + 1;
				return true;
			}
			*pOutLine = nullptr;
			return false;
		}

		bool IsOpen() const
		{
			return pData;
		}

		char* pCurrentLine = nullptr;
		char* pNextLine = nullptr;
		char* pData = nullptr;
	};
}

const LdrMaterial& LdrGetMaterial(int code, LdrState* pData)
{
	auto it = pData->MaterialMap.find(code);
	if (it != pData->MaterialMap.end())
	{
		return pData->Materials[it->second];
	}
	return pData->DefaultMaterial;
}

bool LdrInit(const LdrConfig* pConfig, LdrState* pData)
{
	pData->Config = *pConfig;
	pData->MaterialMap.clear();
	pData->Materials.clear();
	pData->PartMap.clear();
	pData->Parts.clear();

	if (pConfig->Quality == LdrQuality::High)
		pData->DatabaseLocations.push_back({ "p/48/" , LdrPart::Type::Primitive });
	else if(pConfig->Quality == LdrQuality::Low)
		pData->DatabaseLocations.push_back({ "p/8/" , LdrPart::Type::Primitive });

	pData->DatabaseLocations.push_back({ "p/", LdrPart::Type::Primitive });				// Official Primitives
	pData->DatabaseLocations.push_back({ "parts/", LdrPart::Type::Part });				// Official Parts
	pData->DatabaseLocations.push_back({ "models/", LdrPart::Type::Primitive });		// Demo models
	pData->DatabaseLocations.push_back({ "UnOfficial/p/", LdrPart::Type::Primitive });	// Unofficial Primitives
	pData->DatabaseLocations.push_back({ "UnOfficial/parts/", LdrPart::Type::Part });	// Unofficial Parts

	LdrMaterial& defaultMaterial = pData->DefaultMaterial;
	memset(&defaultMaterial, 0, sizeof(LdrMaterial));
	strcpy_s(defaultMaterial.Name, "INVALID");
	defaultMaterial.Color = 0x00FF00FF;
	defaultMaterial.EdgeColor = 0x00FF00FF;

	char configPath[256];
	sprintf_s(configPath, ARRAYSIZE(configPath), "%sLDConfig.ldr", pConfig->pDatabasePath);
	Util::FileReader reader;
	if (!reader.Open(configPath))
	{
		return false;
	}

	const char* pLine = nullptr;
	while (reader.GetLine(&pLine))
	{
		LdrMaterial material;
		material.Type = LdrMaterialType::None;

		const int numRequiredValues = 4;
		if (sscanf_s(pLine, "0 !COLOUR %128s CODE %d VALUE #%x EDGE #%x", material.Name, (uint32)ARRAYSIZE(material.Name), &material.Code, &material.Color, &material.EdgeColor) == numRequiredValues)
		{
			const char* pSearch = nullptr;
			pSearch = strstr(pLine, "ALPHA");
			if (pSearch)
			{
				int alpha = 0;
				sscanf_s(pSearch, "ALPHA %d", &alpha);
				material.Alpha = (uint8)alpha;
			}
			pSearch = strstr(pLine, "LUMINANCE");
			if (pSearch)
			{
				int luminance = 0;
				sscanf_s(pSearch, "LUMINANCE %d", &luminance);
				material.Luminance = (uint8)luminance;
			}

			if (strstr(pLine, "CHROME"))
			{
				material.Type = LdrMaterialType::Chrome;
			}
			else if (strstr(pLine, "PEARLESCENT"))
			{
				material.Type = LdrMaterialType::Pearlescent;
			}
			else if (strstr(pLine, "METAL"))
			{
				material.Type = LdrMaterialType::Metal;
			}
			else if (strstr(pLine, "RUBBER"))
			{
				material.Type = LdrMaterialType::Rubber;
			}
			else if (strstr(pLine, "MATERIAL"))
			{
				pSearch = strstr(pLine, "GLITTER");
				if (pSearch)
				{
					sscanf_s(pSearch, "GLITTER VALUE #%x FRACTION %f VFRACTION %f SIZE %f", &material.Glitter.Color, &material.Glitter.Fraction, &material.Glitter.VFraction, &material.Glitter.Size);
					material.Type = LdrMaterialType::Glitter;
				}

				pSearch = strstr(pLine, "SPECKLE");
				if (pSearch)
				{
					sscanf_s(pSearch, "SPECKLE VALUE #%x FRACTION %f MINSIZE %f MAXSIZE %f", &material.Speckle.Color, &material.Speckle.Fraction, &material.Speckle.MinSize, &material.Speckle.MaxSize);
					material.Type = LdrMaterialType::Speckle;
				}
			}

			pData->MaterialMap[material.Code] = (int)pData->Materials.size();
			pData->Materials.push_back(material);
		}
	}
	return true;
}

bool ParseLDraw(const char* pPartName, LdrState* pData, std::vector<std::unique_ptr<LdrPart>>& outParts)
{
	outParts.clear();
	LdrPart::Type partType = LdrPart::Type::LocalModel;

	// Try absolute path
	Util::FileReader reader;
	if (reader.Open(pPartName))
	{
		partType = LdrPart::Type::LocalModel;
	}
	else // Try database path
	{
		for (const LdrState::DatabaseLocation& location : pData->DatabaseLocations)
		{
			char path[256];
			sprintf_s(path, ARRAYSIZE(path), "%s%s%s", pData->Config.pDatabasePath, location.pLocation, pPartName);
			if (reader.Open(path))
			{
				partType = location.Type;
				break;
			}
		}
	}

	if (!reader.IsOpen())
	{
		E_LOG(Warning, "Could not find part '%s'", pPartName);
		return false;
	}

	std::unique_ptr<LdrPart> part = std::make_unique<LdrPart>();
	part->PartType = partType;
	strcpy_s(part->Name, pPartName);
	outParts.push_back(std::move(part));

	bool invert = false;
	bool ccw = false;
	int dummy;

	enum class Command
	{
		Meta,
		Subfile,
		Line,
		Triangle,
		Quad,
		OptionalLine,
		MAX
	};

	const char* pLine = nullptr;
	while (reader.GetLine(&pLine))
	{
		LdrPart& currentPart = *outParts.back();

		if (strlen(pLine) <= 1)
			continue;

		int cmd;
		if (sscanf_s(pLine, "%d", &cmd) < 1)
			continue;
		Command command = (Command)cmd;
		assert((int)command < (int)Command::MAX);

		if (command == Command::Meta)
		{
			const char* pSearch = strstr(pLine, "0 BFC");
			if (pSearch)
			{
				if (strstr(pSearch, "INVERTNEXT"))
				{
					invert = true;
				}
				if (strstr(pSearch, "CW"))
				{
					ccw = false;
				}
				if (strstr(pSearch, "CCW"))
				{
					ccw = true;
				}
			}

			pSearch = strstr(pLine, "0 FILE");
			if (pSearch)
			{
				if (currentPart.HasData())
				{
					std::unique_ptr<LdrPart> newPart = std::make_unique<LdrPart>();
					newPart->PartType = partType;
					strcpy_s(newPart->Name, pSearch + sizeof("0 FILE"));
					Util::ToLower(newPart->Name);
					outParts.push_back(std::move(newPart));
				}
			}
		}
		else if (command == Command::Subfile)
		{
			LdrSubfile subfile;
			int curr = 0;
			Matrix& transform = subfile.Transform;

			sscanf_s(pLine, "%d %d %f %f %f %f %f %f %f %f %f %f %f %f %n",
				&dummy, &subfile.Color,
				&transform.m[3][0], &transform.m[3][1], &transform.m[3][2], // XYZ
				&transform.m[0][0], &transform.m[1][0], &transform.m[2][0],	// 3x3
				&transform.m[0][1], &transform.m[1][1], &transform.m[2][1],
				&transform.m[0][2], &transform.m[1][2], &transform.m[2][2],
				&curr
			);
			strcpy_s(subfile.Name, pLine + curr);
			Util::ToLower(subfile.Name);

			subfile.Invert = invert;
			invert = false;
			currentPart.Subfiles.push_back(subfile);
		}
		else if (command == Command::Line)
		{

		}
		else if (command == Command::Triangle)
		{
			int color;
			Vector3 triangle[3];

			sscanf_s(pLine, "%d %d %f %f %f %f %f %f %f %f %f",
				&dummy, &color,
				&triangle[0].x, &triangle[0].y, &triangle[0].z,
				&triangle[1].x, &triangle[1].y, &triangle[1].z,
				&triangle[2].x, &triangle[2].y, &triangle[2].z
			);

			currentPart.Vertices.push_back(triangle[ccw ? 2 : 0]);
			currentPart.Vertices.push_back(triangle[ccw ? 1 : 1]);
			currentPart.Vertices.push_back(triangle[ccw ? 0 : 2]);

			currentPart.Colors.push_back(color);
		}
		else if (command == Command::Quad)
		{
			int color;
			Vector3 quad[4];

			sscanf_s(pLine, "%d %d %f %f %f %f %f %f %f %f %f %f %f %f",
				&dummy, &color,
				&quad[0].x, &quad[0].y, &quad[0].z,
				&quad[1].x, &quad[1].y, &quad[1].z,
				&quad[2].x, &quad[2].y, &quad[2].z,
				&quad[3].x, &quad[3].y, &quad[3].z
			);

			currentPart.Vertices.push_back(quad[ccw ? 0 : 0]);
			currentPart.Vertices.push_back(quad[ccw ? 3 : 1]);
			currentPart.Vertices.push_back(quad[ccw ? 2 : 2]);
			currentPart.Vertices.push_back(quad[ccw ? 2 : 2]);
			currentPart.Vertices.push_back(quad[ccw ? 1 : 3]);
			currentPart.Vertices.push_back(quad[ccw ? 0 : 0]);

			currentPart.Colors.push_back(color);
			currentPart.Colors.push_back(color);
		}
	}
	return true;
}

LdrPart* GetPart(const char* pName, LdrState* pData)
{
	for (const std::pair<const char*, const char*>& replacement : pData->Config.ReplacementMap)
	{
		if (strcmp(pName, replacement.first) == 0)
		{
			if (replacement.second)
			{
				pName = replacement.second;
				break;
			}
			else
			{
				return nullptr;
			}
		}
	}

	auto it = pData->PartMap.find(pName);
	if (it != pData->PartMap.end())
	{
		return pData->Parts[it->second].get();
	}

	std::vector<std::unique_ptr<LdrPart>> parts;
	if (ParseLDraw(pName, pData, parts))
	{
		for (std::unique_ptr<LdrPart>& newPart : parts)
		{
			pData->PartMap[newPart->Name] = (int)pData->Parts.size();
			pData->Parts.push_back(std::move(newPart));
		}
		return pData->Parts[pData->Parts.size() - parts.size()].get();
	}
	return nullptr;
}

void ResolveModelParts(LdrPart* pPart, LdrState* pData, LdrModel& outModel, const Matrix& transform = Matrix::Identity, int color = 0)
{
	if (pPart->PartType == LdrPart::Type::Part)
	{
		LdrModel::Instance instance;
		instance.Transform = transform;
		instance.Color = color;
		auto it = std::find(outModel.Parts.begin(), outModel.Parts.end(), pPart);
		if (it != outModel.Parts.end())
		{
			instance.Index = (int)(it - outModel.Parts.begin());
		}
		else
		{
			instance.Index = (int)outModel.Parts.size();
			outModel.Parts.push_back(pPart);
		}
		outModel.Instances.push_back(instance);
	}
	else
	{
		for (LdrSubfile& subfile : pPart->Subfiles)
		{
			LdrPart* pSubpart = GetPart(subfile.Name, pData);
			if (pSubpart)
			{
				Matrix scale = subfile.Invert ? Matrix::CreateScale(-1) : Matrix::CreateScale(1);
				ResolveModelParts(pSubpart, pData, outModel, subfile.Transform * transform * scale, subfile.Color == MATERIAL_CODE_INHERIT ? color : subfile.Color);
			}
		}
	}
}

uint32 ResolveTriangleColor(uint32 triangleColor, uint32 parentColor)
{
	return triangleColor == MATERIAL_CODE_INHERIT ? parentColor : triangleColor;
}

void FlattenPart(LdrPart* pPart, LdrState* pData, const Matrix& currentMatrix = Matrix::Identity, bool invert = 0, int color = 0)
{
	for (const LdrSubfile& subfile : pPart->Subfiles)
	{
		LdrPart* pSubpart = GetPart(subfile.Name, pData);
		if (!pSubpart)
			continue;

		bool inv = subfile.Invert;
		float det = subfile.Transform.Determinant();
		inv ^= (det < 0);

		FlattenPart(pSubpart, pData, subfile.Transform * currentMatrix, invert ^ inv, ResolveTriangleColor(subfile.Color, color));

		for (int i = 0; i < pSubpart->Vertices.size(); i += 3)
		{
			IntVector3 winding = inv ? IntVector3(2, 1, 0) : IntVector3(0, 1, 2);
			pPart->Vertices.push_back(Vector3::Transform(pSubpart->Vertices[i + winding.x], subfile.Transform));
			pPart->Vertices.push_back(Vector3::Transform(pSubpart->Vertices[i + winding.y], subfile.Transform));
			pPart->Vertices.push_back(Vector3::Transform(pSubpart->Vertices[i + winding.z], subfile.Transform));

			pPart->Colors.push_back(ResolveTriangleColor(pSubpart->Colors[i / 3], color));
		}
	}
	pPart->Subfiles.clear();
}

void ComputePartNormals(LdrPart* pPart)
{
	if (pPart->Normals.empty())
	{
		pPart->Normals.resize(pPart->Vertices.size());
		for (size_t i = 0; i < pPart->Vertices.size(); i += 3)
		{
			Vector3 n0 = pPart->Vertices[i + 1] - pPart->Vertices[i + 0];
			Vector3 n1 = pPart->Vertices[i + 2] - pPart->Vertices[i + 0];
			Vector3 normal = n0.Cross(n1);
			normal.Normalize();
			pPart->Normals[i + 0] = normal;
			pPart->Normals[i + 1] = normal;
			pPart->Normals[i + 2] = normal;
		}

		std::map<Vector3, std::vector<int>> vertexMap;
		for (size_t i = 0; i < pPart->Vertices.size(); ++i)
		{
			vertexMap[pPart->Vertices[i]].push_back((int)i);
		}

		const float minAngleCos = cos(Math::PIDIV4);

		std::vector<Vector3> newNormals(pPart->Normals.size());
		for (size_t i = 0; i < pPart->Vertices.size(); ++i)
		{
			const std::vector<int>& identicalVertices = vertexMap[pPart->Vertices[i]];
			Vector3 vertexNormal = pPart->Normals[i];
			Vector3 smoothNormal;
			for (int vertexIndex : identicalVertices)
			{
				const Vector3& otherNormal = pPart->Normals[vertexIndex];
				if (vertexNormal.Dot(otherNormal) > minAngleCos)
				{
					smoothNormal += otherNormal;
				}
			}
			smoothNormal.Normalize();
			newNormals[i] = smoothNormal;
		}
		pPart->Normals.swap(newNormals);
	}
}

void ComputePartIndices(LdrPart* pPart)
{
	// Inspired by MeshOptimized by zeux

	struct HashedVertex
	{
		HashedVertex(const LdrPart* pPart, int vertex)
			: pPart(pPart), Vertex(vertex)
		{}
		const LdrPart* pPart;
		int Vertex;

		static void MurmurHash(uint32& h, const void* pData, size_t len)
		{
			assert(len % 4 == 0); // Assume 4 byte alignment as we're leaving off the last part of the algorithm
			const unsigned int m = 0x5bd1e995;
			const int r = 24;
			const char* pKey = (char*)pData;
			while (len >= 4)
			{
				unsigned int k = *(unsigned int*)(pData);

				k *= m;
				k ^= k >> r;
				k *= m;

				h *= m;
				h ^= k;

				pKey += 4;
				len -= 4;
			}
		}

		struct Hasher
		{
			size_t operator()(const HashedVertex& v) const
			{
				uint32 h = 0;
				MurmurHash(h, v.pPart->Vertices.data() + v.Vertex, sizeof(Vector3));
				MurmurHash(h, v.pPart->Normals.data() + v.Vertex, sizeof(Vector3));
				MurmurHash(h, v.pPart->Colors.data() + v.Vertex / 3, sizeof(uint32));
				return h;
			}
		};

		bool operator==(const HashedVertex& rhs) const
		{
			return pPart->Colors[Vertex / 3] == rhs.pPart->Colors[rhs.Vertex / 3] &&
				pPart->Normals[Vertex] == rhs.pPart->Normals[rhs.Vertex] &&
				pPart->Vertices[Vertex] == rhs.pPart->Vertices[rhs.Vertex];
		}
	};

	std::unordered_map<HashedVertex, uint32, HashedVertex::Hasher> buckets;
	std::vector<uint32> remap(pPart->Vertices.size(), ~0u);

	uint32 indexCount = 0;
	for (size_t i = 0; i < pPart->Vertices.size(); ++i)
	{
		if (remap[i] == ~0u)
		{
			HashedVertex v(pPart, (uint32)i);
			auto it = buckets.find(v);
			if (it == buckets.end())
			{
				buckets[v] = (uint32)i;
				remap[i] = indexCount++;
			}
			else
			{
				remap[i] = remap[it->second];
			}
		}
	}

	auto remapBuffer = [](void* pData, uint32* remap, uint32 numElements, uint32 elementSize)
	{
		// Make a copy
		std::vector<char> copy(numElements * elementSize);
		memcpy(copy.data(), pData, copy.size());

		for (uint32 i = 0; i < numElements; ++i)
		{
			memcpy((char*)pData + remap[i] * elementSize, copy.data() + i * elementSize, elementSize);
		}
	};

	auto remapColorBuffer = [](void* pData, uint32* remap, uint32 numElements, uint32 elementSize)
	{
		// Make a copy
		std::vector<char> copy(numElements * elementSize);
		memcpy(copy.data(), pData, copy.size());

		for (uint32 i = 0; i < numElements; ++i)
		{
			memcpy((char*)pData + (remap[i * 3] / 3) * elementSize, copy.data() + i * elementSize, elementSize);
		}
	};

	remapBuffer(pPart->Vertices.data(), remap.data(), (uint32)pPart->Vertices.size(), sizeof(Vector3));
	remapBuffer(pPart->Normals.data(), remap.data(), (uint32)pPart->Normals.size(), sizeof(Vector3));
	remapColorBuffer(pPart->Colors.data(), remap.data(), (uint32)pPart->Colors.size(), sizeof(uint32));

	pPart->Vertices.resize(indexCount);
	pPart->Normals.resize(indexCount);
	pPart->Colors.resize(indexCount / 3);
	pPart->Indices.swap(remap);
}

/*
	Model Loading happens in several stages:
	1. Gather all geometry instances at "Part" granularity with recursion.
	2. Flatten the geometry of each part with recursion.
	3. Compute smooth vertex normals.
	4. Generate index buffer to deduplicate verticee.
*/

bool LdrLoadModel(const char* pFile, LdrState* pData, LdrModel& outModel)
{
	LdrPart* pMainPart = GetPart(pFile, pData);
	if (!pMainPart)
	{
		return false;
	}

	float lduScale = 0.004f;

	ResolveModelParts(pMainPart, pData, outModel, Matrix::CreateScale(lduScale, -lduScale, lduScale));

	for (LdrPart* pPart : outModel.Parts)
	{
		FlattenPart(pPart, pData);
		ComputePartNormals(pPart);
	}

	// Generate indices in a separate loop because part flattening needs unindexed vertices and inner parts may be re-used
	for (LdrPart* pPart : outModel.Parts)
	{
		ComputePartIndices(pPart);
	}

	return true;
}
