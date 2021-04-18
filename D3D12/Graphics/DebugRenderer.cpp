#include "stdafx.h"
#include "DebugRenderer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/CommandContext.h"
#include "Light.h"
#include "RenderGraph/RenderGraph.h"

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

void DebugRenderer::Initialize(Graphics* pGraphics)
{
	CD3DX12_INPUT_ELEMENT_DESC inputElements[] = {
		CD3DX12_INPUT_ELEMENT_DESC("POSITION", DXGI_FORMAT_R32G32B32_FLOAT),
		CD3DX12_INPUT_ELEMENT_DESC("COLOR", DXGI_FORMAT_R32_UINT),
	};

	//Shaders
	Shader* pVertexShader = pGraphics->GetShaderManager()->GetShader("DebugRenderer.hlsl", ShaderType::Vertex, "VSMain");
	Shader* pPixelShader = pGraphics->GetShaderManager()->GetShader("DebugRenderer.hlsl", ShaderType::Pixel, "PSMain");

	//Rootsignature
	m_pRS = std::make_unique<RootSignature>(pGraphics);
	m_pRS->FinalizeFromShader("Diffuse", pVertexShader);

	//Opaque
	PipelineStateInitializer psoDesc;
	psoDesc.SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
	psoDesc.SetRootSignature(m_pRS->GetRootSignature());
	psoDesc.SetVertexShader(pVertexShader);
	psoDesc.SetPixelShader(pPixelShader);
	psoDesc.SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, pGraphics->GetMultiSampleCount());
	psoDesc.SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
	psoDesc.SetDepthWrite(true);
	psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	psoDesc.SetName("Triangle DebugRenderer");
	m_pTrianglesPSO = pGraphics->CreatePipeline(psoDesc);

	psoDesc.SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
	psoDesc.SetName("Lines DebugRenderer");
	m_pLinesPSO = pGraphics->CreatePipeline(psoDesc);
}

void DebugRenderer::Render(RGGraph& graph, const Matrix& viewProjection, Texture* pTarget, Texture* pDepth)
{
	int linePrimitives = (int)m_Lines.size() * 2;
	int trianglePrimitives = (int)m_Triangles.size() * 3;
	int totalPrimitives = linePrimitives + trianglePrimitives;
	if (totalPrimitives == 0)
	{
		return;
	}

	constexpr uint32 VertexStride = sizeof(DebugLine) / 2;

	RGPassBuilder pass = graph.AddPass("Debug Rendering");
	pass.Bind([=](CommandContext& context, const RGPassResources& resources)
		{
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			context.InsertResourceBarrier(pTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);

			context.BeginRenderPass(RenderPassInfo(pTarget, RenderPassAccess::Load_Store, pDepth, RenderPassAccess::Load_Store, false));
			context.SetGraphicsRootSignature(m_pRS.get());

			context.SetGraphicsDynamicConstantBufferView(0, viewProjection);

			if (linePrimitives != 0)
			{
				context.SetDynamicVertexBuffer(0, linePrimitives, VertexStride, m_Lines.data());
				context.SetPipelineState(m_pLinesPSO);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
				context.Draw(0, linePrimitives);
			}
			if (trianglePrimitives != 0)
			{
				context.SetDynamicVertexBuffer(0, trianglePrimitives, VertexStride, m_Triangles.data());
				context.SetPipelineState(m_pTrianglesPSO);
				context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context.Draw(0, trianglePrimitives);
			}
			context.EndRenderPass();
		});
	m_Lines.clear();
	m_Triangles.clear();
}

void DebugRenderer::AddLine(const Vector3& start, const Vector3& end, const Color& color)
{
	AddLine(start, end, color, color);
}

void DebugRenderer::AddLine(const Vector3& start, const Vector3& end, const Color& colorStart, const Color& colorEnd)
{
	m_Lines.push_back(DebugLine(start, end, Math::EncodeColor(colorStart), Math::EncodeColor(colorEnd)));
}

void DebugRenderer::AddRay(const Vector3& start, const Vector3& direction, const Color& color)
{
	AddLine(start, start + direction, color);
}

void DebugRenderer::AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& color, bool solid)
{
	AddTriangle(a, b, c, color, color, color, solid);
}

void DebugRenderer::AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& colorA, const Color& colorB, const Color& colorC, bool solid)
{
	if (solid)
	{
		m_Triangles.push_back(DebugTriangle(a, b, c, Math::EncodeColor(colorA), Math::EncodeColor(colorB), Math::EncodeColor(colorC)));
	}
	else
	{
		AddLine(a, b, colorA);
		AddLine(b, c, colorB);
		AddLine(c, b, colorC);
	}
}

void DebugRenderer::AddPolygon(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Color& color)
{
	AddTriangle(a, b, c, color);
	AddTriangle(c, d, a, color);
}

void DebugRenderer::AddBox(const Vector3& position, const Vector3& extents, const Color& color, bool solid /*= false*/)
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
		AddPolygon(min, v1, v2, v3, color);
		AddPolygon(v4, v5, max, v6, color);
		AddPolygon(min, v4, v6, v3, color);
		AddPolygon(v1, v5, max, v2, color);
		AddPolygon(v3, v2, max, v6, color);
		AddPolygon(min, v1, v5, v4, color);
	}
}

