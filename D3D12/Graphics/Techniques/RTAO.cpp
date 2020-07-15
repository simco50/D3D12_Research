#include "stdafx.h"
#include "RTAO.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/RaytracingCommon.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Mesh.h"
#include "Scene/Camera.h"

RTAO::RTAO(Graphics* pGraphics)
{
	if (pGraphics->SupportsRayTracing())
	{
		SetupResources(pGraphics);
		SetupPipelines(pGraphics);
	}
}

void RTAO::OnSwapchainCreated(int windowWidth, int windowHeight)
{
}

void RTAO::Execute(RGGraph& graph, Texture* pColor, Texture* pDepth, Camera& camera)
{
	static float g_AoPower = 3;
	static float g_AoRadius = 0.5f;
	static int32 g_AoSamples = 1;

	ImGui::Begin("Parameters");
	ImGui::Text("Ambient Occlusion");
	ImGui::SliderFloat("Power", &g_AoPower, 0, 10);
	ImGui::SliderFloat("Radius", &g_AoRadius, 0.1f, 2.0f);
	ImGui::SliderInt("Samples", &g_AoSamples, 1, 64);
	ImGui::End();

	RGPassBuilder rt = graph.AddPass("RTAO");
	rt.Bind([=](CommandContext& context, const RGPassResources& passResources)
		{
			context.InsertResourceBarrier(pDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			context.InsertResourceBarrier(pColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

			context.SetComputeRootSignature(m_pGlobalRS.get());
			context.SetPipelineState(m_pRtSO.Get());

			constexpr const int numRandomVectors = 64;
			struct Parameters
			{
				Matrix ViewInverse;
				Matrix ProjectionInverse;
				Vector4 RandomVectors[numRandomVectors];
				float Power;
				float Radius;
				int32 Samples;
			} parameters{};

			static bool written = false;
			static Vector4 randoms[numRandomVectors];
			if (!written)
			{
				srand(2);
				written = true;
				for (int i = 0; i < numRandomVectors; ++i)
				{
					randoms[i] = Vector4(Math::RandVector());
					randoms[i].z = Math::Lerp(0.1f, 0.8f, (float)abs(randoms[i].z));
					randoms[i].Normalize();
					randoms[i] *= Math::Lerp(0.1f, 1.0f, (float)pow(Math::RandomRange(0, 1), 2));
				}
			}
			memcpy(parameters.RandomVectors, randoms, sizeof(Vector4) * numRandomVectors);

			parameters.ViewInverse = camera.GetViewInverse();
			parameters.ProjectionInverse = camera.GetProjectionInverse();
			parameters.Power = g_AoPower;
			parameters.Radius = g_AoRadius;
			parameters.Samples = g_AoSamples;

			ShaderBindingTable bindingTable(m_pRtSO.Get());
			bindingTable.AddRayGenEntry("RayGen", {});
			bindingTable.AddMissEntry("Miss", {});
			bindingTable.AddHitGroupEntry("HitGroup", {});

			context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
			context.SetDynamicDescriptor(1, 0, pColor->GetUAV());
			context.SetDynamicDescriptor(2, 0, m_pTLAS->GetSRV());
			context.SetDynamicDescriptor(2, 1, pDepth->GetSRV());

			context.DispatchRays(bindingTable, pColor->GetWidth(), pColor->GetHeight());
		});
}

void RTAO::GenerateAccelerationStructure(Graphics* pGraphics, Mesh* pMesh, CommandContext& context)
{
	if (pGraphics->SupportsRayTracing() == false)
	{
		return;
	}
	ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();

	//Bottom Level Acceleration Structure
	{
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
		for (size_t i = 0; i < pMesh->GetMeshCount(); ++i)
		{
			const SubMesh* pSubMesh = pMesh->GetMesh((int)i);
			if (pMesh->GetMaterial(pSubMesh->GetMaterialId()).IsTransparent)
			{
				continue;
			}
			D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
			geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			geometryDesc.Triangles.IndexBuffer = pMesh->GetIndexBuffer()->GetGpuHandle() + pSubMesh->GetIndexByteOffset();
			geometryDesc.Triangles.IndexCount = pSubMesh->GetIndexCount();
			geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
			geometryDesc.Triangles.Transform3x4 = 0;
			geometryDesc.Triangles.VertexBuffer.StartAddress = pMesh->GetVertexBuffer()->GetGpuHandle() + pSubMesh->GetVertexByteOffset();
			geometryDesc.Triangles.VertexBuffer.StrideInBytes = pMesh->GetVertexBuffer()->GetDesc().ElementSize;
			geometryDesc.Triangles.VertexCount = pSubMesh->GetVertexCount();
			geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			geometries.push_back(geometryDesc);
		}

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
		prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		prebuildInfo.Flags = 
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
			| D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
		prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		prebuildInfo.NumDescs = (uint32)geometries.size();
		prebuildInfo.pGeometryDescs = geometries.data();

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
		pGraphics->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

		m_pBLASScratch = std::make_unique<Buffer>(pGraphics, "BLAS Scratch Buffer");
		m_pBLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess));

		m_pBLAS = std::make_unique<Buffer>(pGraphics, "BLAS");
		m_pBLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
		asDesc.Inputs = prebuildInfo;
		asDesc.DestAccelerationStructureData = m_pBLAS->GetGpuHandle();
		asDesc.ScratchAccelerationStructureData = m_pBLASScratch->GetGpuHandle();
		asDesc.SourceAccelerationStructureData = 0;

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
		context.InsertUavBarrier(m_pBLAS.get());
		context.FlushResourceBarriers();
	}
	//Top Level Acceleration Structure
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
		prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		prebuildInfo.Flags = 
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
			| D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
		prebuildInfo.NumDescs = 1;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
		pGraphics->GetRaytracingDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

		m_pTLASScratch = std::make_unique<Buffer>(pGraphics, "TLAS Scratch");
		m_pTLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::None));
		m_pTLAS = std::make_unique<Buffer>(pGraphics, "TLAS");
		m_pTLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)));
		m_pDescriptorsBuffer = std::make_unique<Buffer>(pGraphics, "Descriptors Buffer");
		m_pDescriptorsBuffer->Create(BufferDesc::CreateVertexBuffer((uint32)Math::AlignUp<uint64>(sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), 4, BufferFlag::Upload));

		D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDesc = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(m_pDescriptorsBuffer->Map());
		pInstanceDesc->AccelerationStructure = m_pBLAS->GetGpuHandle();
		pInstanceDesc->Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		pInstanceDesc->InstanceContributionToHitGroupIndex = 0;
		pInstanceDesc->InstanceID = 0;
		pInstanceDesc->InstanceMask = 0xFF;
		memcpy(pInstanceDesc->Transform, &Matrix::Identity, 12 * sizeof(float));
		m_pDescriptorsBuffer->Unmap();

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc{};
		asDesc.DestAccelerationStructureData = m_pTLAS->GetGpuHandle();
		asDesc.ScratchAccelerationStructureData = m_pTLASScratch->GetGpuHandle();
		asDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
		asDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		asDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		asDesc.Inputs.InstanceDescs = m_pDescriptorsBuffer->GetGpuHandle();
		asDesc.Inputs.NumDescs = 1;
		asDesc.SourceAccelerationStructureData = 0;

		pCmd->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);
		context.InsertUavBarrier(m_pTLAS.get());
	}
}

