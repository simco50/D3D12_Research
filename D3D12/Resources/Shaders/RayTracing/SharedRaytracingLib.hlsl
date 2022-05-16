#include "Common.hlsli"
#include "RaytracingCommon.hlsli"

[shader("miss")]
void OcclusionMS(inout OcclusionPayload payload : SV_RayPayload)
{
    payload.SetMiss();
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
	MeshInstance instance = GetMeshInstance(InstanceID());
	VertexAttribute vertex = GetVertexAttributes(instance, attrib.barycentrics, PrimitiveIndex());
	MaterialData material = GetMaterial(instance.Material);
	MaterialProperties surface = GetMaterialProperties(material, vertex.UV, 0);

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