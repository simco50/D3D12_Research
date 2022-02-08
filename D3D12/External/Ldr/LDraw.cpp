#include "LDraw.h"

#include <assert.h>

constexpr uint32 MATERIAL_CODE_INHERIT = 16;
constexpr uint32 MATERIAL_CODE_COMPLEMENT = 24;

namespace Util
{
	constexpr char ToLower(char c)
	{
		return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
	}

	constexpr void ToLower(char* pStr)
	{
		while (*pStr)
		{
			*pStr = ToLower(*pStr);
			++pStr;
		}
	};

	template<typename T, size_t N>
	constexpr size_t SizeOf(T(&arr)[N]) { return N; }

	static void MurmurHash(uint32& h, const void* pData, size_t len)
	{
		assert(len % 4 == 0); // Assume 4 byte alignment as we're leaving off the last part of the algorithm
		const uint32 m = 0x5bd1e995;
		const uint32 r = 24;
		const char* pKey = (char*)pData;
		while (len >= 4)
		{
			uint32 k = *(uint32*)(pData);

			k *= m;
			k ^= k >> r;
			k *= m;

			h *= m;
			h ^= k;

			pKey += 4;
			len -= 4;
		}
	}

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
			fopen_s(&pFile, pPath, "rb");
			if (!pFile)
				return false;
			fseek(pFile, 0, SEEK_END);
			size_t size = ftell(pFile);
			pData = new char[size + 1];
			pCurrentLine = (char*)pData;
			fseek(pFile, 0, SEEK_SET);
			size_t read = fread(pData, sizeof(char), size, pFile);
			static_cast<char*>(pData)[size] = '\0';
			fclose(pFile);
			return true;
		}

		~FileReader()
		{
			delete[] pData;
		}

		bool GetLine(const char** pOutLine)
		{
			if (!pCurrentLine)
				return false;

			char* pLine = pCurrentLine;
			char* pNextLine = strchr(pLine, '\n');
			if (pNextLine)
				*pNextLine = 0;

			char* pReturn = strchr(pCurrentLine, '\r');
			if (pReturn)
				*pReturn = 0;

			*pOutLine = pCurrentLine;

			pCurrentLine = pNextLine ? pNextLine + 1 : nullptr;
			return true;
		}

		bool IsOpen() const
		{
			return pData;
		}

		char* pCurrentLine = nullptr;
		void* pData = nullptr;
	};
}

LdrResult LdrInit(const LdrConfig* pConfig, LdrState* pData)
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
	defaultMaterial.Name = "INVALID";
	defaultMaterial.Color = 0x00FF00FF;
	defaultMaterial.EdgeColor = 0x00FF00FF;

	char configPath[256];
	sprintf_s(configPath, Util::SizeOf(configPath), "%sLDConfig.ldr", pConfig->pDatabasePath);
	Util::FileReader reader;
	if (!reader.Open(configPath))
	{
		return LdrResult::Error_FileNotFound;
	}

	const char* pLine = nullptr;
	while (reader.GetLine(&pLine))
	{
		LdrMaterial material;
		material.Type = LdrMaterialFinish::None;

		const uint32 numRequiredValues = 4;
		if (sscanf_s(pLine, "0 !COLOUR %128s CODE %d VALUE #%x EDGE #%x", material.Name.Data, LdrName::SIZE, &material.Code, &material.Color, &material.EdgeColor) == numRequiredValues)
		{
			material.EdgeColor |= 0xFF000000;

			const char* pSearch = nullptr;
			pSearch = strstr(pLine, "ALPHA");
			if (pSearch)
			{
				int alpha = 0;
				sscanf_s(pSearch, "ALPHA %d", &alpha);
				material.Color |= (alpha << 24);
			}
			else
			{
				material.Color |= 0xFF000000;
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
				material.Type = LdrMaterialFinish::Chrome;
			}
			else if (strstr(pLine, "PEARLESCENT"))
			{
				material.Type = LdrMaterialFinish::Pearlescent;
			}
			else if (strstr(pLine, "MATTE_METALLIC"))
			{
				material.Type = LdrMaterialFinish::MatteMetallic;
			}
			else if (strstr(pLine, "METAL"))
			{
				material.Type = LdrMaterialFinish::Metallic;
			}
			else if (strstr(pLine, "RUBBER"))
			{
				material.Type = LdrMaterialFinish::Rubber;
			}
			else if (strstr(pLine, "MATERIAL"))
			{
				pSearch = strstr(pLine, "GLITTER");
				if (pSearch)
				{
					sscanf_s(pSearch, "GLITTER VALUE #%x FRACTION %f VFRACTION %f SIZE %f", &material.Glitter.Color, &material.Glitter.Fraction, &material.Glitter.VFraction, &material.Glitter.Size);
					material.Type = LdrMaterialFinish::Glitter;
				}

				pSearch = strstr(pLine, "SPECKLE");
				if (pSearch)
				{
					sscanf_s(pSearch, "SPECKLE VALUE #%x FRACTION %f MINSIZE %f MAXSIZE %f", &material.Speckle.Color, &material.Speckle.Fraction, &material.Speckle.MinSize, &material.Speckle.MaxSize);
					material.Type = LdrMaterialFinish::Speckle;
				}
			}

			pData->MaterialMap[material.Code] = (uint32)pData->Materials.size();
			pData->Materials.push_back(material);
		}
	}
	return LdrResult::Success;
}