void RTAO::SetupResources(Graphics* pGraphics)
{
}

void RTAO::SetupPipelines(Graphics* pGraphics)
{
	//Raytracing Pipeline
	{
		m_pRayGenSignature = std::make_unique<RootSignature>();
		m_pRayGenSignature->Finalize("Ray Gen RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

		m_pHitSignature = std::make_unique<RootSignature>();
		m_pHitSignature->Finalize("Hit RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

		m_pMissSignature = std::make_unique<RootSignature>();
		m_pMissSignature->Finalize("Miss RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

		m_pGlobalRS = std::make_unique<RootSignature>();
		m_pGlobalRS->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pGlobalRS->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pGlobalRS->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_ALL);
		m_pGlobalRS->AddStaticSampler(0, CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP), D3D12_SHADER_VISIBILITY_ALL);
		m_pGlobalRS->Finalize("Dummy Global RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ShaderLibrary shaderLibrary("RTAO.hlsl");

		StateObjectDesc stateDesc;
		stateDesc.AddLibrary(shaderLibrary, { "RayGen", "ClosestHit", "Miss" });
		stateDesc.AddHitGroup("HitGroup", "ClosestHit");
		stateDesc.BindLocalRootSignature("RayGen", m_pRayGenSignature->GetRootSignature());
		stateDesc.BindLocalRootSignature("Miss", m_pMissSignature->GetRootSignature());
		stateDesc.BindLocalRootSignature("HitGroup", m_pHitSignature->GetRootSignature());
		stateDesc.SetRaytracingShaderConfig(sizeof(float), 2 * sizeof(float));
		stateDesc.SetRaytracingPipelineConfig(1);
		stateDesc.SetGlobalRootSignature(m_pGlobalRS->GetRootSignature());
		m_pRtSO = stateDesc.Finalize("RTAO SO", pGraphics->GetRaytracingDevice());
	}
}