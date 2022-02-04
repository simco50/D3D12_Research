#pragma once

namespace LDraw
{
	constexpr int MATERIAL_CODE_INHERIT = 16;
	constexpr int MATERIAL_CODE_COMPLEMENT = 24;

	enum class MaterialType
	{
		None,
		Chrome,
		Speckle,
		Pearlescent,
		Rubber,
		Metal,
		Glitter,
	};

	struct Material
	{
		char Name[128];
		uint32 Code = 0;
		uint32 Color = 0;
		uint32 EdgeColor = 0;
		uint32 Emissive = 0;
		uint8 Alpha = 0xFF;
		uint8 Luminance = 0;
		MaterialType Type = MaterialType::None;

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

	struct Subfile
	{
		Matrix Transform;
		char Name[128];
		uint32 Color;
		bool Invert;
	};

	struct Part
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
		std::vector<Subfile> Subfiles;
		Type PartType;

		bool HasData() const
		{
			return Vertices.size() > 0 || Subfiles.size() > 0;
		}
	};

	struct Model
	{
		struct Instance
		{
			int Index;
			int Color;
			Matrix Transform;
		};

		std::vector<Part*> Parts;
		std::vector<Instance> Instances;
	};

	inline void ToLower(char* pStr)
	{
		while(*pStr)
		{
			*pStr = (char)tolower(*pStr);
			++pStr;
		}
	};

	struct Context
	{
		const char* pDatabasePath = "D:/References/ldraw/ldraw/";
		std::vector<std::unique_ptr<Part>> Parts;
		std::map<StringHash, int> PartMap;
		std::vector<Material> Materials;
		std::map<int, int> MaterialMap;
		Material DefaultMaterial;

		struct DatabaseLocation
		{
			const char* pLocation;
			Part::Type Type;
		};

		std::vector<DatabaseLocation> DatabaseLocations;

		const Material& GetMaterial(int code) const
		{
			auto it = MaterialMap.find(code);
			if (it != MaterialMap.end())
			{
				return Materials[it->second];
			}
			return DefaultMaterial;
		}

