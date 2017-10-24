#pragma once

class Graphics
{
public:
	Graphics(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
	virtual void OnRender();
	virtual void OnDestroy();

private:
	static const UINT FrameCount = 2;

	struct Vertex
	{
		XMFLOAT3 position;
		XMFLOAT4 color;
	};

	// Pipeline objects.
	D3D12_VIEWPORT m_Viewport;
	D3D12_RECT m_ScissorRect;
	ComPtr<IDXGISwapChain3> m_pSwapChain;
	ComPtr<ID3D12Device> m_pDevice;
	ComPtr<ID3D12Resource> m_pRenderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_pCommandAllocator;
	ComPtr<ID3D12CommandQueue> m_pCommandQueue;
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3D12DescriptorHeap> m_pRtvHeap;
	ComPtr<ID3D12PipelineState> m_pPipelineState;
	ComPtr<ID3D12GraphicsCommandList> m_pCommandList;
	UINT m_RtvDescriptorSize;


	// App resources.
	ComPtr<ID3D12Resource> m_pVertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_pVertexBufferView;

	ComPtr<ID3D12Resource> m_pIndexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_pIndexBufferView;

	ComPtr<ID3D12DescriptorHeap> m_pConstBufferHeap;
	ComPtr<ID3D12Resource> m_pConstBuffer;

	// Synchronization objects.
	UINT m_FrameIndex;
	HANDLE m_FenceEvent;
	ComPtr<ID3D12Fence> m_pFence;
	UINT64 m_FenceValue;

	HWND m_Hwnd;

	unsigned int m_WindowWidth;
	unsigned int m_WindowHeight;

	void MakeWindow();
	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitForPreviousFrame();

	static LRESULT CALLBACK WndProcStatic(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};

