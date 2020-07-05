#pragma once

class Graphics;
class RootSignature;
class PipelineState;
class RGGraph;
class Texture;
struct Light;

struct DebugLine
{
	DebugLine(const Vector3& start, const Vector3& end, const uint32& colorStart, const uint32& colorEnd)
		: Start(start), ColorStart(colorStart), End(end), ColorEnd(colorEnd)
	{}

	Vector3 Start;
	uint32 ColorStart;
	Vector3 End;
	uint32 ColorEnd;
};

struct DebugTriangle
{
	DebugTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const uint32& colorA, const uint32& colorB, const uint32& colorC)
		: A(a), ColorA(colorA), B(b), ColorB(colorB), C(c), ColorC(colorC)
	{}

	Vector3 A;
	uint32 ColorA;
	Vector3 B;
	uint32 ColorB;
	Vector3 C;
	uint32 ColorC;
};

class DebugRenderer
{
public:
	static DebugRenderer* Get();
	
	void Initialize(Graphics* pGraphics);
	void Render(RGGraph& graph, const Matrix& viewProjection, Texture* pTarget, Texture* pDepth);
	void EndFrame();

	void AddLine(const Vector3& start, const Vector3& end, const Color& color);
	void AddLine(const Vector3& start, const Vector3& end, const Color& colorStart, const Color& colorEnd);
	void AddRay(const Vector3& start, const Vector3& direction, const Color& color);
	void AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& color, bool solid = true);
	void AddTriangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& colorA, const Color& colorB, const Color& colorC, bool solid = true);
	void AddPolygon(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Color& color);
	void AddBox(const Vector3& position, const Vector3& extents, const Color& color, bool solid = false);
	void AddBoundingBox(const BoundingBox& boundingBox, const Color& color, bool solid = false);
	void AddBoundingBox(const BoundingBox& boundingBox, const Matrix& transform, const Color& color, bool solid = false);
	void AddSphere(const Vector3& position, float radius, int slices, int stacks, const Color& color, bool solid = false);
	void AddFrustrum(const BoundingFrustum& frustum, const Color& color);
	void AddAxisSystem(const Matrix& transform, float lineLength = 1.0f);
	void AddWireCylinder(const Vector3& position, const Vector3& direction, float height, float radius, int segments, const Color& color);
	void AddWireCone(const Vector3& position, const Vector3& direction, float height, float angle, int segments, const Color& color);
	void AddBone(const Matrix& matrix, float length, const Color& color);
	void AddLight(const Light& light);

	std::vector<DebugLine> m_Lines;
	std::vector<DebugTriangle> m_Triangles;

	std::unique_ptr<PipelineState> m_pTrianglesPSO;
	std::unique_ptr<PipelineState> m_pLinesPSO;
	std::unique_ptr<RootSignature> m_pRS;
private:
	DebugRenderer() = default;
};