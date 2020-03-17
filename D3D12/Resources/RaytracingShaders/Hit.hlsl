#include "Common.hlsl"

[shader("closesthit")] 
void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
	payload.hit = 1;
}
