#include "Common.hlsl"

[shader("miss")] 
void Miss(inout HitInfo payload : SV_RayPayload) 
{
	payload.hit = 0;
}