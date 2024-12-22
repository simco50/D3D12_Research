#include "stdafx.h"
#include "SoftwareRaster.h"
#include "Core/Image.h"
#include "RHI/Texture.h"
#include "RHI/Buffer.h"
#include "RHI/ResourceViews.h"
#include "RHI/PipelineState.h"
#include "RHI/Device.h"
#include "Renderer/Renderer.h"
#include "Renderer/Techniques/MeshletRasterizer.h"
#include "RenderGraph/RenderGraph.h"

SoftwareRaster::SoftwareRaster(GraphicsDevice* pDevice)
{
	m_pRasterPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "RasterCompute.hlsl", "RasterizeCS");
	m_pRasterVisualizePSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "RasterCompute.hlsl", "ResolveVisBufferCS");
	m_pBuildRasterArgsPSO = pDevice->CreateComputePipeline(GraphicsCommon::pCommonRS, "RasterCompute.hlsl", "BuildRasterArgsCS");
}

void SoftwareRaster::Render(RGGraph& graph, const RenderView* pView, const RasterContext& rasterContext)
{
	const Vector2u viewDimensions = pView->GetDimensions();

	RGBuffer* pRasterArgs = graph.Create("Raster Args", BufferDesc::CreateIndirectArguments<D3D12_DISPATCH_ARGUMENTS>(1));
	graph.AddPass("Raster Args", RGPassFlag::Compute)
		.Read({ rasterContext.pVisibleMeshletsCounter })
		.Write({ pRasterArgs })
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pBuildRasterArgsPSO);

				context.BindResources(BindingSlot::UAV, resources.GetUAV(pRasterArgs));
				context.BindResources(BindingSlot::SRV, resources.GetUAV(rasterContext.pVisibleMeshletsCounter));
				context.Dispatch(1);
			});

	RGTexture* pRasterOutput = graph.Create("Raster Output", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, ResourceFormat::RG32_UINT));
	graph.AddPass("Raster", RGPassFlag::Compute)
		.Read({ rasterContext.pVisibleMeshlets, pRasterArgs })
		.Write(pRasterOutput)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.ClearTextureUInt(resources.Get(pRasterOutput));

				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pRasterPSO);

				Renderer::BindViewUniforms(context, *pView);
				context.BindResources(BindingSlot::UAV, resources.GetUAV(pRasterOutput));
				context.BindResources(BindingSlot::SRV, {
						resources.GetSRV(rasterContext.pVisibleMeshlets),
					});

				context.ExecuteIndirect(GraphicsCommon::pIndirectDispatchSignature, 1, resources.Get(pRasterArgs));
			});


	RGTexture* pDebug = graph.Create("Output", TextureDesc::Create2D(viewDimensions.x, viewDimensions.y, ResourceFormat::RGBA8_UNORM));
	graph.AddPass("Raster Debug", RGPassFlag::Compute)
		.Read(pRasterOutput)
		.Write(pDebug)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetComputeRootSignature(GraphicsCommon::pCommonRS);
				context.SetPipelineState(m_pRasterVisualizePSO);

				Renderer::BindViewUniforms(context, *pView);
				context.BindResources(BindingSlot::UAV, resources.GetUAV(pDebug));
				context.BindResources(BindingSlot::SRV, resources.GetSRV(pRasterOutput));

				context.Dispatch(ComputeUtils::GetNumThreadGroups(pDebug->GetDesc().Width, 16, pDebug->GetDesc().Height, 16));
			});
}


#pragma warning(push)
#pragma warning(disable: 4996) //_CRT_SECURE_NO_WARNINGS
#include <cgltf.h>
#pragma warning(pop)

struct Vertex
{
	Vector3 Position;
	Vector3 Normal;
	Vector2 UV;
};

struct Geometry
{
	Array<Vertex> Vertices;
	Array<uint32> Indices;
	Matrix World;
};

