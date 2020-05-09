#include "stdafx.h"
#include "DebugRenderer.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/CommandContext.h"
#include "Scene/Camera.h"
#include "Mesh.h"
#include "Light.h"
#include "RenderGraph/RenderGraph.h"

DebugRenderer& DebugRenderer::Instance()
{
	static DebugRenderer instance;
	return instance;
}

void DebugRenderer::Initialize(Graphics* pGraphics)
{
	m_pGraphics = pGraphics;
	D3D12_INPUT_ELEMENT_DESC inputElements[] = {
		D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32_UINT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	//Shaders
	Shader vertexShader("Resources/Shaders/DebugRenderer.hlsl", Shader::Type::Vertex, "VSMain", { });
	Shader pixelShader("Resources/Shaders/DebugRenderer.hlsl", Shader::Type::Pixel, "PSMain", { });

	//Rootsignature
	m_pRS = std::make_unique<RootSignature>();
	m_pRS->FinalizeFromShader("Diffuse", vertexShader, pGraphics->GetDevice());

	//Opaque
	m_pTrianglesPSO = std::make_unique<PipelineState>();
	m_pTrianglesPSO->SetInputLayout(inputElements, sizeof(inputElements) / sizeof(inputElements[0]));
	m_pTrianglesPSO->SetRootSignature(m_pRS->GetRootSignature());
	m_pTrianglesPSO->SetVertexShader(vertexShader.GetByteCode(), vertexShader.GetByteCodeSize());
	m_pTrianglesPSO->SetPixelShader(pixelShader.GetByteCode(), pixelShader.GetByteCodeSize());
	m_pTrianglesPSO->SetRenderTargetFormat(Graphics::RENDER_TARGET_FORMAT, Graphics::DEPTH_STENCIL_FORMAT, m_pGraphics->GetMultiSampleCount(), m_pGraphics->GetMultiSampleQualityLevel(m_pGraphics->GetMultiSampleCount()));
	m_pTrianglesPSO->SetDepthTest(D3D12_COMPARISON_FUNC_GREATER_EQUAL);
	m_pTrianglesPSO->SetDepthWrite(true);
	m_pTrianglesPSO->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_pTrianglesPSO->Finalize("Triangle DebugRenderer PSO", pGraphics->GetDevice());

	m_pLinesPSO = std::make_unique<PipelineState>(*m_pTrianglesPSO);
	m_pLinesPSO->SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
	m_pLinesPSO->Finalize("Lines DebugRenderer PSO", pGraphics->GetDevice());
}

void DebugRenderer::Render(RGGraph& graph)
{
	int totalPrimitives = m_LinePrimitives + m_TrianglePrimitives;

	if (totalPrimitives == 0 || m_pCamera == nullptr)
	{
		return;
	}

	constexpr uint32 VertexStride = sizeof(DebugLine) / 2;

	graph.AddPass("Debug Rendering", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return [=](CommandContext& context, const RGPassResources& resources)
			{
				context.InsertResourceBarrier(m_pGraphics->GetDepthStencil(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
				context.InsertResourceBarrier(m_pGraphics->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET);

				context.BeginRenderPass(RenderPassInfo(m_pGraphics->GetCurrentRenderTarget(), RenderPassAccess::Load_Store, m_pGraphics->GetDepthStencil(), RenderPassAccess::Load_Store));

				context.SetViewport(FloatRect(0, 0, (float)m_pGraphics->GetWindowWidth(), (float)m_pGraphics->GetWindowHeight()));
				context.SetGraphicsRootSignature(m_pRS.get());

				const Matrix& projectionMatrix = m_pCamera->GetViewProjection();
				context.SetDynamicConstantBufferView(0, &projectionMatrix, sizeof(Matrix));

				if (m_LinePrimitives != 0)
				{
					context.SetDynamicVertexBuffer(0, m_LinePrimitives, VertexStride, m_Lines.data());
					context.SetPipelineState(m_pLinesPSO.get());
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
					context.Draw(0, m_LinePrimitives);
				}
				if (m_TrianglePrimitives != 0)
				{
					context.SetDynamicVertexBuffer(0, m_TrianglePrimitives, VertexStride, m_Triangles.data());
					context.SetPipelineState(m_pTrianglesPSO.get());
					context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					context.Draw(0, m_TrianglePrimitives);
				}

				context.EndRenderPass();
			};
		});
}

void DebugRenderer::EndFrame()
{
	m_LinePrimitives = 0;
	m_Lines.clear();
	m_TrianglePrimitives = 0;
	m_Triangles.clear();
}

void DebugRenderer::AddLine(const Vector3& start, const Vector3& end, const Color& color)
{
	AddLine(start, end, color, color);
}

void DebugRenderer::AddLine(const Vector3& start, const Vector3& end, const Color& colorStart, const Color& colorEnd)
{
	m_Lines.push_back(DebugLine(start, end, Math::EncodeColor(colorStart), Math::EncodeColor(colorEnd)));
	m_LinePrimitives += 2;
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
		m_TrianglePrimitives += 3;
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

void DebugRenderer::AddBox(const Vector3& position, const Vector3& extents, const Color& color, const bool solid /*= false*/)
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

void DebugRenderer::AddBoundingBox(const BoundingBox& boundingBox, const Color& color, const bool solid /*= false*/)
{
	AddBox(boundingBox.Center, boundingBox.Extents, color, solid);
}

void DebugRenderer::AddBoundingBox(const BoundingBox& boundingBox, const Matrix& transform, const Color& color, const bool solid /*= false*/)
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

void DebugRenderer::AddSphere(const Vector3& position, const float radius, const int slices, const int stacks, const Color& color, const bool solid)
{
	DebugSphere sphere(position, radius);

	float jStep = Math::PI / slices;
	float iStep = Math::PI / stacks;

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

void DebugRenderer::AddAxisSystem(const Matrix& transform, const float lineLength)
{
	Matrix newMatrix = Matrix::CreateScale(Math::ScaleFromMatrix(transform));
	newMatrix.Invert(newMatrix);
	newMatrix *= Matrix::CreateScale(Vector3::Distance(m_pCamera->GetViewInverse().Translation(), transform.Translation()) / 5.0f);
	newMatrix *= transform;
	Vector3 origin(Vector3::Transform(Vector3(), transform));
	Vector3 x(Vector3::Transform(Vector3(lineLength, 0, 0), newMatrix));
	Vector3 y(Vector3::Transform(Vector3(0, lineLength, 0), newMatrix));
	Vector3 z(Vector3::Transform(Vector3(0, 0, lineLength), newMatrix));

	AddLine(origin, x, Color(1, 0, 0, 1));
	AddLine(origin, y, Color(0, 1, 0, 1));
	AddLine(origin, z, Color(0, 0, 1, 1));
}

void DebugRenderer::AddWireCylinder(const Vector3& position, const Vector3& direction, const float height, const float radius, const int segments, const Color& color)
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

void DebugRenderer::AddWireCone(const Vector3& position, const Vector3& direction, const float height, const float angle, const int segments, const Color& color)
{
	Vector3 d;
	direction.Normalize(d);

	float radius = tan(Math::ToRadians * angle) * height;
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

void DebugRenderer::AddBone(const Matrix& matrix, const float length, const Color& color)
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
	switch (light.LightType)
	{
	case Light::Type::Directional:
		AddWireCylinder(light.Position, light.Direction, 30.0f, 5.0f, 10, Color(1.0f, 1.0f, 0.0f, 1.0f));
		AddAxisSystem(Matrix::CreateWorld(light.Position, -light.Direction, Vector3::Up), 1.0f);
		break;
	case Light::Type::Point:
		AddSphere(light.Position, light.Range, 8, 8, Color(1.0f, 1.0f, 0.0f, 1.0f), false);
		break;
	case Light::Type::Spot:
		AddWireCone(light.Position, light.Direction, light.Range, Math::ToDegrees * acos(light.SpotlightAngles.y), 10, Color(1.0f, 1.0f, 0.0f, 1.0f));
		break;
	default:
		break;
	}
}