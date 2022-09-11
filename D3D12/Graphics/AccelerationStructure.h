#pragma once

class Buffer;
class CommandContext;
class ShaderResourceView;
struct SceneView;
struct SubMesh;

class AccelerationStructure
{
public:
	void Build(CommandContext& context, const SceneView& view);
	ShaderResourceView* GetSRV() const;

private:
	RefCountPtr<Buffer> m_pTLAS;
	RefCountPtr<Buffer> m_pScratch;
};
