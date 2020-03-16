#include "Common.hlsl"

struct STriVertex 
{
	float3 vertex;
	float4 color;
};

//StructuredBuffer<STriVertex> BTriVertex : register(t0);

[shader("closesthit")] void ClosestHit(inout HitInfo payload, Attributes attrib) 
{
	float3 worldRayOrigin = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

	float3 barycentrics = float3(1.f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);
	uint vertId = 3 * PrimitiveIndex();
	float3 hitColor = float3(1,1,1) * RayTCurrent() / 500.0f;
	payload.colorAndDistance = float4(hitColor, RayTCurrent());
}
