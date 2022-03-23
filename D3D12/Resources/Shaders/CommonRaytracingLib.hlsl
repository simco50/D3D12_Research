#include "Common.hlsli"
#include "RaytracingCommon.hlsli"

[shader("miss")]
void OcclusionMiss(inout OcclusionPayload payload : SV_RayPayload)
{
    payload.SetMiss();
}