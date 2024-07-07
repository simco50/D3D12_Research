#include "stdafx.h"
#include "DebugRenderer.h"
#include "RHI/Device.h"
#include "RHI/PipelineState.h"
#include "RHI/RootSignature.h"
#include "RHI/CommandContext.h"
#include "Renderer/Light.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/SceneView.h"

struct DebugSphere
{
	DebugSphere(const Vector3& center, const float radius) :
		Center(center), Radius(radius)
	{}

	Vector3 Center;
	float Radius;

	Vector3 GetPoint(const float theta, const float phi) const
	{
		return Center + GetLocalPoint(theta, phi);
	}

	Vector3 GetLocalPoint(const float theta, const float phi) const
	{
		return Vector3(
			Radius * sin(theta) * sin(phi),
			Radius * cos(phi),
			Radius * cos(theta) * sin(phi)
		);
	}
};

DebugRenderer* DebugRenderer::Get()
{
	static DebugRenderer instance;
	return &instance;
}

void DebugRenderer::Initialize(GraphicsDevice* pDevice)
{
	m_pRS = new RootSignature(pDevice);
	m_pRS->AddRootSRV(0);
	m_pRS->AddRootCBV(100);
	m_pRS->Finalize("Primitive Debug Render");

	PipelineStateInitializer psoDesc;
	psoDesc.SetRootSignature(m_pRS);
	psoDesc.SetVertexShader("DebugRenderer.hlsl", "VSMain");
	psoDesc.SetPixelShader("DebugRenderer.hlsl", "PSMain");
	psoDesc.SetRenderTargetFormats(ResourceFormat::RGBA8_UNORM, GraphicsCommon::DepthStencilFormat, 1);
	psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
	psoDesc.SetDepthWrite(true);
	psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	psoDesc.SetName("Triangle DebugRenderer");
	m_pTrianglesPSO = pDevice->CreatePipeline(psoDesc);

	psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
	psoDesc.SetName("Lines DebugRenderer");
	m_pLinesPSO = pDevice->CreatePipeline(psoDesc);
}

void DebugRenderer::Shutdown()
{
	m_pTrianglesPSO.Reset();
	m_pLinesPSO.Reset();
	m_pRS.Reset();
}

void DebugRenderer::Render(RGGraph& graph, const SceneView* pView, RGTexture* pTarget, RGTexture* pDepth)
{
	if (m_NumLines == 0 && m_NumTriangles == 0)
		return;

	constexpr uint32 VertexStride = sizeof(DebugLine) / 2;
	uint32 numLines = m_NumLines;
	uint32 numTriangles = m_NumTriangles;

	graph.AddPass("Debug Rendering", RGPassFlag::Raster)
		.RenderTarget(pTarget)
		.DepthStencil(pDepth)
		.Bind([=](CommandContext& context, const RGResources& resources)
			{
				context.SetGraphicsRootSignature(m_pRS);

				context.BindRootCBV(1, Renderer::GetViewUniforms(pView, resources.Get(pTarget)));

				if (numLines != 0)
				{
					ScratchAllocation allocation = context.AllocateScratch(numLines * VertexStride);
					memcpy(allocation.pMappedMemory, m_Lines, numLines * VertexStride);
					context.BindRootSRV(0, allocation.GpuHandle);
					context.SetPipelineState(m_pLinesPSO);
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
					context.Draw(0, numLines);
				}
				if (numTriangles != 0)
				{
					ScratchAllocation allocation = context.AllocateScratch(numTriangles * VertexStride);
					memcpy(allocation.pMappedMemory, m_Triangles, numTriangles * VertexStride);
					context.BindRootSRV(0, allocation.GpuHandle);
					context.SetPipelineState(m_pTrianglesPSO);
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					context.Draw(0, numTriangles);
				}
			});

	m_NumLines = 0;
	m_NumTriangles = 0;
}

void DebugRenderer::AddLine(const Vector3& start, const Vector3& end, const IntColor& color)
{
	if (m_NumLines < MaxLines)
		m_Lines[m_NumLines++] = DebugLine(start, end, color);
	else
		validateOnce(false);
}

void DebugRenderer::AddRay(const Vector3& start, const Vector3& direction, const IntColor& color)
{
	AddLine(start, start + direction, color);
}

void DebugRenderer::AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const IntColor& color, bool solid)
{
	if (solid)
	{
		if (m_NumTriangles < MaxTriangles)
			m_Triangles[m_NumTriangles++] = DebugTriangle(a, b, c, color);
		else
			validateOnce(false);
	}
	else
	{
		AddLine(a, b, color);
		AddLine(b, c, color);
		AddLine(c, b, color);
	}
}

void DebugRenderer::AddPolygon(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const IntColor& color)
{
	AddTriangle(a, b, c, color);
	AddTriangle(c, d, a, color);
}