static Geometry GetMesh(const char* pFilePath)
{
	Geometry geo;

	cgltf_options options{};
	cgltf_data* pGltfData = nullptr;
	cgltf_result result = cgltf_parse_file(&options, pFilePath, &pGltfData);
	if (result != cgltf_result_success)
	{
		E_LOG(Warning, "GLTF - Failed to load '%s'", pFilePath);
		return geo;
	}
	result = cgltf_load_buffers(&options, pGltfData, pFilePath);
	if (result != cgltf_result_success)
	{
		E_LOG(Warning, "GLTF - Failed to load buffers '%s'", pFilePath);
		return geo;
	}

	uint32 idx = 0;
	for (size_t meshIdx = idx; meshIdx < pGltfData->meshes_count; ++meshIdx)
	{
		const cgltf_mesh& mesh = pGltfData->meshes[meshIdx];
		Array<int> primitives;
		for (size_t primIdx = 0; primIdx < mesh.primitives_count; ++primIdx)
		{
			const cgltf_primitive& primitive = mesh.primitives[primIdx];

			uint32 indexOffset = (uint32)geo.Indices.size();
			uint32 vertexOffset = (uint32)geo.Vertices.size();
			geo.Indices.resize(indexOffset + primitive.indices->count);

			constexpr int indexMap[] = { 0, 2, 1 };
			for (size_t i = 0; i < primitive.indices->count; i += 3)
			{
				geo.Indices[indexOffset + i + 0] = vertexOffset + (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[0]);
				geo.Indices[indexOffset + i + 1] = vertexOffset + (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[1]);
				geo.Indices[indexOffset + i + 2] = vertexOffset + (int)cgltf_accessor_read_index(primitive.indices, i + indexMap[2]);
			}

			geo.Vertices.resize(vertexOffset + primitive.attributes[0].data->count);

			for (size_t attrIdx = 0; attrIdx < primitive.attributes_count; ++attrIdx)
			{
				const cgltf_attribute& attribute = primitive.attributes[attrIdx];
				const char* pName = attribute.name;

				if (strcmp(pName, "POSITION") == 0)
				{
					for (size_t i = 0; i < attribute.data->count; ++i)
						gVerify(cgltf_accessor_read_float(attribute.data, i, &geo.Vertices[vertexOffset + i].Position.x, 3), == 1);
				}
				else if (strcmp(pName, "NORMAL") == 0)
				{
					for (size_t i = 0; i < attribute.data->count; ++i)
						gVerify(cgltf_accessor_read_float(attribute.data, i, &geo.Vertices[vertexOffset + i].Normal.x, 3), == 1);
				}
				else if (strcmp(pName, "TEXCOORD_0") == 0)
				{
					for (size_t i = 0; i < attribute.data->count; ++i)
						gVerify(cgltf_accessor_read_float(attribute.data, i, &geo.Vertices[vertexOffset + i].UV.x, 2), == 1);
				}
			}
		}
	}

	cgltf_free(pGltfData);
	return geo;
}

static Geometry GetCube()
{
	Geometry geo;

	// Define the vertices for the cube
	const float halfSize = 1.0f;
	geo.Vertices = {
		// Front face
		{ {-halfSize, -halfSize, -halfSize}, {0, 0, -1} },
		{ { halfSize, -halfSize, -halfSize}, {0, 0, -1} },
		{ { halfSize,  halfSize, -halfSize}, {0, 0, -1} },
		{ {-halfSize,  halfSize, -halfSize}, {0, 0, -1} },

		// Back face
		{ { halfSize, -halfSize,  halfSize}, {0, 0, 1} },
		{ {-halfSize, -halfSize,  halfSize}, {0, 0, 1} },
		{ {-halfSize,  halfSize,  halfSize}, {0, 0, 1} },
		{ { halfSize,  halfSize,  halfSize}, {0, 0, 1} },

		// Top face
		{ {-halfSize,  halfSize, -halfSize}, {0, 1, 0} },
		{ { halfSize,  halfSize, -halfSize}, {0, 1, 0} },
		{ { halfSize,  halfSize,  halfSize}, {0, 1, 0} },
		{ {-halfSize,  halfSize,  halfSize}, {0, 1, 0} },

		// Bottom face
		{ {-halfSize, -halfSize,  halfSize}, {0, -1, 0} },
		{ { halfSize, -halfSize,  halfSize}, {0, -1, 0} },
		{ { halfSize, -halfSize, -halfSize}, {0, -1, 0} },
		{ {-halfSize, -halfSize, -halfSize}, {0, -1, 0} },

		// Right face
		{ { halfSize, -halfSize, -halfSize}, {1, 0, 0} },
		{ { halfSize, -halfSize,  halfSize}, {1, 0, 0} },
		{ { halfSize,  halfSize,  halfSize}, {1, 0, 0} },
		{ { halfSize,  halfSize, -halfSize}, {1, 0, 0} },

		// Left face
		{ {-halfSize, -halfSize,  halfSize}, {-1, 0, 0} },
		{ {-halfSize, -halfSize, -halfSize}, {-1, 0, 0} },
		{ {-halfSize,  halfSize, -halfSize}, {-1, 0, 0} },
		{ {-halfSize,  halfSize,  halfSize}, {-1, 0, 0} }
	};

	// Define the indices for the cube
	geo.Indices = {
		// Front face
		0, 1, 2,
		0, 2, 3,

		// Back face
		4, 5, 6,
		4, 6, 7,

		// Top face
		8, 9, 10,
		8, 10, 11,

		// Bottom face
		12, 13, 14,
		12, 14, 15,

		// Right face
		16, 17, 18,
		16, 18, 19,

		// Left face
		20, 21, 22,
		20, 22, 23,
	};

	return geo;
}

