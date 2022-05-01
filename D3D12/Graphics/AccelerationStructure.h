#pragma once

class Buffer;
class CommandContext;
class ShaderResourceView;
struct SubMesh;

class AccelerationStructure
{
public:
	void AddInstance(uint32 ID, SubMesh* pMesh, const Matrix& transform);
	void Build(CommandContext& context);
	ShaderResourceView* GetSRV() const;
	void Reset();

private:
	struct Instance
	{
		uint32 ID;
		SubMesh* pMesh;
		Matrix Transform;
	};

	std::vector<Instance> m_Instances;
	RefCountPtr<Buffer> m_pTLAS;
	RefCountPtr<Buffer> m_pScratch;
};