LdrResult ParseLDraw(const char* pPartName, LdrState* pData, std::vector<std::unique_ptr<LdrPart>>& outParts)
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
			sprintf_s(path, Util::SizeOf(path), "%s%s%s", pData->Config.pDatabasePath, location.pLocation, pPartName);
			if (reader.Open(path))
			{
				partType = location.Type;
				break;
			}
		}
	}

	if (!reader.IsOpen())
	{
		return LdrResult::Warning_PartNotFound;
	}

	std::unique_ptr<LdrPart> part = std::make_unique<LdrPart>();
	part->PartType = partType;
	part->Name = pPartName;
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

		if ((uint32)command >= (int)Command::MAX)
			return LdrResult::Error_FileParseError;

		if (command == Command::Meta)
		{
			const char* pSearch = strstr(pLine, "0 BFC");
			if (pSearch)
			{
				if (strstr(pSearch, "INVERTNEXT"))
					invert = true;
				else if (strstr(pSearch, "CCW"))
					ccw = true;
				else if (strstr(pSearch, "CW"))
					ccw = false;
			}

			pSearch = strstr(pLine, "0 FILE");
			if (pSearch)
			{
				if (currentPart.HasData())
				{
					std::unique_ptr<LdrPart> newPart = std::make_unique<LdrPart>();
					newPart->PartType = partType;
					newPart->Name = pSearch + sizeof("0 FILE");
					Util::ToLower(newPart->Name.Data);
					outParts.push_back(std::move(newPart));
				}
			}
		}
		else if (command == Command::Subfile)
		{
			LdrSubfile subfile;
			int curr = 0;
			LdrMatrix& transform = subfile.Transform;

			sscanf_s(pLine, "%d %d %f %f %f %f %f %f %f %f %f %f %f %f %n",
				&dummy, &subfile.Color,
				&transform.m[3][0], &transform.m[3][1], &transform.m[3][2], // XYZ
				&transform.m[0][0], &transform.m[1][0], &transform.m[2][0],	// 3x3
				&transform.m[0][1], &transform.m[1][1], &transform.m[2][1],
				&transform.m[0][2], &transform.m[1][2], &transform.m[2][2],
				&curr
			);
			subfile.Name =  pLine + curr;
			Util::ToLower(subfile.Name.Data);

			subfile.Invert = invert;
			invert = false;
			currentPart.Subfiles.push_back(subfile);
		}
		else if (command == Command::Line)
		{

		}
		else if (command == Command::Triangle)
		{
			uint32 color;
			LdrVector triangle[3];

			auto ParseTriangle = [&](const char* pFormat) {
				return sscanf_s(pLine, pFormat,
					&dummy, &color,
					&triangle[0].x, &triangle[0].y, &triangle[0].z,
					&triangle[1].x, &triangle[1].y, &triangle[1].z,
					&triangle[2].x, &triangle[2].y, &triangle[2].z
				) == 11;
			};

			if (!ParseTriangle("%d %d %f %f %f %f %f %f %f %f %f"))
			{
				// Direct colors are always considered opaque. Set alpha to 255 to indicate this is a direct color.
				ParseTriangle("%d 0x2%x %f %f %f %f %f %f %f %f %f");
				color |= 0xFF000000;
			}

			currentPart.Vertices.push_back(triangle[ccw ? 2 : 0]);
			currentPart.Vertices.push_back(triangle[ccw ? 1 : 1]);
			currentPart.Vertices.push_back(triangle[ccw ? 0 : 2]);

			currentPart.Colors.push_back(color);
			currentPart.Colors.push_back(color);
			currentPart.Colors.push_back(color);

			if (color != MATERIAL_CODE_INHERIT)
				currentPart.IsMultiMaterial = true;
		}
		else if (command == Command::Quad)
		{
			uint32 color;
			LdrVector quad[4];

			auto ParseQuad = [&](const char* pFormat) {
				return sscanf_s(pLine, pFormat,
					&dummy, &color,
					&quad[0].x, &quad[0].y, &quad[0].z,
					&quad[1].x, &quad[1].y, &quad[1].z,
					&quad[2].x, &quad[2].y, &quad[2].z,
					&quad[3].x, &quad[3].y, &quad[3].z
				) == 14;
			};

			if (!ParseQuad("%d %d %f %f %f %f %f %f %f %f %f %f %f %f"))
			{
				// Direct colors are always considered opaque. Set alpha to 255 to indicate this is a direct color.
				ParseQuad("%d 0x2%x %f %f %f %f %f %f %f %f %f %f %f %f");
				color |= 0xFF000000;
			}

			currentPart.Vertices.push_back(quad[ccw ? 0 : 0]);
			currentPart.Vertices.push_back(quad[ccw ? 3 : 1]);
			currentPart.Vertices.push_back(quad[ccw ? 2 : 2]);
			currentPart.Vertices.push_back(quad[ccw ? 2 : 2]);
			currentPart.Vertices.push_back(quad[ccw ? 1 : 3]);
			currentPart.Vertices.push_back(quad[ccw ? 0 : 0]);

			currentPart.Colors.push_back(color);
			currentPart.Colors.push_back(color);
			currentPart.Colors.push_back(color);
			currentPart.Colors.push_back(color);
			currentPart.Colors.push_back(color);
			currentPart.Colors.push_back(color);

			if (color != MATERIAL_CODE_INHERIT)
				currentPart.IsMultiMaterial = true;
		}
	}
	return LdrResult::Success;
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
	if (ParseLDraw(pName, pData, parts) == LdrResult::Success)
	{
		for (std::unique_ptr<LdrPart>& newPart : parts)
		{
			pData->PartMap[newPart->Name] = (uint32)pData->Parts.size();
			pData->Parts.push_back(std::move(newPart));
		}
		return pData->Parts[pData->Parts.size() - parts.size()].get();
	}
	return nullptr;
}

