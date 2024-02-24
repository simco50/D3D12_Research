#pragma once

#include "Graphics/RHI/RHI.h"
#include "RHI/Fence.h"

struct SceneView;
struct SubMesh;

class AccelerationStructure
{
public:
	void Build(CommandContext& context, const SceneView& view);
	ShaderResourceView* GetSRV() const;

private:
	void ProcessCompaction(CommandContext& context);

	Ref<Buffer> m_pTLAS;
	Ref<Buffer> m_pScratch;
	Ref<Buffer> m_pBLASInstancesTargetBuffer;
	Ref<Buffer> m_pBLASInstancesSourceBuffer;

	// Compaction
	Ref<Buffer> m_pPostBuildInfoBuffer;
	Ref<Buffer> m_pPostBuildInfoReadbackBuffer;
	SyncPoint m_PostBuildInfoFence;
	std::vector<Ref<Buffer>*> m_QueuedRequests;
	std::vector<Ref<Buffer>*> m_ActiveRequests;
};
