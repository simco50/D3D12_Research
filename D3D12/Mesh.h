#pragma once
class GraphicsBuffer;
class CommandContext;

class Mesh
{
public:
	bool Load(const char* pFilePath, ID3D12Device* pDevice, CommandContext* pContext);
	void Draw(CommandContext* pContext);

private:
	int m_IndexCount = 0;
	int m_VertexCount = 0;
	std::unique_ptr<GraphicsBuffer> m_pVertexBuffer;
	std::unique_ptr<GraphicsBuffer> m_pIndexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;
};