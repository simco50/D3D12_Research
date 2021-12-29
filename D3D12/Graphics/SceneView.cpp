#include "stdafx.h"
#include "SceneView.h"
#include "Core/CommandContext.h"
#include "Scene/Camera.h"
#include "Core/Buffer.h"
#include "Mesh.h"
#include "Core/PipelineState.h"
#include "Core/Texture.h"

void DrawScene(CommandContext& context, const SceneView& scene, Batch::Blending blendModes)
{
	DrawScene(context, scene, scene.VisibilityMask, blendModes);
}

void BindViewParameters(uint32 rootIndex, CommandContext& context, const SceneView& scene)
{
	ShaderInterop::ViewUniforms parameters;
	const Camera& camera = *scene.pCamera;
	Texture* pMainTexture = scene.pRenderTarget;

	parameters.View = camera.GetView();
	parameters.ViewInverse = camera.GetViewInverse();
	parameters.Projection = camera.GetProjection();
	parameters.ProjectionInverse = camera.GetProjectionInverse();
	parameters.ViewProjection = camera.GetViewProjection();
	parameters.ViewProjectionInverse = camera.GetProjectionInverse() * camera.GetViewInverse();

	Matrix reprojectionMatrix = camera.GetViewProjection().Invert() * camera.GetPreviousViewProjection();
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

	parameters.PreviousViewProjection = camera.GetPreviousViewProjection();
	parameters.ReprojectionMatrix = premult * reprojectionMatrix * postmult;
	parameters.ViewPosition = Vector4(camera.GetPosition());

	DirectX::XMVECTOR nearPlane, farPlane, left, right, top, bottom;
	camera.GetFrustum().GetPlanes(&nearPlane, &farPlane, &right, &left, &top, &bottom);
	parameters.FrustumPlanes[0] = Vector4(nearPlane);
	parameters.FrustumPlanes[1] = Vector4(farPlane);
	parameters.FrustumPlanes[2] = Vector4(left);
	parameters.FrustumPlanes[3] = Vector4(right);
	parameters.FrustumPlanes[4] = Vector4(top);
	parameters.FrustumPlanes[5] = Vector4(bottom);

	parameters.ScreenDimensions = Vector2((float)pMainTexture->GetWidth(), (float)pMainTexture->GetHeight());
	parameters.ScreenDimensionsInv = Vector2(1.0f / pMainTexture->GetWidth(), 1.0f / pMainTexture->GetHeight());
	parameters.ViewJitter.x = camera.GetPreviousJitter().x - camera.GetJitter().x;
	parameters.ViewJitter.y = -(camera.GetPreviousJitter().y - camera.GetJitter().y);
	parameters.NearZ = camera.GetNear();
	parameters.FarZ = camera.GetFar();
	parameters.FoV = camera.GetFoV();

	parameters.FrameIndex = scene.FrameIndex;
	parameters.SsrSamples = 1;
	parameters.LightCount = scene.pLightBuffer->GetNumElements();

	memcpy(&parameters.LightViewProjections, &scene.ShadowData.LightViewProjections, ARRAYSIZE(parameters.LightViewProjections) * MAX_SHADOW_CASTERS);
	parameters.CascadeDepths = scene.ShadowData.CascadeDepths;
	parameters.NumCascades = scene.ShadowData.NumCascades;
	parameters.ShadowMapOffset = scene.ShadowData.ShadowMapOffset;

	parameters.TLASIndex = scene.SceneTLAS;
	parameters.MeshesIndex = scene.pMeshBuffer->GetSRVIndex();
	parameters.MaterialsIndex = scene.pMaterialBuffer->GetSRVIndex();
	parameters.MeshInstancesIndex = scene.pMeshInstanceBuffer->GetSRVIndex();
	parameters.LightsIndex = scene.pLightBuffer->GetSRVIndex();

	context.SetRootCBV(rootIndex, parameters);
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

	ShaderInterop::PerObjectData objectData;
	for (const Batch* b : meshes)
	{
		objectData.Index = b->Index;
		context.SetRootConstants(0, objectData);
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