void DebugRenderer::AddBoundingBox(const BoundingBox& boundingBox, const Color& color, bool solid /*= false*/)
{
	AddBox(boundingBox.Center, boundingBox.Extents, color, solid);
}

void DebugRenderer::AddBoundingBox(const BoundingBox& boundingBox, const Matrix& transform, const Color& color, bool solid /*= false*/)
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

void DebugRenderer::AddSphere(const Vector3& position, float radius,  int slices, int stacks, const Color& color, bool solid)
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

void DebugRenderer::AddFrustrum(const BoundingFrustum& frustrum, const Color& color)
{
	std::vector<Vector3> corners(BoundingFrustum::CORNER_COUNT);
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
	Matrix newMatrix = Matrix::CreateScale(Math::ScaleFromMatrix(transform));
	newMatrix.Invert(newMatrix);
	newMatrix *= transform;
	Vector3 origin(Vector3::Transform(Vector3(), transform));
	Vector3 x(Vector3::Transform(Vector3(lineLength, 0, 0), newMatrix));
	Vector3 y(Vector3::Transform(Vector3(0, lineLength, 0), newMatrix));
	Vector3 z(Vector3::Transform(Vector3(0, 0, lineLength), newMatrix));

	AddLine(origin, x, Color(1, 0, 0, 1));
	AddLine(origin, y, Color(0, 1, 0, 1));
	AddLine(origin, z, Color(0, 0, 1, 1));
}

void DebugRenderer::AddWireCylinder(const Vector3& position, const Vector3& direction, float height, float radius, int segments, const Color& color)
{
	Vector3 d;
	direction.Normalize(d);

	DebugSphere sphere(position, radius);
	float t = Math::PI * 2 / (segments + 1);

	Matrix world = Matrix::CreateFromQuaternion(Math::LookRotation(d)) * Matrix::CreateTranslation(position - d * (height / 2));
	for (int i = 0; i < segments + 1; ++i)
	{
		Vector3 a = Vector3::Transform(sphere.GetLocalPoint(Math::PIDIV2, i * t), world);
		Vector3 b = Vector3::Transform(sphere.GetLocalPoint(Math::PIDIV2, (i + 1) * t), world);
		AddLine(a, b, color, color);
		AddLine(a + d * height, b + d * height, color, color);
		AddLine(a, a + d * height, color, color);
	}
}

void DebugRenderer::AddWireCone(const Vector3& position, const Vector3& direction, float height, float angle, int segments, const Color& color)
{
	Vector3 d;
	direction.Normalize(d);

	float radius = tanf(0.5f * angle * Math::ToRadians) * height;
	DebugSphere sphere(position, radius);
	float t = Math::PI * 2 / (segments + 1);

	Matrix world = Matrix::CreateFromQuaternion(Math::LookRotation(d)) * Matrix::CreateTranslation(position);
	for (int i = 0; i < segments + 1; ++i)
	{
		Vector3 a = Vector3::Transform(sphere.GetLocalPoint(Math::PIDIV2, i * t), world) + direction * height;
		Vector3 b = Vector3::Transform(sphere.GetLocalPoint(Math::PIDIV2, (i + 1) * t), world) + direction * height;
		AddLine(a, b, color, color);
		AddLine(a, position, color, color);
	}
}

void DebugRenderer::AddBone(const Matrix& matrix, float length, const Color& color)
{
	float boneSize = 2;
	Vector3 start = Vector3::Transform(Vector3(0, 0, 0), matrix);
	Vector3 a = Vector3::Transform(Vector3(-boneSize, boneSize, boneSize), matrix);
	Vector3 b = Vector3::Transform(Vector3(boneSize, boneSize, boneSize), matrix);
	Vector3 c = Vector3::Transform(Vector3(boneSize, -boneSize, boneSize), matrix);
	Vector3 d = Vector3::Transform(Vector3(-boneSize, -boneSize, boneSize), matrix);
	Vector3 tip = Vector3::Transform(Vector3(0, 0, -boneSize * length), matrix);

	AddTriangle(start, d, c, color, color, color, false);
	AddTriangle(start, a, d, color, color, color, false);
	AddTriangle(start, b, a, color, color, color, false);
	AddTriangle(start, c, b, color, color, color, false);
	AddTriangle(d, tip, c, color, color, color, false);
	AddTriangle(a, tip, d, color, color, color, false);
	AddTriangle(b, tip, a, color, color, color, false);
	AddTriangle(c, tip, b, color, color, color, false);
}

void DebugRenderer::AddLight(const Light& light)
{
	switch (light.Type)
	{
	case LightType::Directional:
		AddWireCylinder(light.Position, light.Direction, 30.0f, 5.0f, 10, Color(1.0f, 1.0f, 0.0f, 1.0f));
		AddAxisSystem(Matrix::CreateWorld(light.Position, -light.Direction, Vector3::Up), 1.0f);
		break;
	case LightType::Point:
		AddSphere(light.Position, light.Range, 8, 8, Color(1.0f, 1.0f, 0.0f, 1.0f), false);
		break;
	case LightType::Spot:
		AddWireCone(light.Position, light.Direction, light.Range, light.UmbraAngle, 10, Color(1.0f, 1.0f, 0.0f, 1.0f));
		break;
	default:
		break;
	}
}
