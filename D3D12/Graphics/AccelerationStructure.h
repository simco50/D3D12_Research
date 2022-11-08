#pragma once
#include "RHI/Fence.h"
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
	void ProcessCompaction(CommandContext& context);

	RefCountPtr<Buffer> m_pTLAS;
	RefCountPtr<Buffer> m_pScratch;
	RefCountPtr<Buffer> m_pBLASInstancesTargetBuffer;
	RefCountPtr<Buffer> m_pBLASInstancesSourceBuffer;

	// Compaction
	RefCountPtr<Buffer> m_pPostBuildInfoBuffer;
	RefCountPtr<Buffer> m_pPostBuildInfoReadbackBuffer;
	SyncPoint m_PostBuildInfoFence;
	std::vector<RefCountPtr<Buffer>*> m_QueuedRequests;
	std::vector<RefCountPtr<Buffer>*> m_ActiveRequests;
};