void DebugRenderer::AddBox(const Vector3& position, const Vector3& extents, const IntColor& color, bool solid /*= false*/)
{
	Vector3 min(position.x - extents.x, position.y - extents.y, position.z - extents.z);
	Vector3 max(position.x + extents.x, position.y + extents.y, position.z + extents.z);

	Vector3 v1(max.x, min.y, min.z);
	Vector3 v2(max.x, max.y, min.z);
	Vector3 v3(min.x, max.y, min.z);
	Vector3 v4(min.x, min.y, max.z);
	Vector3 v5(max.x, min.y, max.z);
	Vector3 v6(min.x, max.y, max.z);

	if (!solid)
	{
		AddLine(min, v1, color);
		AddLine(v1, v2, color);
		AddLine(v2, v3, color);
		AddLine(v3, min, color);
		AddLine(v4, v5, color);
		AddLine(v5, max, color);
		AddLine(max, v6, color);
		AddLine(v6, v4, color);
		AddLine(min, v4, color);
		AddLine(v1, v5, color);
		AddLine(v2, max, color);
		AddLine(v3, v6, color);
	}
	else
	{
		AddPolygon(v3, v2, v1, min, color);
		AddPolygon(v4, v5, max, v6, color);
		AddPolygon(min, v4, v6, v3, color);
		AddPolygon(v2, max, v5, v1, color);
		AddPolygon(v6, max, v2, v3, color);
		AddPolygon(min, v1, v5, v4, color);
	}
}

void DebugRenderer::AddBoundingBox(const BoundingBox& boundingBox, const IntColor& color, bool solid /*= false*/)
{
	AddBox(boundingBox.Center, boundingBox.Extents, color, solid);
}

void DebugRenderer::AddBoundingBox(const BoundingBox& boundingBox, const Matrix& transform, const IntColor& color, bool solid /*= false*/)
{
	const Vector3 min(boundingBox.Center.x - boundingBox.Extents.x, boundingBox.Center.y - boundingBox.Extents.y, boundingBox.Center.z - boundingBox.Extents.z);
	const Vector3 max(boundingBox.Center.x + boundingBox.Extents.x, boundingBox.Center.y + boundingBox.Extents.y, boundingBox.Center.z + boundingBox.Extents.z);

	Vector3 v0(Vector3::Transform(min, transform));
	Vector3 v1(Vector3::Transform(Vector3(max.x, min.y, min.z), transform));
	Vector3 v2(Vector3::Transform(Vector3(max.x, max.y, min.z), transform));
	Vector3 v3(Vector3::Transform(Vector3(min.x, max.y, min.z), transform));
	Vector3 v4(Vector3::Transform(Vector3(min.x, min.y, max.z), transform));
	Vector3 v5(Vector3::Transform(Vector3(max.x, min.y, max.z), transform));
	Vector3 v6(Vector3::Transform(Vector3(min.x, max.y, max.z), transform));
	Vector3 v7(Vector3::Transform(max, transform));

	if (!solid)
    {
        AddLine(v0, v1, color);
        AddLine(v1, v2, color);
        AddLine(v2, v3, color);
        AddLine(v3, v0, color);
        AddLine(v4, v5, color);
        AddLine(v5, v7, color);
        AddLine(v7, v6, color);
        AddLine(v6, v4, color);
        AddLine(v0, v4, color);
        AddLine(v1, v5, color);
        AddLine(v2, v7, color);
        AddLine(v3, v6, color);
    }
    else
    {
        AddPolygon(v0, v1, v2, v3, color);
        AddPolygon(v4, v5, v7, v6, color);
        AddPolygon(v0, v4, v6, v3, color);
        AddPolygon(v1, v5, v7, v2, color);
        AddPolygon(v3, v2, v7, v6, color);
        AddPolygon(v0, v1, v5, v4, color);
    }
}

void DebugRenderer::AddSphere(const Vector3& position, float radius, int slices, int stacks, const IntColor& color, bool solid)
{
	DebugSphere sphere(position, radius);

	const float jStep = Math::PI / slices;
	const float iStep = Math::PI / stacks;

	if (!solid)
	{
		for (float j = 0; j < Math::PI; j += jStep)
		{
			for (float i = 0; i < Math::PI * 2; i += iStep)
			{
				Vector3 p1 = sphere.GetPoint(i, j);
				Vector3 p2 = sphere.GetPoint(i + iStep, j);
				Vector3 p3 = sphere.GetPoint(i, j + jStep);
				Vector3 p4 = sphere.GetPoint(i + iStep, j + jStep);

				AddLine(p1, p2, color);
				AddLine(p3, p4, color);
				AddLine(p1, p3, color);
				AddLine(p2, p4, color);
			}
		}
	}
	else
	{
		for (float j = 0; j < Math::PI; j += jStep)
		{
			for (float i = 0; i < Math::PI * 2; i += iStep)
			{
				Vector3 p1 = sphere.GetPoint(i, j);
				Vector3 p2 = sphere.GetPoint(i + iStep, j);
				Vector3 p3 = sphere.GetPoint(i, j + jStep);
				Vector3 p4 = sphere.GetPoint(i + iStep, j + jStep);

				AddPolygon(p2, p1, p3, p4, color);
			}
		}
	}
}