static Geometry GetSphere()
{
	// Define the number of rows and columns for the sphere
	const int Rows = 8;
	const int Columns = 16;

	Geometry geo;

	// Define the vertices for the sphere
	geo.Vertices.resize((Rows + 1) * (Columns + 1));

	int vertexIndex = 0;
	for (int row = 0; row <= Rows; ++row) {
		float v = (float)row / Rows;
		float theta1 = v * Math::PI;

		for (int col = 0; col <= Columns; ++col) {
			float u = (float)col / Columns;
			float theta2 = u * Math::PI * 2;

			float x = sin(theta1) * cos(theta2);
			float y = cos(theta1);
			float z = sin(theta1) * sin(theta2);

			Vector3 position(x, y, z);
			Vector3 normal = position;
			normal.Normalize();

			geo.Vertices[vertexIndex++] = { Vector3(x, y, z), normal };
		}
	}

	// Define the indices for the sphere
	const int NumIndices = Rows * Columns * 6;
	geo.Indices.resize(NumIndices);
	int index = 0;
	for (int row = 0; row < Rows; ++row) {
		for (int col = 0; col < Columns; ++col) {
			uint32_t topLeft = (row * (Columns + 1)) + col;
			uint32_t topRight = topLeft + 1;
			uint32_t bottomLeft = ((row + 1) * (Columns + 1)) + col;
			uint32_t bottomRight = bottomLeft + 1;

			geo.Indices[index++] = topLeft;
			geo.Indices[index++] = bottomLeft;
			geo.Indices[index++] = topRight;

			geo.Indices[index++] = topRight;
			geo.Indices[index++] = bottomLeft;
			geo.Indices[index++] = bottomRight;
		}
	}
	return geo;
}

template<typename T>
static T Interpolate(const T& v0, const T& v1, const T& v2, const Vector3& bary)
{
	return v0 * bary.x + v1 * bary.y + v2 * bary.z;
}


static float EdgeFunction(const Vector2& a, const Vector2& b, const Vector2& c)
{
	return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
};

