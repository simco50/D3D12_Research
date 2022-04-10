#include "stdafx.h"
#include "SceneView.h"
#include "RHI/CommandContext.h"
#include "RHI/Buffer.h"
#include "RHI/PipelineState.h"
#include "RHI/Texture.h"
#include "Mesh.h"
#include "Core/ConsoleVariables.h"

namespace Tweakables
{
	extern ConsoleVariable<int> g_SsrSamples;
}

void DrawScene(CommandContext& context, const SceneView& scene, Batch::Blending blendModes)
{
	DrawScene(context, scene, scene.VisibilityMask, blendModes);
}

ShaderInterop::ViewUniforms GetViewUniforms(const SceneView& sceneView, Texture* pTarget)
{
	ShaderInterop::ViewUniforms parameters;
	const ViewTransform& view = sceneView.View;

	parameters.View = view.View;
	parameters.ViewInverse = view.ViewInverse;
	parameters.Projection = view.Projection;
	parameters.ProjectionInverse = view.ProjectionInverse;
	parameters.ViewProjection = view.ViewProjection;
	parameters.ViewProjectionInverse = view.ProjectionInverse * view.ViewInverse;

	Matrix reprojectionMatrix = parameters.ViewProjectionInverse * view.PreviousViewProjection;
	// Transform from uv to clip space: texcoord * 2 - 1
	Matrix premult = {
		2.0f, 0, 0, 0,
		0, -2.0f, 0, 0,
		0, 0, 1, 0,
		-1, 1, 0, 1
	};
	// Transform from clip to uv space: texcoord * 0.5 + 0.5
	Matrix postmult = {
		0.5f, 0, 0, 0,
		0, -0.5f, 0, 0,
		0, 0, 1, 0,
		0.5f, 0.5f, 0, 1
	};

	parameters.PreviousViewProjection = view.PreviousViewProjection;
	parameters.ReprojectionMatrix = premult * reprojectionMatrix * postmult;
	parameters.ViewPosition = view.Position;

	DirectX::XMVECTOR nearPlane, farPlane, left, right, top, bottom;
	view.Frustum.GetPlanes(&nearPlane, &farPlane, &right, &left, &top, &bottom);
	parameters.FrustumPlanes[0] = Vector4(nearPlane);
	parameters.FrustumPlanes[1] = Vector4(farPlane);
	parameters.FrustumPlanes[2] = Vector4(left);
	parameters.FrustumPlanes[3] = Vector4(right);
	parameters.FrustumPlanes[4] = Vector4(top);
	parameters.FrustumPlanes[5] = Vector4(bottom);

	if (pTarget)
	{
		parameters.ScreenDimensions = Vector2((float)pTarget->GetWidth(), (float)pTarget->GetHeight());
		parameters.ScreenDimensionsInv = Vector2(1.0f / pTarget->GetWidth(), 1.0f / pTarget->GetHeight());
	}
	parameters.ViewportDimensions = Vector2(view.Viewport.GetWidth(), view.Viewport.GetHeight());
	parameters.ViewportDimensionsInv = Vector2(1.0f / view.Viewport.GetWidth(), 1.0f / view.Viewport.GetHeight());

	parameters.ViewJitter.x = view.PreviousJitter.x - view.Jitter.x;
	parameters.ViewJitter.y = -(view.PreviousJitter.y - view.Jitter.y);
	parameters.NearZ = view.NearPlane;
	parameters.FarZ = view.FarPlane;
	parameters.FoV = view.FoV;


	parameters.FrameIndex = sceneView.FrameIndex;
	parameters.SsrSamples = Tweakables::g_SsrSamples.Get();
	parameters.LightCount = sceneView.pLightBuffer->GetNumElements();

	check(sceneView.ShadowViews.size() <= MAX_SHADOW_CASTERS);
	for (uint32 i = 0; i < sceneView.ShadowViews.size(); ++i)
	{
		parameters.LightViewProjections[i] = sceneView.ShadowViews[i].ViewProjection;
	}
	parameters.CascadeDepths = sceneView.ShadowCascadeDepths;
	parameters.NumCascades = sceneView.NumShadowCascades;
	parameters.ShadowMapOffset = sceneView.ShadowMapOffset;

	parameters.TLASIndex = sceneView.pSceneTLAS ? sceneView.pSceneTLAS->GetSRVIndex() : DescriptorHandle::InvalidHeapIndex;
	parameters.MeshesIndex = sceneView.pMeshBuffer->GetSRVIndex();
	parameters.MaterialsIndex = sceneView.pMaterialBuffer->GetSRVIndex();
	parameters.MeshInstancesIndex = sceneView.pMeshInstanceBuffer->GetSRVIndex();
	parameters.TransformsIndex = sceneView.pTransformsBuffer->GetSRVIndex();
	parameters.LightsIndex = sceneView.pLightBuffer->GetSRVIndex();
	parameters.SkyIndex = sceneView.pSky->GetSRVIndex();
	parameters.DDGIVolumesIndex = sceneView.pDDGIVolumesBuffer->GetSRVIndex();
	parameters.NumDDGIVolumes = sceneView.NumDDGIVolumes;
	return parameters;
}

void DrawScene(CommandContext& context, const SceneView& scene, const VisibilityMask& visibility, Batch::Blending blendModes)
{
	std::vector<const Batch*> meshes;
	for (const Batch& b : scene.Batches)
	{
		if (EnumHasAnyFlags(b.BlendMode, blendModes) && visibility.GetBit(b.InstanceData.World))
		{
			meshes.push_back(&b);
		}
	}

	auto CompareSort = [&scene, blendModes](const Batch* a, const Batch* b)
	{
		float aDist = Vector3::DistanceSquared(a->Bounds.Center, scene.View.Position);
		float bDist = Vector3::DistanceSquared(b->Bounds.Center, scene.View.Position);
		return EnumHasAnyFlags(blendModes, Batch::Blending::AlphaBlend) ? bDist < aDist : aDist < bDist;
	};
	std::sort(meshes.begin(), meshes.end(), CompareSort);

	for (const Batch* b : meshes)
	{
		context.SetRootConstants(0, b->InstanceData);
		if(context.GetCurrentPSO()->GetType() == PipelineStateType::Mesh)
		{
			context.DispatchMesh(ComputeUtils::GetNumThreadGroups(b->pMesh->NumMeshlets, 32));
		}
		else
		{
			context.SetIndexBuffer(b->pMesh->IndicesLocation);
			context.DrawIndexed(b->pMesh->IndicesLocation.Elements, 0, 0);
		}
	}
}