void DebugRenderer::AddFrustrum(const BoundingFrustum& frustrum, const IntColor& color)
{
	Array<Vector3> corners(BoundingFrustum::CORNER_COUNT);
	frustrum.GetCorners(corners.data());

	AddLine(corners[0], corners[1], color);
	AddLine(corners[1], corners[2], color);
	AddLine(corners[2], corners[3], color);
	AddLine(corners[3], corners[0], color);
	AddLine(corners[4], corners[5], color);
	AddLine(corners[5], corners[6], color);
	AddLine(corners[6], corners[7], color);
	AddLine(corners[7], corners[4], color);
	AddLine(corners[0], corners[4], color);
	AddLine(corners[1], corners[5], color);
	AddLine(corners[2], corners[6], color);
	AddLine(corners[3], corners[7], color);
}

void DebugRenderer::AddAxisSystem(const Matrix& transform, float lineLength)
{
	Vector3 origin(Vector3::Transform(Vector3(), transform));
	Vector3 x(Vector3::Transform(Vector3(lineLength, 0, 0), transform));
	Vector3 y(Vector3::Transform(Vector3(0, lineLength, 0), transform));
	Vector3 z(Vector3::Transform(Vector3(0, 0, lineLength), transform));

	AddLine(origin, x, Colors::Red);
	AddLine(origin, y, Colors::Green);
	AddLine(origin, z, Colors::Blue);
}

void DebugRenderer::AddWireCylinder(const Vector3& position, const Quaternion& rotation, float height, float radius, int segments, const IntColor& color)
{
	Vector3 forward = Vector3::Transform(Vector3::UnitZ, rotation);
	Matrix world = Matrix::CreateFromQuaternion(rotation) * Matrix::CreateTranslation(position);
	float t = Math::PI * 2 / (segments + 1);

	for (int i = 0; i < segments + 1; ++i)
	{
		Vector3 a = Vector3::Transform(Vector3(radius * cos(t * i), radius * sin(t * i), 0), world);
		Vector3 b = Vector3::Transform(Vector3(radius * cos(t * (i + 1)), radius * sin(t * (i + 1)), 0), world);
		AddLine(a - forward * height, b - forward * height, color);
		AddLine(a + forward * height, b + forward * height, color);
		AddLine(a + forward * height, a - forward * height, color);
	}
}

void DebugRenderer::AddCone(const Vector3& position, const Quaternion& rotation, float height, float angle, int segments, const IntColor& color, bool solid)
{
	Matrix world = Matrix::CreateFromQuaternion(rotation) * Matrix::CreateTranslation(position);

	float radius = tanf(0.5f * angle * Math::DegreesToRadians) * height;
	float t = Math::PI * 2 / (segments + 1);
	for (int i = 0; i < segments + 1; ++i)
	{
		Vector3 a = Vector3::Transform(Vector3(radius * cos(t * i), radius * sin(t * i), height), world);
		Vector3 b = Vector3::Transform(Vector3(radius * cos(t * (i + 1)), radius * sin(t * (i + 1)), height), world);
		AddLine(a, b, color);
		AddLine(a, position, color);
	}
}

void DebugRenderer::AddBone(const Matrix& matrix, float length, const IntColor& color)
{
	float boneSize = 2;
	Vector3 start = Vector3::Transform(Vector3(0, 0, 0), matrix);
	Vector3 a = Vector3::Transform(Vector3(-boneSize, boneSize, boneSize), matrix);
	Vector3 b = Vector3::Transform(Vector3(boneSize, boneSize, boneSize), matrix);
	Vector3 c = Vector3::Transform(Vector3(boneSize, -boneSize, boneSize), matrix);
	Vector3 d = Vector3::Transform(Vector3(-boneSize, -boneSize, boneSize), matrix);
	Vector3 tip = Vector3::Transform(Vector3(0, 0, -boneSize * length), matrix);

	AddTriangle(start, d, c, color, false);
	AddTriangle(start, a, d, color, false);
	AddTriangle(start, b, a, color, false);
	AddTriangle(start, c, b, color, false);
	AddTriangle(d, tip, c, color, false);
	AddTriangle(a, tip, d, color, false);
	AddTriangle(b, tip, a, color, false);
	AddTriangle(c, tip, b, color, false);
}

void DebugRenderer::AddLight(const Transform& transform, const Light& light, const IntColor& color /*= Colors::Yellow*/)
{
	switch (light.Type)
	{
	case LightType::Directional:
		AddWireCylinder(transform.Position, transform.Rotation, 4.0f, 2.0f, 10, color);
		AddAxisSystem(Matrix::CreateFromQuaternion(transform.Rotation) * Matrix::CreateTranslation(transform.Position), 1.0f);
		break;
	case LightType::Point:
		AddSphere(transform.Position, light.Range, 8, 8, color, false);
		break;
	case LightType::Spot:
		AddCone(transform.Position, transform.Rotation, light.Range, light.UmbraAngleDegrees, 10, color);
		break;
	default:
		break;
	}
}