void ResolveModelParts(LdrPart* pPart, LdrState* pData, LdrModel& outModel, const LdrMatrix& transform = LdrMatrix(), uint32 color = 0)
{
	if (pPart->PartType == LdrPart::Type::Part || pPart->Vertices.size() != 0)
	{
		LdrModel::Instance instance;
		instance.Transform = transform;
		instance.Color = color;
		auto it = std::find(outModel.Parts.begin(), outModel.Parts.end(), pPart);
		if (it != outModel.Parts.end())
		{
			instance.Index = (uint32)(it - outModel.Parts.begin());
		}
		else
		{
			instance.Index = (uint32)outModel.Parts.size();
			outModel.Parts.push_back(pPart);
		}
		outModel.Instances.push_back(instance);
	}
	else
	{
		for (LdrSubfile& subfile : pPart->Subfiles)
		{
			LdrPart* pSubpart = GetPart(subfile.Name.c_str(), pData);
			if (pSubpart)
			{
				LdrMatrix scale = subfile.Invert ? LdrMatrix::CreateScale(-1, -1, -1) : LdrMatrix::CreateScale(1, 1, 1);
				ResolveModelParts(pSubpart, pData, outModel, subfile.Transform * transform * scale, subfile.Color == MATERIAL_CODE_INHERIT ? color : subfile.Color);
			}
		}
	}
}