void SoftwareRaster::RasterizeTest()
{
	int width = 1024;
	int height = 1024;

	Vector3 viewPos = Vector3(-3.0f, 2.0f, -6.0f);
	Matrix worldToView = DirectX::XMMatrixLookAtLH(viewPos, Vector3::Zero, Vector3::Up);
	Matrix projection = Math::CreatePerspectiveMatrix(60.0f * Math::DegreesToRadians, (float)width / height, 0.5f, 100.0f);
	Matrix worldToProjection = worldToView * projection;

	Vector3 lightDirection = Vector3(0.2f, -2.0f, 1.0f);
	lightDirection.Normalize();

	Geometry geometries[] = {
		GetCube(),
	};

	geometries[0].World = Matrix::CreateTranslation(0.0f, 0.0f, 0.0f);

	uint32* pPixels = new uint32[width * height];
	float* pDepth = new float[width * height];
	for (int i = 0; i < width * height; ++i)
	{
		pDepth[i] = 1.0f;
		pPixels[i] = Math::Pack_RGBA8_UNORM(Color(0.1f, 0.3f, 0.5f, 1.0f));
	}

	for (const Geometry& geo : geometries)
	{
		const Array<uint32>& indices = geo.Indices;
		const Array<Vertex>& vertices = geo.Vertices;

		for (int i = 0; i < (int)indices.size(); i += 3)
		{
			// Vertex shader
			Vertex v0 = vertices[indices[i + 0]];
			Vertex v1 = vertices[indices[i + 1]];
			Vertex v2 = vertices[indices[i + 2]];

			Vector3 wPos0 = Vector3::Transform(v0.Position, geo.World);
			Vector3 wPos1 = Vector3::Transform(v1.Position, geo.World);
			Vector3 wPos2 = Vector3::Transform(v2.Position, geo.World);

			Vector4 clipPositions[] = {
				Vector4::Transform(Vector4(wPos0.x, wPos0.y, wPos0.z, 1), worldToProjection),
				Vector4::Transform(Vector4(wPos1.x, wPos1.y, wPos1.z, 1), worldToProjection),
				Vector4::Transform(Vector4(wPos2.x, wPos2.y, wPos2.z, 1), worldToProjection),
			};

			// Perspective divide
			clipPositions[0] = Vector4(clipPositions[0].x / clipPositions[0].w, -clipPositions[0].y / clipPositions[0].w, clipPositions[0].z / clipPositions[0].w, clipPositions[0].w);
			clipPositions[1] = Vector4(clipPositions[1].x / clipPositions[1].w, -clipPositions[1].y / clipPositions[1].w, clipPositions[1].z / clipPositions[1].w, clipPositions[1].w);
			clipPositions[2] = Vector4(clipPositions[2].x / clipPositions[2].w, -clipPositions[2].y / clipPositions[2].w, clipPositions[2].z / clipPositions[2].w, clipPositions[2].w);

			// Viewport transform
			Vector2 viewportPos[] = {
				Vector2(((clipPositions[0].x * 0.5f + 0.5f) * width), ((clipPositions[0].y * 0.5f + 0.5f) * height)),
				Vector2(((clipPositions[1].x * 0.5f + 0.5f) * width), ((clipPositions[1].y * 0.5f + 0.5f) * height)),
				Vector2(((clipPositions[2].x * 0.5f + 0.5f) * width), ((clipPositions[2].y * 0.5f + 0.5f) * height)),
			};

			// Rasterization
			Vector2 minBounds = Vector2(100000000, 100000000);
			Vector2 maxBounds = Vector2(0, 0);

			minBounds = Vector2::Min(minBounds, viewportPos[0]);
			minBounds = Vector2::Min(minBounds, viewportPos[1]);
			minBounds = Vector2::Min(minBounds, viewportPos[2]);
			maxBounds = Vector2::Max(maxBounds, viewportPos[0]);
			maxBounds = Vector2::Max(maxBounds, viewportPos[1]);
			maxBounds = Vector2::Max(maxBounds, viewportPos[2]);

			Vector2 v01 = viewportPos[1] - viewportPos[0];
			Vector2 v02 = viewportPos[2] - viewportPos[0];
			float det = v01.x * v02.y - v01.y * v02.x;
			if (det >= 0.0f)
				continue; // backface
			float rcpDet = -1.0f / det;

			for (uint32 y = (uint32)minBounds.y; y <= (uint32)maxBounds.y; ++y)
			{
				for (uint32 x = (uint32)minBounds.x; x <= (uint32)maxBounds.x; ++x)
				{
					Vector2 pixel((float)x + 0.5f, (float)y + 0.5f);

					float w0 = EdgeFunction(viewportPos[1], viewportPos[2], pixel);
					float w1 = EdgeFunction(viewportPos[2], viewportPos[0], pixel);
					float w2 = EdgeFunction(viewportPos[0], viewportPos[1], pixel);
					if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
					{
						// Attribute interpolation
						w0 *= rcpDet;
						w1 *= rcpDet;
						w2 *= rcpDet;
						float z = clipPositions[0].z * w0 + clipPositions[1].z * w1 + clipPositions[2].z * w2;

						// Depth test
						if (z >= pDepth[x + y * width])
							continue;

						pDepth[x + y * width] = z;
						Vector3 bary(w0, w1, w2);

						// Pixel shader
						Vector3 n = Interpolate(v0.Normal, v1.Normal, v2.Normal, bary);
						n.Normalize();
						Vector2 uv = Interpolate(v0.UV, v1.UV, v2.UV, bary);

						float d = n.Dot(-lightDirection);
						d = Math::Clamp(d, 0.0f, 1.0f);

						// Output
						Color c(d, d, d, 1.0f);
						pPixels[x + y * width] = Math::Pack_RGBA8_UNORM(c);
					}
				}
			}
		}
	}

	{
		Image img(width, height, 1, ResourceFormat::RGBA8_UNORM, 1, pPixels);
		img.Save("Output.png");
	}

	uint32* pDepthPixels = new uint32[width * height];
	{
		for (int i = 0; i < width * height; ++i)
		{
			float d = pDepth[i];
			pDepthPixels[i] = Math::Pack_RGBA8_UNORM(Vector4(d, d, d, 1.0f));
		}

		Image img(width, height, 1, ResourceFormat::RGBA8_UNORM, 1, pDepthPixels);
		img.Save("Depth.png");
	}

	delete[] pDepthPixels;
	delete[] pPixels;
	delete[] pDepth;
}

