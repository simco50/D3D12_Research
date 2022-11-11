#include "stdafx.h"
#include "Mesh.h"
#include "Graphics/RHI/CommandContext.h"
#include "Graphics/RHI/Graphics.h"
#include "Graphics/RHI/Texture.h"
#include "Graphics/RHI/Buffer.h"
#include "Graphics/RHI/DynamicResourceAllocator.h"
#include "Core/Paths.h"
#include "Content/Image.h"
#include "Core/Utils.h"
#include "ShaderInterop.h"
#include "Graphics/SceneView.h"

#pragma warning(push)
#pragma warning(disable: 4996) //_CRT_SECURE_NO_WARNINGS
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#pragma warning(pop)

#include "meshoptimizer.h"

#include "LDraw.h"

Mesh::~Mesh()
{
}

bool Mesh::Load(const char* pFilePath, GraphicsDevice* pDevice, CommandContext* pContext, float uniformScale /*= 1.0f*/)
{
	struct MeshData
	{
		uint32 MaterialIndex = 0;
		float ScaleFactor = 1;

		std::vector<Vector3> PositionsStream;
		std::vector<Vector3> NormalsStream;
		std::vector<Vector4> TangentsStream;
		std::vector<Vector2> UVsStream;
		std::vector<Vector4> ColorsStream;
		std::vector<uint32> Indices;

		std::vector<ShaderInterop::Meshlet> Meshlets;
		std::vector<uint32> MeshletVertices;
		std::vector<ShaderInterop::MeshletTriangle> MeshletTriangles;
		std::vector<ShaderInterop::MeshletBounds> MeshletBounds;
	};

	std::vector<MeshData> meshDatas;

	std::string extension = Paths::GetFileExtenstion(pFilePath);
	if (extension == "dat" || extension == "ldr" || extension == "mpd")
	{
		LdrConfig config;
		config.pDatabasePath = "D:/References/ldraw/ldraw/";
		//config.Quality = LdrQuality::High;

		// Logo studs
		config.ReplacementMap.push_back({ "stud.dat", "stud-logo4.dat" });

		// No studs
		//config.ReplacementMap.push_back({ "stud.dat", nullptr });

		LdrState context;
		if (LdrInit(&config, &context) != LdrResult::Success)
			return false;

		LdrModel mdl;
		if (LdrLoadModel(pFilePath, &context, mdl) != LdrResult::Success)
			return false;

		auto FixBaseColor = [](Color& c)
		{
			c.x = powf(c.x, 2.2f);
			c.y = powf(c.y, 2.2f);
			c.z = powf(c.z, 2.2f);
		};

		auto CreateMaterialFromLDraw = [&](int color) {
			Material mat;
			const LdrMaterial& lmat = LdrGetMaterial(color, &context);
			mat.Name = lmat.Name.c_str();
			LdrDecodeARGB(lmat.Color, &mat.BaseColorFactor.x);
			FixBaseColor(mat.BaseColorFactor);
			mat.RoughnessFactor = 0.1f;
			mat.MetalnessFactor = 0.0f;
			mat.AlphaMode = mat.BaseColorFactor.w >= 1 ? MaterialAlphaMode::Opaque : MaterialAlphaMode::Blend;

			if (lmat.Type == LdrMaterialFinish::Metallic)
			{
				mat.MetalnessFactor = 1.0f;
				mat.RoughnessFactor = 0.1f;
			}
			else if (lmat.Type == LdrMaterialFinish::MatteMetallic)
			{
				mat.MetalnessFactor = 1.0f;
				mat.RoughnessFactor = 0.5f;
			}
			else if (lmat.Type == LdrMaterialFinish::Chrome)
			{
				mat.MetalnessFactor = 1.0f;
				mat.RoughnessFactor = 0.0f;
			}
			return mat;
		};

		struct MaterialPartCombination
		{
			LdrPart* pPart;
			uint32 Color;
			uint32 Index;
		};

		// Materials are part of the mesh so instances of the same mesh but different material have to be duplicated :(
		std::vector<MaterialPartCombination> map;

		for (int i = 0; i < (int)mdl.Instances.size(); ++i)
		{
			const LdrModel::Instance& instance = mdl.Instances[i];
			LdrPart* pPart = mdl.Parts[instance.Index];

			SubMeshInstance inst;

			auto it = std::find_if(map.begin(), map.end(), [&](const MaterialPartCombination& cm)
				{
					return cm.Color == instance.Color && cm.pPart == pPart;
				});

			if (it != map.end())
			{
				inst.MeshIndex = it->Index;
			}
			else
			{
				Material material = CreateMaterialFromLDraw(instance.Color);
				if (pPart->IsMultiMaterial)
					material.BaseColorFactor = Color(1, 1, 1, 1);

				MeshData mesh;
				mesh.MaterialIndex = (int)m_Materials.size();
				mesh.Indices.resize(pPart->Indices.size());
				for (int j = 0; j < (int)pPart->Indices.size(); ++j)
				{
					mesh.Indices[j] = pPart->Indices[j];
				}
				mesh.PositionsStream.resize(pPart->Vertices.size());
				mesh.NormalsStream.resize(pPart->Vertices.size());
				mesh.TangentsStream.resize(pPart->Vertices.size());
				if(pPart->IsMultiMaterial)
					mesh.ColorsStream.resize(pPart->Colors.size());
				for (int j = 0; j < (int)pPart->Vertices.size(); ++j)
				{
					mesh.PositionsStream[j] = Vector3(pPart->Vertices[j].x, pPart->Vertices[j].y, pPart->Vertices[j].z);
					mesh.NormalsStream[j] = Vector3(pPart->Normals[j].x, pPart->Normals[j].y, pPart->Normals[j].z);
					mesh.TangentsStream[j] = Vector4(1, 0, 0, 1);

					if (pPart->IsMultiMaterial)
					{
						uint32 vertexColor = LdrResolveVertexColor(instance.Color, pPart->Colors[j], &context);
						Color verColor;
						LdrDecodeARGB(vertexColor, &verColor.x);
						FixBaseColor(verColor);
						mesh.ColorsStream[j] = verColor;
					}
				}

				MaterialPartCombination combination;
				combination.pPart = pPart;
				combination.Color = instance.Color;
				combination.Index = (int)meshDatas.size();
				map.push_back(combination);
				inst.MeshIndex = combination.Index;

				meshDatas.push_back(mesh);
				m_Materials.push_back(material);
			}

			inst.Transform = Matrix(&instance.Transform.m[0][0]);
			m_MeshInstances.push_back(inst);
		}
	}
	else
	{
		cgltf_options options{};
		cgltf_data* pGltfData = nullptr;
		cgltf_result result = cgltf_parse_file(&options, pFilePath, &pGltfData);
		if (result != cgltf_result_success)
		{
			E_LOG(Warning, "GLTF - Failed to load '%s'", pFilePath);
			return false;
		}
		result = cgltf_load_buffers(&options, pGltfData, pFilePath);
		if (result != cgltf_result_success)
		{
			E_LOG(Warning, "GLTF - Failed to load buffers '%s'", pFilePath);
			return false;
		}

		// Load unique textures;
		std::map<const cgltf_image*, Texture*> textureMap;

		auto MaterialIndex = [&](const cgltf_material* pMat) -> int
		{
			if (!pMat)
			{
				return 0;
			}
			return (int)(pMat - pGltfData->materials) + 1;
		};

		using Hash = TStringHash<false>;
		std::vector<Hash> usedExtensions;
		for (uint32 i = 0; i < pGltfData->extensions_used_count; ++i)
		{
			usedExtensions.push_back(pGltfData->extensions_used[i]);
		}
		bool useEmissiveStrength = std::find_if(usedExtensions.begin(), usedExtensions.end(), [](const Hash& rhs) { return rhs == "KHR_materials_emissive_strength"; }) != usedExtensions.end();

		Material defaultMaterial;
		m_Materials.push_back(defaultMaterial);

		m_Materials.reserve(pGltfData->materials_count + 1);
		for (size_t i = 0; i < pGltfData->materials_count; ++i)
		{
			const cgltf_material& gltfMaterial = pGltfData->materials[i];

			m_Materials.push_back(Material());
			Material& material = m_Materials.back();

			auto RetrieveTexture = [this, &textureMap, pContext, pFilePath](const cgltf_texture_view texture, bool srgb) -> Texture*
			{
				if (texture.texture)
				{
					const cgltf_image* pImage = texture.texture->image;
					auto it = textureMap.find(pImage);
					const char* pName = pImage->uri ? pImage->uri : "Material Texture";
					RefCountPtr<Texture> pTex;
					if (it == textureMap.end())
					{
						if (pImage->buffer_view)
						{
							Image newImg;
							if (newImg.Load((char*)pImage->buffer_view->buffer->data + pImage->buffer_view->offset, pImage->buffer_view->size, pImage->mime_type))
							{
								pTex = GraphicsCommon::CreateTextureFromImage(*pContext, newImg, srgb, pName);
							}
						}
						else
						{
							pTex = GraphicsCommon::CreateTextureFromFile(*pContext, Paths::Combine(Paths::GetDirectoryPath(pFilePath), pImage->uri).c_str(), srgb, pName);
						}
						if (pTex.Get())
						{
							m_Textures.push_back(pTex);
							textureMap[pImage] = m_Textures.back();
							return m_Textures.back();
						}
						else
						{
							E_LOG(Warning, "GLTF - Failed to load texture '%s' for '%s'", pImage->uri, pFilePath);
						}
					}
					else
					{
						return it->second;
					}
				}
				return nullptr;
			};

			auto GetAlphaMode = [](cgltf_alpha_mode mode) {
				switch (mode)
				{
				case cgltf_alpha_mode_blend: return MaterialAlphaMode::Blend;
				case cgltf_alpha_mode_opaque: return MaterialAlphaMode::Opaque;
				case cgltf_alpha_mode_mask: return MaterialAlphaMode::Masked;
				}
				return MaterialAlphaMode::Opaque;
			};

			if (gltfMaterial.has_pbr_metallic_roughness)
			{
				material.pDiffuseTexture = RetrieveTexture(gltfMaterial.pbr_metallic_roughness.base_color_texture, true);
				material.pRoughnessMetalnessTexture = RetrieveTexture(gltfMaterial.pbr_metallic_roughness.metallic_roughness_texture, false);
				material.BaseColorFactor.x = gltfMaterial.pbr_metallic_roughness.base_color_factor[0];
				material.BaseColorFactor.y = gltfMaterial.pbr_metallic_roughness.base_color_factor[1];
				material.BaseColorFactor.z = gltfMaterial.pbr_metallic_roughness.base_color_factor[2];
				material.BaseColorFactor.w = gltfMaterial.pbr_metallic_roughness.base_color_factor[3];
				material.MetalnessFactor = gltfMaterial.pbr_metallic_roughness.metallic_factor;
				material.RoughnessFactor = gltfMaterial.pbr_metallic_roughness.roughness_factor;
			}
			else if (gltfMaterial.has_pbr_specular_glossiness)
			{
				material.pDiffuseTexture = RetrieveTexture(gltfMaterial.pbr_specular_glossiness.diffuse_texture, true);
				material.RoughnessFactor = 1.0f - gltfMaterial.pbr_specular_glossiness.glossiness_factor;
				material.BaseColorFactor.x = gltfMaterial.pbr_specular_glossiness.diffuse_factor[0];
				material.BaseColorFactor.y = gltfMaterial.pbr_specular_glossiness.diffuse_factor[1];
				material.BaseColorFactor.z = gltfMaterial.pbr_specular_glossiness.diffuse_factor[2];
				material.BaseColorFactor.w = gltfMaterial.pbr_specular_glossiness.diffuse_factor[3];
			}
			material.AlphaCutoff = gltfMaterial.alpha_cutoff;
			material.AlphaMode = GetAlphaMode(gltfMaterial.alpha_mode);
			material.pEmissiveTexture = RetrieveTexture(gltfMaterial.emissive_texture, true);
			material.EmissiveFactor.x = gltfMaterial.emissive_factor[0];
			material.EmissiveFactor.y = gltfMaterial.emissive_factor[1];
			material.EmissiveFactor.z = gltfMaterial.emissive_factor[2];
			if (useEmissiveStrength)
				material.EmissiveFactor *= gltfMaterial.emissive_strength.emissive_strength;
			material.pNormalTexture = RetrieveTexture(gltfMaterial.normal_texture, false);
			if (gltfMaterial.name)
				material.Name = gltfMaterial.name;
		}

		std::map<const cgltf_mesh*, std::vector<int>> meshToPrimitives;
		int primitiveIndex = 0;

		for (size_t meshIdx = 0; meshIdx < pGltfData->meshes_count; ++meshIdx)
		{
			const cgltf_mesh& mesh = pGltfData->meshes[meshIdx];
			std::vector<int> primitives;
			for (size_t primIdx = 0; primIdx < mesh.primitives_count; ++primIdx)
			{
				const cgltf_primitive& primitive = mesh.primitives[primIdx];
				primitives.push_back(primitiveIndex++);
				MeshData meshData;

				meshData.MaterialIndex = MaterialIndex(primitive.material);
				meshData.Indices.resize(primitive.indices->count);

				constexpr int indexMap[] = { 0, 2, 1 };
				for (size_t i = 0; i < primitive.indices->count; i += 3)
				{
					meshData.Indices[i + 0] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[0]);
					meshData.Indices[i + 1] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[1]);
					meshData.Indices[i + 2] = (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[2]);
				}

				for (size_t attrIdx = 0; attrIdx < primitive.attributes_count; ++attrIdx)
				{
					const cgltf_attribute& attribute = primitive.attributes[attrIdx];
					const char* pName = attribute.name;

					if (strcmp(pName, "POSITION") == 0)
					{
						meshData.PositionsStream.resize(attribute.data->count);
						for (size_t i = 0; i < attribute.data->count; ++i)
						{
							check(cgltf_accessor_read_float(attribute.data, i, &meshData.PositionsStream[i].x, 3));
						}
					}
					else if (strcmp(pName, "NORMAL") == 0)
					{
						meshData.NormalsStream.resize(attribute.data->count);
						for (size_t i = 0; i < attribute.data->count; ++i)
						{
							check(cgltf_accessor_read_float(attribute.data, i, &meshData.NormalsStream[i].x, 3));
						}
					}
					else if (strcmp(pName, "TANGENT") == 0)
					{
						meshData.TangentsStream.resize(attribute.data->count);
						for (size_t i = 0; i < attribute.data->count; ++i)
						{
							check(cgltf_accessor_read_float(attribute.data, i, &meshData.TangentsStream[i].x, 4));
						}
					}
					else if (strcmp(pName, "TEXCOORD_0") == 0)
					{
						meshData.UVsStream.resize(attribute.data->count);
						for (size_t i = 0; i < attribute.data->count; ++i)
						{
							check(cgltf_accessor_read_float(attribute.data, i, &meshData.UVsStream[i].x, 2));
						}
					}
					else if (strcmp(pName, "COLOR_0") == 0)
					{
						meshData.ColorsStream.resize(attribute.data->count);
						for (size_t i = 0; i < attribute.data->count; ++i)
						{
							check(cgltf_accessor_read_float(attribute.data, i, &meshData.ColorsStream[i].x, 4));
						}
					}
					else
					{
						validateOncef(false, "GLTF - Attribute '%s' is unsupported", pName);
					}
				}

				for (const Vector3& position : meshData.PositionsStream)
				{
					meshData.ScaleFactor = Math::Max(fabs(position.x), meshData.ScaleFactor);
					meshData.ScaleFactor = Math::Max(fabs(position.y), meshData.ScaleFactor);
					meshData.ScaleFactor = Math::Max(fabs(position.z), meshData.ScaleFactor);
				}
				for (Vector3& position : meshData.PositionsStream)
				{
					position /= meshData.ScaleFactor;
					check(fabs(position.x) <= 1.0f && fabs(position.y) <= 1.0f && fabs(position.z) <= 1.0f);
				}
				meshDatas.push_back(meshData);
			}
			meshToPrimitives[&mesh] = primitives;
		}

		for (size_t i = 0; i < pGltfData->nodes_count; i++)
		{
			const cgltf_node& node = pGltfData->nodes[i];

			if (node.mesh)
			{
				Matrix localToWorld;
				cgltf_node_transform_world(&node, &localToWorld.m[0][0]);

				for (int primitive : meshToPrimitives[node.mesh])
				{
					const MeshData& meshData = meshDatas[primitive];
					SubMeshInstance& newNode = m_MeshInstances.emplace_back();
					newNode.MeshIndex = primitive;
					newNode.Transform = Matrix::CreateScale(meshData.ScaleFactor) * localToWorld * Matrix::CreateScale(uniformScale, uniformScale, -uniformScale);
				}
			}
		}

		cgltf_free(pGltfData);
	}

	uint64 bufferSize = 0;
	struct TVertexPositionStream { Vector2u P; };
	struct TVertexNormalStream { uint32 N; uint32 T; };
	struct TVertexColorStream { uint32 C; };
	struct TVertexUVStream { uint32 UV; };

	for (MeshData& meshData : meshDatas)
	{
		meshopt_optimizeVertexCache(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), meshData.PositionsStream.size());

		meshopt_optimizeOverdraw(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), &meshData.PositionsStream[0].x, meshData.PositionsStream.size(), sizeof(Vector3), 1.05f);

		std::vector<uint32> remap(meshData.PositionsStream.size());
		meshopt_optimizeVertexFetchRemap(&remap[0], meshData.Indices.data(), meshData.Indices.size(), meshData.PositionsStream.size());
		meshopt_remapIndexBuffer(meshData.Indices.data(), meshData.Indices.data(), meshData.Indices.size(), &remap[0]);
		meshopt_remapVertexBuffer(meshData.PositionsStream.data(), meshData.PositionsStream.data(), meshData.PositionsStream.size(), sizeof(Vector3), &remap[0]);
		meshopt_remapVertexBuffer(meshData.NormalsStream.data(), meshData.NormalsStream.data(), meshData.NormalsStream.size(), sizeof(Vector3), &remap[0]);
		meshopt_remapVertexBuffer(meshData.TangentsStream.data(), meshData.TangentsStream.data(), meshData.TangentsStream.size(), sizeof(Vector4), &remap[0]);
		meshopt_remapVertexBuffer(meshData.UVsStream.data(), meshData.UVsStream.data(), meshData.UVsStream.size(), sizeof(Vector2), &remap[0]);
		if(!meshData.ColorsStream.empty())
			meshopt_remapVertexBuffer(meshData.ColorsStream.data(), meshData.ColorsStream.data(), meshData.ColorsStream.size(), sizeof(Vector4), &remap[0]);

		// Meshlet generation
		const size_t maxVertices = ShaderInterop::MESHLET_MAX_VERTICES;
		const size_t maxTriangles = ShaderInterop::MESHLET_MAX_TRIANGLES;
		const size_t maxMeshlets = meshopt_buildMeshletsBound(meshData.Indices.size(), maxVertices, maxTriangles);

		meshData.Meshlets.resize(maxMeshlets);
		meshData.MeshletVertices.resize(maxMeshlets * maxVertices);

		std::vector<unsigned char> meshletTriangles(maxMeshlets * maxTriangles * 3);
		std::vector<meshopt_Meshlet> meshlets(maxMeshlets);

		size_t meshlet_count = meshopt_buildMeshlets(meshlets.data(), meshData.MeshletVertices.data(), meshletTriangles.data(),
			meshData.Indices.data(), meshData.Indices.size(), &meshData.PositionsStream[0].x, meshData.PositionsStream.size(), sizeof(Vector3), maxVertices, maxTriangles, 0);

		// Trimming
		const meshopt_Meshlet& last = meshlets[meshlet_count - 1];
		meshletTriangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
		meshlets.resize(meshlet_count);

		meshData.MeshletVertices.resize(last.vertex_offset + last.vertex_count);
		meshData.Meshlets.resize(meshlet_count);
		meshData.MeshletBounds.resize(meshlet_count);
		meshData.MeshletTriangles.resize(meshletTriangles.size() / 3);

		uint32 triangleOffset = 0;
		for (size_t i = 0; i < meshlet_count; ++i)
		{
			const meshopt_Meshlet& meshlet = meshlets[i];

			Vector3 min = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
			Vector3 max = Vector3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (uint32 k = 0; k < meshlet.triangle_count * 3; ++k)
			{
				uint32 idx = meshData.MeshletVertices[meshlet.vertex_offset + meshletTriangles[meshlet.triangle_offset + k]];
				const Vector3& p = meshData.PositionsStream[idx];
				max = Vector3::Max(max, p);
				min = Vector3::Min(min, p);
			}
			ShaderInterop::MeshletBounds& outBounds = meshData.MeshletBounds[i];
			outBounds.Center = (max + min) / 2;
			outBounds.Extents = (max - min) / 2;

			// Encode triangles and get rid of 4 byte padding
			unsigned char* pSourceTriangles = meshletTriangles.data() + meshlet.triangle_offset;
			for (uint32 triIdx = 0; triIdx < meshlet.triangle_count; ++triIdx)
			{
				ShaderInterop::MeshletTriangle& tri = meshData.MeshletTriangles[triIdx + triangleOffset];
				tri.V0 = *pSourceTriangles++;
				tri.V1 = *pSourceTriangles++;
				tri.V2 = *pSourceTriangles++;
			}

			ShaderInterop::Meshlet& outMeshlet = meshData.Meshlets[i];
			outMeshlet.TriangleCount = meshlet.triangle_count;
			outMeshlet.TriangleOffset = triangleOffset;
			outMeshlet.VertexCount = meshlet.vertex_count;
			outMeshlet.VertexOffset = meshlet.vertex_offset;
			triangleOffset += meshlet.triangle_count;
		}
		meshData.MeshletTriangles.resize(triangleOffset);

		bufferSize += Math::AlignUp<uint64>(meshData.Indices.size() * sizeof(uint32), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.PositionsStream.size() * sizeof(TVertexPositionStream), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.UVsStream.size() * sizeof(TVertexUVStream), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.NormalsStream.size() * sizeof(TVertexNormalStream), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.ColorsStream.size() * sizeof(TVertexColorStream), 16);

		bufferSize += Math::AlignUp<uint64>(meshData.Meshlets.size() * sizeof(ShaderInterop::Meshlet), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.MeshletVertices.size() * sizeof(uint32), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.MeshletTriangles.size() * sizeof(ShaderInterop::MeshletTriangle), 16);
		bufferSize += Math::AlignUp<uint64>(meshData.MeshletBounds.size() * sizeof(ShaderInterop::MeshletBounds), 16);
	}

	m_pGeometryData = pDevice->CreateBuffer(BufferDesc::CreateBuffer(bufferSize, BufferFlag::ShaderResource | BufferFlag::ByteAddress), "Geometry Buffer");
	DynamicAllocation allocation = pContext->AllocateTransientMemory(bufferSize);

	uint64 dataOffset = 0;
	auto CopyData = [&dataOffset, &allocation](const void* pSource, uint64 size)
	{
		checkf(dataOffset < std::numeric_limits<uint32>::max(), "Offset stored in 32-bit int");
		memcpy(static_cast<char*>(allocation.pMappedMemory) + dataOffset, pSource, size);
		dataOffset = Math::AlignUp(dataOffset + size, 16ull);
	};

	for (const MeshData& meshData : meshDatas)
	{
		BoundingBox bounds;
		bounds.CreateFromPoints(bounds, meshData.PositionsStream.size(), (DirectX::XMFLOAT3*)meshData.PositionsStream.data(), sizeof(Vector3));

		SubMesh subMesh;
		subMesh.Bounds = bounds;
		subMesh.MaterialId = meshData.MaterialIndex;
		subMesh.PositionsFormat = ResourceFormat::RGBA16_SNORM;

		{
			subMesh.PositionStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.PositionsStream.size(), sizeof(TVertexPositionStream), dataOffset);
			std::vector<TVertexPositionStream> positionStream;
			positionStream.reserve(meshData.PositionsStream.size());
			Utils::Transform(meshData.PositionsStream, positionStream, [](const Vector3& value) -> TVertexPositionStream { return { Math::Pack_RGBA16_SNORM(Vector4(value)) }; });
			CopyData(positionStream.data(), sizeof(TVertexPositionStream) * meshData.PositionsStream.size());
		}

		{
			subMesh.NormalStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.NormalsStream.size(), sizeof(TVertexNormalStream), dataOffset);
			std::vector<TVertexNormalStream> normalStream;
			normalStream.reserve(meshData.NormalsStream.size());
			for (size_t i = 0; i < meshData.NormalsStream.size(); ++i)
			{
				Vector4 tangent = meshData.TangentsStream.empty() ? Vector4(1, 0, 0, 1) : meshData.TangentsStream[i];
				normalStream.push_back({
						Math::Pack_RGB10A2_SNORM(Vector4(meshData.NormalsStream[i])),
						Math::Pack_RGB10A2_SNORM(tangent),
					});
			}
			CopyData(normalStream.data(), sizeof(TVertexNormalStream) * normalStream.size());
		}

		if (!meshData.ColorsStream.empty())
		{
			subMesh.ColorsStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.ColorsStream.size(), sizeof(TVertexColorStream), dataOffset);
			std::vector<TVertexColorStream> colorStream;
			colorStream.reserve(meshData.ColorsStream.size());
			Utils::Transform(meshData.ColorsStream, colorStream, [](const Vector4& value) -> TVertexColorStream { return { Math::Pack_RGBA8_UNORM(value) }; });
			CopyData(colorStream.data(), sizeof(TVertexColorStream)* colorStream.size());
		}

		if (!meshData.UVsStream.empty())
		{
			subMesh.UVStreamLocation = VertexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.UVsStream.size(), sizeof(TVertexUVStream), dataOffset);
			std::vector<TVertexUVStream> uvStream;
			uvStream.reserve(meshData.UVsStream.size());
			Utils::Transform(meshData.UVsStream, uvStream, [](const Vector2& value) -> TVertexUVStream { return { Math::Pack_RG16_FLOAT(value) }; });
			CopyData(uvStream.data(), sizeof(TVertexUVStream) * uvStream.size());
		}

		if (meshData.PositionsStream.size() < std::numeric_limits<uint16>::max())
		{
			subMesh.IndicesLocation = IndexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.Indices.size(), ResourceFormat::R16_UINT, dataOffset);
			std::vector<uint16> indicesStream;
			indicesStream.reserve(meshData.Indices.size());
			Utils::Transform(meshData.Indices, indicesStream, [](const uint32 value) -> uint16 { assert(value < std::numeric_limits<uint16>::max());  return (uint16)value; });
			CopyData(indicesStream.data(), sizeof(uint16) * indicesStream.size());
		}
		else
		{
			subMesh.IndicesLocation = IndexBufferView(m_pGeometryData->GetGpuHandle() + dataOffset, (uint32)meshData.Indices.size(), ResourceFormat::R32_UINT, dataOffset);
			CopyData(meshData.Indices.data(), sizeof(uint32) * meshData.Indices.size());
		}

		subMesh.MeshletsLocation = (uint32)dataOffset;
		CopyData(meshData.Meshlets.data(), sizeof(ShaderInterop::Meshlet) * meshData.Meshlets.size());

		subMesh.MeshletVerticesLocation = (uint32)dataOffset;
		CopyData(meshData.MeshletVertices.data(), sizeof(uint32) * meshData.MeshletVertices.size());

		subMesh.MeshletTrianglesLocation = (uint32)dataOffset;
		CopyData(meshData.MeshletTriangles.data(), sizeof(ShaderInterop::MeshletTriangle) * meshData.MeshletTriangles.size());

		subMesh.MeshletBoundsLocation = (uint32)dataOffset;
		CopyData(meshData.MeshletBounds.data(), sizeof(ShaderInterop::MeshletBounds) * meshData.MeshletBounds.size());

		subMesh.NumMeshlets = (uint32)meshData.Meshlets.size();

		subMesh.pParent = this;
		m_Meshes.push_back(subMesh);
	}

	pContext->CopyBuffer(allocation.pBackingResource, m_pGeometryData, bufferSize, allocation.Offset, 0);

	return true;
}