uint32 ResolveTriangleColor(uint32 triangleColor, uint32 parentColor)
{
	return triangleColor == MATERIAL_CODE_INHERIT ? parentColor : triangleColor;
}

void FlattenPart(LdrPart* pPart, LdrState* pData, const LdrMatrix& currentLdrMatrix = LdrMatrix(), bool invert = false, uint32 color = MATERIAL_CODE_INHERIT)
{
	for (const LdrSubfile& subfile : pPart->Subfiles)
	{
		LdrPart* pSubpart = GetPart(subfile.Name.c_str(), pData);
		if (!pSubpart)
			continue;

		bool inv = subfile.Invert;
		float det = subfile.Transform.Determinant3x3();
		inv ^= (det < 0);

		FlattenPart(pSubpart, pData, subfile.Transform * currentLdrMatrix, invert ^ inv, ResolveTriangleColor(subfile.Color, color));

		pPart->IsMultiMaterial |= pSubpart->IsMultiMaterial;

		for (uint32 i = 0; i < pSubpart->Vertices.size(); i += 3)
		{
			pPart->Vertices.push_back(pSubpart->Vertices[i + (inv ? 2u : 0u)].Transform(subfile.Transform));
			pPart->Vertices.push_back(pSubpart->Vertices[i + (inv ? 1u : 1u)].Transform(subfile.Transform));
			pPart->Vertices.push_back(pSubpart->Vertices[i + (inv ? 0u : 2u)].Transform(subfile.Transform));

			pPart->Colors.push_back(ResolveTriangleColor(pSubpart->Colors[i + (inv ? 2u : 0u)], subfile.Color));
			pPart->Colors.push_back(ResolveTriangleColor(pSubpart->Colors[i + (inv ? 1u : 1u)], subfile.Color));
			pPart->Colors.push_back(ResolveTriangleColor(pSubpart->Colors[i + (inv ? 0u : 2u)], subfile.Color));
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
			LdrVector n0 = pPart->Vertices[i + 1] - pPart->Vertices[i + 0];
			LdrVector n1 = pPart->Vertices[i + 2] - pPart->Vertices[i + 0];
			LdrVector normal = n1.Cross(n0).Normalize();
			pPart->Normals[i + 0] = normal;
			pPart->Normals[i + 1] = normal;
			pPart->Normals[i + 2] = normal;
		}

		struct VectorHasher
		{
			size_t operator()(const LdrVector& v) const
			{
				uint32 h = 0;
				Util::MurmurHash(h, &v, sizeof(LdrVector));
				return h;
			}
		};

		std::unordered_map<LdrVector, std::vector<uint32>, VectorHasher> vertexMap;
		for (size_t i = 0; i < pPart->Vertices.size(); ++i)
		{
			vertexMap[pPart->Vertices[i]].push_back((uint32)i);
		}

		const float minAngleCos = cos(3.141592f / 4.0f);

		std::vector<LdrVector> newNormals(pPart->Normals.size());
		for (size_t i = 0; i < pPart->Vertices.size(); ++i)
		{
			const std::vector<uint32>& identicalVertices = vertexMap[pPart->Vertices[i]];
			LdrVector vertexNormal = pPart->Normals[i];
			LdrVector smoothNormal;
			for (uint32 vertexIndex : identicalVertices)
			{
				const LdrVector& otherNormal = pPart->Normals[vertexIndex];
				if (vertexNormal.Dot(otherNormal) > minAngleCos)
				{
					smoothNormal = smoothNormal + otherNormal;
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

	if (pPart->Indices.empty())
	{
		struct HashedVertex
		{
			HashedVertex(const LdrPart* pPart, uint32 vertex)
				: pPart(pPart), Vertex(vertex)
			{}
			const LdrPart* pPart;
			uint32 Vertex;

			struct Hasher
			{
				size_t operator()(const HashedVertex& v) const
				{
					uint32 h = 0;
					Util::MurmurHash(h, v.pPart->Vertices.data() + v.Vertex, sizeof(LdrVector));
					Util::MurmurHash(h, v.pPart->Normals.data() + v.Vertex, sizeof(LdrVector));
					Util::MurmurHash(h, v.pPart->Colors.data() + v.Vertex, sizeof(uint32));
					return h;
				}
			};

			bool operator==(const HashedVertex& rhs) const
			{
				return pPart->Colors[Vertex] == rhs.pPart->Colors[rhs.Vertex] &&
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
			char* pCopy = new char[numElements * elementSize];
			memcpy(pCopy, pData, numElements * elementSize);
			for (uint32 i = 0; i < numElements; ++i)
			{
				memcpy((char*)pData + remap[i] * elementSize, pCopy + i * elementSize, elementSize);
			}
			delete[] pCopy;
		};

		remapBuffer(pPart->Vertices.data(), remap.data(), (uint32)pPart->Vertices.size(), sizeof(LdrVector));
		remapBuffer(pPart->Normals.data(), remap.data(), (uint32)pPart->Normals.size(), sizeof(LdrVector));
		remapBuffer(pPart->Colors.data(), remap.data(), (uint32)pPart->Colors.size(), sizeof(uint32));

		pPart->Vertices.resize(indexCount);
		pPart->Normals.resize(indexCount);
		pPart->Colors.resize(indexCount);
		pPart->Indices.swap(remap);
	}
}

/*
	Model Loading happens in several stages:
	1. Gather all geometry instances at "Part" granularity with recursion.
	2. Flatten the geometry of each part with recursion.
	3. Compute smooth vertex normals.
	4. Generate index buffer to deduplicate verticee.
*/

LdrResult LdrLoadModel(const char* pFile, LdrState* pData, LdrModel& outModel)
{
	outModel.Instances.clear();
	outModel.Parts.clear();

	LdrPart* pMainPart = GetPart(pFile, pData);
	if (!pMainPart)
	{
		return LdrResult::Error_FileNotFound;
	}

	constexpr float lduScale = 0.004f;

	ResolveModelParts(pMainPart, pData, outModel, LdrMatrix::CreateScale(lduScale, -lduScale, lduScale));

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

	return LdrResult::Success;
}

const LdrMaterial& LdrGetMaterial(uint32 code, const LdrState* pData)
{
	auto it = pData->MaterialMap.find(code);
	if (it != pData->MaterialMap.end())
	{
		return pData->Materials[it->second];
	}
	return pData->DefaultMaterial;
}

uint32 LdrResolveVertexColor(uint32 partColor, uint32 vertexColor, const LdrState* pData)
{
	uint32 color = vertexColor == MATERIAL_CODE_INHERIT ? partColor : vertexColor;
	// A color with a value higher than 24-bit max is considered a direct color.
	if (color & 0xFF000000)
	{
		return color;
	}
	return LdrGetMaterial(color, pData).Color;
}

void LdrDecodeARGB(uint32 color, float* pColor)
{
	float normalizeFactor = 1.0f / 255.0f;
	pColor[0] = normalizeFactor * ((color >> 16) & 0xFF);
	pColor[1] = normalizeFactor * ((color >> 8) & 0xFF);
	pColor[2] = normalizeFactor * ((color >> 0) & 0xFF);
	pColor[3] = normalizeFactor * ((color >> 24) & 0xFF);
}
