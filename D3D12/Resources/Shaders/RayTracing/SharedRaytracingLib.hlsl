#include "Common.hlsli"
#include "RaytracingCommon.hlsli"

[shader("miss")]
void OcclusionMS(inout OcclusionPayload payload : SV_RayPayload)
{
    payload.HitT = -1.0f;
}

[shader("closesthit")]
void MaterialCHS(inout MaterialRayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
	payload.InstanceID = InstanceID();
	payload.PrimitiveID = PrimitiveIndex();
	payload.HitT = RayTCurrent();
	payload.Barycentrics = attrib.barycentrics;
	payload.FrontFace = HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE;
}

[shader("anyhit")]
void MaterialAHS(inout MaterialRayPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
	InstanceData instance = GetInstance(InstanceID());
	VertexAttribute vertex = GetVertexAttributes(instance, attrib.barycentrics, PrimitiveIndex());
	MaterialData material = GetMaterial(instance.MaterialIndex);
	MaterialProperties surface = EvaluateMaterial(material, vertex, 0);

	uint seed = SeedThread(DispatchRaysIndex().xy, DispatchRaysDimensions().xy, cView.FrameIndex);
	if(surface.Opacity < Random01(seed))
	{
		IgnoreHit();
	}
}

[shader("miss")]
void MaterialMS(inout MaterialRayPayload payload : SV_RayPayload)
{
	// Nothing to do here! :)
}