		bool Init()
		{
			MaterialMap.clear();
			Materials.clear();
			PartMap.clear();
			Parts.clear();

			DatabaseLocations = {
				{ "p/", Part::Type::Primitive },			// Official Primitives
				{ "parts/", Part::Type::Part },				// Official Parts
				{ "models/", Part::Type::Primitive },		// Demo models
				{ "UnOfficial/p/", Part::Type::Primitive },	// Unofficial Primitives
				{ "UnOfficial/parts/", Part::Type::Part },	// Unofficial Parts
			};

			memset(&DefaultMaterial, 0, sizeof(Material));
			strcpy_s(DefaultMaterial.Name, "DefaultMaterial");
			DefaultMaterial.Color = 0x00FF00FF;
			DefaultMaterial.EdgeColor = 0x00FF00FF;

			char configPath[256];
			FormatString(configPath, ARRAYSIZE(configPath), "%sLDConfig.ldr", pDatabasePath);
			std::ifstream fs(configPath);
			if (!fs.is_open())
			{
				return false;
			}

			std::string line;
			while (getline(fs, line))
			{
				Material material;
				material.Type = MaterialType::None;

				const char* pLine = line.c_str();
				const int numRequiredValues = 4;
				if(sscanf_s(pLine, "0 !COLOUR %128s CODE %d VALUE #%x EDGE #%x", material.Name, (uint32)ARRAYSIZE(material.Name), &material.Code, &material.Color, &material.EdgeColor) == numRequiredValues)
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
						material.Type = MaterialType::Chrome;
					}
					else if (strstr(pLine, "PEARLESCENT"))
					{
						material.Type = MaterialType::Pearlescent;
					}
					else if (strstr(pLine, "METAL"))
					{
						material.Type = MaterialType::Metal;
					}
					else if (strstr(pLine, "RUBBER"))
					{
						material.Type = MaterialType::Rubber;
					}
					else if (strstr(pLine, "MATERIAL"))
					{
						pSearch = strstr(pLine, "GLITTER");
						if (pSearch)
						{
							sscanf_s(pSearch, "GLITTER VALUE #%x FRACTION %f VFRACTION %f SIZE %f", &material.Glitter.Color, &material.Glitter.Fraction, &material.Glitter.VFraction, &material.Glitter.Size);
							material.Type = MaterialType::Glitter;
						}

						pSearch = strstr(pLine, "SPECKLE");
						if (pSearch)
						{
							sscanf_s(pSearch, "SPECKLE VALUE #%x FRACTION %f MINSIZE %f MAXSIZE %f", &material.Speckle.Color, &material.Speckle.Fraction, &material.Speckle.MinSize, &material.Speckle.MaxSize);
							material.Type = MaterialType::Speckle;
						}
					}

					MaterialMap[material.Code] = (int)Materials.size();
					Materials.push_back(material);
				}
			}
			return true;
		}

		bool ParseLDraw(const char* pPartName, std::vector<std::unique_ptr<Part>>& outParts)
		{
			outParts.clear();
			Part::Type partType = Part::Type::LocalModel;

			// Try absolute path
			std::ifstream str(pPartName);
			if (str.is_open())
			{
				partType = Part::Type::LocalModel;
			}
			else // Try database path
			{
				for (const DatabaseLocation& location : DatabaseLocations)
				{
					char path[256];
					FormatString(path, ARRAYSIZE(path), "%s%s%s", pDatabasePath, location.pLocation, pPartName);
					str.open(path);
					if (str.is_open())
					{
						partType = location.Type;
						break;
					}
				}
			}

			if (!str.is_open())
			{
				E_LOG(Warning, "Could not find part '%s'", pPartName);
				return false;
			}

			std::unique_ptr<Part> part = std::make_unique<Part>();
			part->PartType = partType;
			strcpy_s(part->Name, pPartName);
			outParts.push_back(std::move(part));

			std::string line;
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

			while (getline(str, line))
			{
				Part& currentPart = *outParts.back();

				if (line.length() <= 1)
					continue;

				const char* pLine = line.c_str();

				int cmd;
				if(sscanf_s(pLine, "%d", &cmd) < 1)
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
							std::unique_ptr<Part> newPart = std::make_unique<Part>();
							newPart->PartType = partType;
							strcpy_s(newPart->Name, pSearch + sizeof("0 FILE"));
							ToLower(newPart->Name);
							outParts.push_back(std::move(newPart));
						}
					}
				}
				else if (command == Command::Subfile)
				{
					Subfile subfile;
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
					ToLower(subfile.Name);

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

		Part* GetPart(const char* pName)
		{
			auto it = PartMap.find(pName);
			if (it != PartMap.end())
			{
				return Parts[it->second].get();
			}

			std::vector<std::unique_ptr<Part>> parts;
			if (ParseLDraw(pName, parts))
			{
				for (std::unique_ptr<Part>& newPart : parts)
				{
					PartMap[newPart->Name] = (int)Parts.size();
					Parts.push_back(std::move(newPart));
				}
				return Parts[Parts.size() - parts.size()].get();
			}
			return nullptr;
		}

		void ResolveModelParts(Part* pPart, Model& outModel, const Matrix& transform = Matrix::Identity, int color = 0)
		{
			if (pPart->PartType == Part::Type::Part)
			{
				Model::Instance instance;
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
				for (Subfile& subfile : pPart->Subfiles)
				{
					Part* pSubpart = GetPart(subfile.Name);
					if (pSubpart)
					{
						Matrix scale = subfile.Invert ? Matrix::CreateScale(-1) : Matrix::CreateScale(1);
						ResolveModelParts(pSubpart, outModel, subfile.Transform * transform * scale, subfile.Color == MATERIAL_CODE_INHERIT ? color : subfile.Color);
					}
				}
			}
		}

		void FlattenPart(Part* pRootPart, Part* pPart, Matrix currentMatrix = Matrix::Identity, bool invert = 0, int color = 0)
		{
			if (pRootPart != pPart)
			{
				for (int i = 0; i < pPart->Vertices.size(); i += 3)
				{
					IntVector3 winding = invert ? IntVector3(2, 1, 0) : IntVector3(0, 1, 2);
					pRootPart->Vertices.push_back(Vector3::Transform(pPart->Vertices[i + winding.x], currentMatrix));
					pRootPart->Vertices.push_back(Vector3::Transform(pPart->Vertices[i + winding.y], currentMatrix));
					pRootPart->Vertices.push_back(Vector3::Transform(pPart->Vertices[i + winding.z], currentMatrix));
				}

				for (int i = 0; i < pPart->Colors.size(); ++i)
				{
					pRootPart->Colors.push_back(pPart->Colors[i] == MATERIAL_CODE_INHERIT ? color : pPart->Colors[i]);
				}
			}

			for (const Subfile& subfile : pPart->Subfiles)
			{
				Part* pSubpart = GetPart(subfile.Name);
				if (!pSubpart)
				{
					continue;
				}

				bool inv = invert ^ subfile.Invert;
				float det = subfile.Transform.Determinant();
				inv ^= (det < 0);

				FlattenPart(pRootPart, pSubpart, subfile.Transform * currentMatrix, inv, subfile.Color == MATERIAL_CODE_INHERIT ? color : subfile.Color);
			}
			//#todo: It would be better to propagate the geometry one level at a time and then clear the subfiles
			//pPart->Subfiles.clear();
		}

		void ComputePartNormals(Part* pPart)
		{
			if (pPart->Normals.empty())
			{
				for (size_t i = 0; i < pPart->Vertices.size(); i += 3)
				{
					Vector3 n0 = pPart->Vertices[i + 1] - pPart->Vertices[i + 0];
					Vector3 n1 = pPart->Vertices[i + 2] - pPart->Vertices[i + 0];
					Vector3 normal = n0.Cross(n1);
					normal.Normalize();
					pPart->Normals.push_back(normal);
					pPart->Normals.push_back(normal);
					pPart->Normals.push_back(normal);
				}

				std::map<Vector3, std::vector<int>> vertexMap;
				for (size_t i = 0; i < pPart->Vertices.size(); ++i)
				{
					vertexMap[pPart->Vertices[i]].push_back((int)i);
				}

				std::vector<Vector3> newNormals(pPart->Normals.size());
				for (size_t i = 0; i < pPart->Vertices.size(); ++i)
				{
					const std::vector<int>& identicalVertices = vertexMap[pPart->Vertices[i]];
					Vector3 vertexNormal = pPart->Normals[i];
					Vector3 smoothNormal;
					for (int vertexIndex : identicalVertices)
					{
						const Vector3& otherNormal = pPart->Normals[vertexIndex];
						if (vertexNormal.Dot(otherNormal) > cos(Math::PIDIV4))
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

		bool LoadModel(const char* pFile, Model& outModel)
		{
			LDraw::Part* pMainPart = GetPart(pFile);
			if (!pMainPart)
			{
				return false;
			}

			ResolveModelParts(pMainPart, outModel, Matrix::CreateScale(1, -1, 1));

			for (Part* pPart : outModel.Parts)
			{
				FlattenPart(pPart, pPart);
				ComputePartNormals(pPart);
			}

			return true;
		}
	};
}
