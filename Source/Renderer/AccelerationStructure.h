#pragma once

#include "RHI/RHI.h"
#include "RHI/Fence.h"

struct Batch;
struct SubMesh;
struct RenderView;

class AccelerationStructure
{
public:
	AccelerationStructure();
	~AccelerationStructure();

	void Init(GraphicsDevice* pDevice);
	void Build(CommandContext& context, const RenderView& view, Span<const Batch> batches);
	ShaderResourceView* GetSRV() const;

private:
	void ProcessCompaction(CommandContext& context);

	Ref<PipelineState> m_pUpdateTLASPSO;

	Ref<Buffer> m_pTLAS;
	Ref<Buffer> m_pScratch;
	Ref<Buffer> m_pBLASInstancesTargetBuffer;
	Ref<Buffer> m_pBLASInstancesSourceBuffer;

	// Compaction
	Ref<Buffer> m_pPostBuildInfoBuffer;
	Ref<Buffer> m_pPostBuildInfoReadbackBuffer;
	SyncPoint m_PostBuildInfoFence;
	Array<Ref<Buffer>*> m_QueuedRequests;
	Array<Ref<Buffer>*> m_ActiveRequests;
};
