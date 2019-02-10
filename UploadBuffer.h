#pragma once

template<typename T>
class UploadBuffer
{
public:
	UploadBuffer(ID3D12Device* pDevice, unsigned int elementCount = 1, const bool isConstBuffer = false);
	~UploadBuffer();

	UploadBuffer(const UploadBuffer& rhs) = delete;
	UploadBuffer& operator=(const UploadBuffer& rhs) = delete;

	ID3D12Resource* GetResource() const { return m_pBuffer.Get(); }

	void CopyData(const unsigned int elementIndex, void* pData);

	static unsigned int ConstantBufferSize(const unsigned int size);

private:
	bool m_IsConstBuffer;
	unsigned int m_ElementCount;
	unsigned int m_Stride;

	ComPtr<ID3D12Resource> m_pUploadBuffer;
	UINT* m_pDataPtr = nullptr;
};

template<typename T>
UploadBuffer<T>::UploadBuffer(ID3D12Device* pDevice, unsigned int elementCount /*= 1*/, const bool isConstBuffer /*= false*/) :
	m_ElementCount(elementCount), m_IsConstBuffer(isConstBuffer)
{
	m_Stride = sizeof(T);
	if (isConstBuffer)
		m_Stride = ConstantBufferSize(m_Stride);

	pDevice->CreateCommittedResource(
		CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), 
		D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(m_Stride * elementCount),
		D3D12_RESOURCE_STATE_GENERIC_READ, 
		nullptr, 
		IID_PPV_ARGS(m_pUploadBuffer.GetAddressOf())
	);

	m_pUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_pDataPtr));
}

template<typename T>
UploadBuffer<T>::~UploadBuffer()
{
	if(m_pUploadBuffer != nullptr)
		m_pUploadBuffer->Unmap(0, nullptr);
}

template<typename T>
unsigned int UploadBuffer<T>::ConstantBufferSize(const unsigned int size)
{
	return (size + 256) & ~256;
}

template<typename T>
void UploadBuffer<T>::CopyData(const unsigned int elementIndex, void* pData)
{
	memcpy(&m_pDataPtr[elementIndex * m_Stride], pData, sizeof(T));
}