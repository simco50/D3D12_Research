#include "stdafx.h"
#include "RTAO.h"
#include "Graphics/Core/Shader.h"
#include "Graphics/Core/PipelineState.h"
#include "Graphics/Core/RootSignature.h"
#include "Graphics/Core/GraphicsBuffer.h"
#include "Graphics/Core/Graphics.h"
#include "Graphics/Core/CommandContext.h"
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Core/Texture.h"
#include "Graphics/Core/ResourceViews.h"
#include "Graphics/Mesh.h"
#include "Graphics/Light.h"
#include "Graphics/Profiler.h"
#include "Scene/Camera.h"
#include "RenderGraph/RenderGraph.h"
#include "Core/RaytracingCommon.h"

RTAO::RTAO(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
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

void RTAO::Execute(RGGraph& graph, const RtaoInputResources& resources)
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

	graph.AddPass("Raytracing", [&](RGPassBuilder& builder)
		{
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				{
					context.InsertResourceBarrier(resources.pDepthTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					context.SetComputeRootSignature(m_pGlobalRS.get());
					ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();
					pCmd->SetPipelineState1(m_pStateObject.Get());

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

					parameters.ViewInverse = resources.pCamera->GetViewInverse();
					parameters.ProjectionInverse = resources.pCamera->GetProjectionInverse();
					parameters.Power = g_AoPower;
					parameters.Radius = g_AoRadius;
					parameters.Samples = g_AoSamples;

					D3D12_DISPATCH_RAYS_DESC rayDesc{};
					ShaderBindingTable bindingTable(m_pStateObject.Get());
					bindingTable.AddRayGenEntry("RayGen", {});
					bindingTable.AddMissEntry("Miss", {});
					bindingTable.AddHitGroupEntry("HitGroup", {});
					bindingTable.Commit(context, rayDesc);

					rayDesc.Width = resources.pRenderTarget->GetWidth();
					rayDesc.Height = resources.pRenderTarget->GetHeight();
					rayDesc.Depth = 1;

					context.SetComputeDynamicConstantBufferView(0, &parameters, sizeof(Parameters));
					context.SetDynamicDescriptor(1, 0, resources.pRenderTarget->GetUAV());
					context.SetDynamicDescriptor(2, 0, m_pTLAS->GetSRV());
					context.SetDynamicDescriptor(2, 1, resources.pDepthTexture->GetSRV());

					context.PrepareDraw(DescriptorTableType::Compute);
					pCmd->DispatchRays(&rayDesc);
				}
			};
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
			SubMesh* pSubMesh = pMesh->GetMesh((int)i);
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
		context.InsertUavBarrier(m_pBLAS.get(), true);
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
		context.InsertUavBarrier(m_pTLAS.get(), true);
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
		D3D12_SAMPLER_DESC samplerDesc{};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		m_pGlobalRS->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_ALL);
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		m_pGlobalRS->Finalize("Dummy Global RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ShaderLibrary shaderLibrary("RTAO.hlsl");

		CD3DX12_STATE_OBJECT_DESC desc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

		//Shaders
		{
			CD3DX12_DXIL_LIBRARY_SUBOBJECT* pRayGenDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
			CD3DX12_SHADER_BYTECODE byteCode = CD3DX12_SHADER_BYTECODE(shaderLibrary.GetByteCode(), shaderLibrary.GetByteCodeSize());
			pRayGenDesc->SetDXILLibrary(&byteCode);
			pRayGenDesc->DefineExport(L"RayGen");
			pRayGenDesc->DefineExport(L"ClosestHit");
			pRayGenDesc->DefineExport(L"Miss");
		}

		//Hit groups
		{
			CD3DX12_HIT_GROUP_SUBOBJECT* pHitGroupDesc = desc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
			pHitGroupDesc->SetHitGroupExport(L"HitGroup");
			pHitGroupDesc->SetClosestHitShaderImport(L"ClosestHit");
		}

		//Root signatures and associations
		{
			CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pRayGenRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
			pRayGenRs->SetRootSignature(m_pRayGenSignature->GetRootSignature());
			CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pMissRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
			pMissRs->SetRootSignature(m_pMissSignature->GetRootSignature());
			CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pHitRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
			pHitRs->SetRootSignature(m_pHitSignature->GetRootSignature());

			CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pRayGenAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			pRayGenAssociation->AddExport(L"RayGen");
			pRayGenAssociation->SetSubobjectToAssociate(*pRayGenRs);

			CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pMissAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			pMissAssociation->AddExport(L"Miss");
			pMissAssociation->SetSubobjectToAssociate(*pMissRs);

			CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pHitAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			pHitAssociation->AddExport(L"HitGroup");
			pHitAssociation->SetSubobjectToAssociate(*pHitRs);
		}

		//Raytracing config
		{
			CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* pRtConfig = desc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
			pRtConfig->Config(sizeof(float), 2 * sizeof(float));

			CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* pRtPipelineConfig = desc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
			pRtPipelineConfig->Config(1);

			CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* pGlobalRs = desc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
			pGlobalRs->SetRootSignature(m_pGlobalRS->GetRootSignature());
		}
		D3D12_STATE_OBJECT_DESC stateObject = *desc;

		VERIFY_HR_EX(pGraphics->GetRaytracingDevice()->CreateStateObject(&stateObject, IID_PPV_ARGS(m_pStateObject.GetAddressOf())), pGraphics->GetDevice());
		VERIFY_HR_EX(m_pStateObject->QueryInterface(IID_PPV_ARGS(m_pStateObjectProperties.GetAddressOf())), pGraphics->GetDevice());
	}
}