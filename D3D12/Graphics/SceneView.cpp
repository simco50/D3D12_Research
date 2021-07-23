#include "stdafx.h"
#include "SceneView.h"
#include "Core/CommandContext.h"
#include "Scene/Camera.h"
#include "Core/GraphicsBuffer.h"
#include "Mesh.h"

void DrawScene(CommandContext& context, const SceneView& scene, Batch::Blending blendModes)
{
	DrawScene(context, scene, scene.VisibilityMask, blendModes);
}

void DrawScene(CommandContext& context, const SceneView& scene, const VisibilityMask& visibility, Batch::Blending blendModes)
{
	std::vector<const Batch*> meshes;
	for (const Batch& b : scene.Batches)
	{
		if (EnumHasAnyFlags(b.BlendMode, blendModes) && visibility.GetBit(b.Index))
		{
			meshes.push_back(&b);
		}
	}

	auto CompareSort = [&scene, blendModes](const Batch* a, const Batch* b)
	{
		float aDist = Vector3::DistanceSquared(a->pMesh->Bounds.Center, scene.pCamera->GetPosition());
		float bDist = Vector3::DistanceSquared(b->pMesh->Bounds.Center, scene.pCamera->GetPosition());
		return EnumHasAnyFlags(blendModes, Batch::Blending::AlphaBlend) ? bDist < aDist : aDist < bDist;
	};
	std::sort(meshes.begin(), meshes.end(), CompareSort);

	struct PerObjectData
	{
		uint32 Mesh;
		uint32 Material;
	} ObjectData;

	for (const Batch* b : meshes)
	{
		ObjectData.Material = b->Material;
		ObjectData.Mesh = b->Index;
		context.SetGraphicsRootConstants(0, ObjectData);
		context.SetIndexBuffer(b->pMesh->IndicesLocation);
		context.DrawIndexed(b->pMesh->IndicesLocation.Elements, 0, 0);
	}
}
