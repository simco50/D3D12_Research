#include "stdafx.h"
#include "RayTracing.h"
#include "Graphics/Shader.h"
#include "Graphics/PipelineState.h"
#include "Graphics/RootSignature.h"
#include "Graphics/GraphicsBuffer.h"
#include "Graphics/Graphics.h"
#include "Graphics/CommandContext.h"
#include "Graphics/CommandQueue.h"
#include "Graphics/Texture.h"
#include "Graphics/Mesh.h"
#include "Graphics/Light.h"
#include "Graphics/Profiler.h"
#include "Scene/Camera.h"
#include "ResourceViews.h"
#include "RenderGraph/RenderGraph.h"
#include "External/nv_helpers_dx12/ShaderBindingTableGenerator.h"

Raytracing::Raytracing(Graphics* pGraphics)
	: m_pGraphics(pGraphics)
{
	if (pGraphics->SupportsRayTracing())
	{
		SetupResources(pGraphics);
		SetupPipelines(pGraphics);
	}
}

void Raytracing::OnSwapchainCreated(int windowWidth, int windowHeight)
{
	if (m_pOutputTexture)
	{
		m_pOutputTexture->Create(TextureDesc::Create2D(windowWidth, windowHeight, DXGI_FORMAT_R8G8B8A8_UNORM, TextureFlag::UnorderedAccess));
		m_pOutputTexture->CreateUAV(&pOutputRawUAV, TextureUAVDesc(0));
	}
}

void Raytracing::Execute(RGGraph& graph, const RaytracingInputResources& resources)
{
	if (m_pOutputTexture == nullptr)
	{
		return;
	}

	graph.AddPass("Raytracing", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				ID3D12GraphicsCommandList* pC = context.GetCommandList();
				ComPtr<ID3D12GraphicsCommandList4> pCmd;
				pC->QueryInterface(IID_PPV_ARGS(pCmd.GetAddressOf()));
				if (!pCmd)
				{
					return;
				}

				nv_helpers_dx12::ShaderBindingTableGenerator sbtGenerator;
				DynamicAllocation sbtAllocation;
				//Shader Bindings
				{
					context.InsertResourceBarrier(resources.pDepthTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(resources.pNormalsTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(m_pOutputTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					context.FlushResourceBarriers();

					DescriptorHandle descriptors = context.AllocateTransientDescriptors(4, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ID3D12Device* pDevice = m_pGraphics->GetDevice();

					DescriptorHandle renderTargetUAV = descriptors;
					pDevice->CopyDescriptorsSimple(1, renderTargetUAV.GetCpuHandle(), m_pOutputTexture->GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					descriptors += pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					DescriptorHandle tlasSRV = descriptors;
					pDevice->CopyDescriptorsSimple(1, tlasSRV.GetCpuHandle(), m_pTLAS->GetSRV()->GetDescriptor(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					descriptors += pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					DescriptorHandle textureSRVs = descriptors;
					pDevice->CopyDescriptorsSimple(1, descriptors.GetCpuHandle(), resources.pNormalsTexture->GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					descriptors += pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					pDevice->CopyDescriptorsSimple(1, descriptors.GetCpuHandle(), resources.pDepthTexture->GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

					constexpr const int numRandomVectors = 64;
					struct CameraParameters
					{
						Matrix ViewInverse;
						Matrix ProjectionInverse;
						Vector4 RandomVectors[numRandomVectors];
					} cameraData;

					static bool written = false;
					static Vector4 randoms[numRandomVectors];
					if (!written)
					{
						for (int i = 0; i < numRandomVectors; ++i)
						{
							randoms[i] = Vector4(Math::RandVector());
							randoms[i].z = Math::Lerp(0.1f, 0.8f, (float)abs(randoms[i].z));
							randoms[i].Normalize();
							randoms[i] *= Math::Lerp(0.2f, 1.0f, (float)pow(Math::RandomRange(0, 1), 2));
						}
						written = true;
					}
					memcpy(cameraData.RandomVectors, randoms, sizeof(Vector4) * numRandomVectors);

					cameraData.ViewInverse = resources.pCamera->GetViewInverse();
					cameraData.ProjectionInverse = resources.pCamera->GetProjectionInverse();

					DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(CameraParameters));
					memcpy((char*)allocation.pMappedMemory, &cameraData, sizeof(CameraParameters));

					sbtGenerator.AddMissProgram(L"Miss", {});
					sbtGenerator.AddRayGenerationProgram(L"RayGen", { reinterpret_cast<uint64*>(allocation.GpuHandle), reinterpret_cast<uint64*>(renderTargetUAV.GetGpuHandle().ptr), reinterpret_cast<uint64*>(tlasSRV.GetGpuHandle().ptr), reinterpret_cast<uint64*>(textureSRVs.GetGpuHandle().ptr) });
					sbtGenerator.AddHitGroup(L"HitGroup", {});
					sbtAllocation = context.AllocateTransientMemory(sbtGenerator.ComputeSBTSize());
					sbtGenerator.Generate(sbtAllocation.pMappedMemory, m_pStateObjectProperties.Get());
				}
				{
					D3D12_DISPATCH_RAYS_DESC rayDesc{};
					rayDesc.Width = m_pOutputTexture->GetWidth();
					rayDesc.Height = m_pOutputTexture->GetHeight();
					rayDesc.Depth = 1;
					rayDesc.RayGenerationShaderRecord.StartAddress = sbtAllocation.GpuHandle;
					rayDesc.RayGenerationShaderRecord.SizeInBytes = sbtGenerator.GetRayGenSectionSize();
					rayDesc.MissShaderTable.StartAddress = sbtAllocation.GpuHandle + sbtGenerator.GetRayGenSectionSize();
					rayDesc.MissShaderTable.SizeInBytes = sbtGenerator.GetMissSectionSize();
					rayDesc.MissShaderTable.StrideInBytes = sbtGenerator.GetMissEntrySize();
					rayDesc.HitGroupTable.StartAddress = sbtAllocation.GpuHandle + sbtGenerator.GetRayGenSectionSize() + sbtGenerator.GetMissSectionSize();
					rayDesc.HitGroupTable.SizeInBytes = sbtGenerator.GetHitGroupSectionSize();
					rayDesc.HitGroupTable.StrideInBytes = sbtGenerator.GetHitGroupEntrySize();
					context.InsertResourceBarrier(m_pOutputTexture.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
					context.ClearUavUInt(m_pOutputTexture.get(), pOutputRawUAV);
					context.FlushResourceBarriers();

					pCmd->SetPipelineState1(m_pStateObject.Get());
					pCmd->DispatchRays(&rayDesc);

					GPU_PROFILE_SCOPE("CopyTarget", &context);
					context.CopyResource(m_pOutputTexture.get(), resources.pRenderTarget);
				}
			};
		});

}

void Raytracing::GenerateAccelerationStructure(Graphics* pGraphics, Mesh* pMesh, CommandContext& context)
{
	if (pGraphics->SupportsRayTracing() == false)
	{
		return;
	}
	ID3D12GraphicsCommandList* pC = context.GetCommandList();
	ComPtr<ID3D12GraphicsCommandList4> pCmd;
	pC->QueryInterface(IID_PPV_ARGS(pCmd.GetAddressOf()));
	ComPtr<ID3D12Device5> pDevice;
	pGraphics->GetDevice()->QueryInterface(IID_PPV_ARGS(pDevice.GetAddressOf()));
	if (!pCmd || !pDevice)
	{
		return;
	}

	//Bottom Level Acceleration Structure
	{
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
		for (size_t i = 0; i < pMesh->GetMeshCount(); ++i)
		{
			SubMesh* pSubMesh = pMesh->GetMesh((int)i);
			D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
			geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			geometryDesc.Triangles.IndexBuffer = pSubMesh->GetIndexBuffer()->GetGpuHandle();
			geometryDesc.Triangles.IndexCount = pSubMesh->GetIndexBuffer()->GetDesc().ElementCount;
			geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
			geometryDesc.Triangles.Transform3x4 = 0;
			geometryDesc.Triangles.VertexBuffer.StartAddress = pSubMesh->GetVertexBuffer()->GetGpuHandle();
			geometryDesc.Triangles.VertexBuffer.StrideInBytes = pSubMesh->GetVertexBuffer()->GetDesc().ElementSize;
			geometryDesc.Triangles.VertexCount = pSubMesh->GetVertexBuffer()->GetDesc().ElementCount;
			geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			geometries.push_back(geometryDesc);
		}

		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS prebuildInfo{};
		prebuildInfo.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		prebuildInfo.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		prebuildInfo.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		prebuildInfo.NumDescs = (uint32)geometries.size();
		prebuildInfo.pGeometryDescs = geometries.data();

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
		pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

		m_pBLASScratch = std::make_unique<Buffer>(pGraphics, "BLAS Scratch Buffer");
		m_pBLASScratch->Create(BufferDesc::CreateByteAddress(Math::AlignUp<uint64>(info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess));
		m_pBLAS = std::make_unique<Buffer>(pGraphics, "BLAS");
		m_pBLAS->Create(BufferDesc::CreateAccelerationStructure(Math::AlignUp<uint64>(info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT), BufferFlag::UnorderedAccess));

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
		prebuildInfo.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
		prebuildInfo.NumDescs = 1;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
		pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&prebuildInfo, &info);

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

void Raytracing::SetupResources(Graphics* pGraphics)
{
	m_pOutputTexture = std::make_unique<Texture>(pGraphics, "Raytracing Output");
}

void Raytracing::SetupPipelines(Graphics* pGraphics)
{
	//Raytracing Pipeline
	{
		m_pRayGenSignature = std::make_unique<RootSignature>();
		m_pRayGenSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pRayGenSignature->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pRayGenSignature->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pRayGenSignature->SetDescriptorTableSimple(3, 1, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, D3D12_SHADER_VISIBILITY_ALL);
		m_pRayGenSignature->Finalize("Ray Gen RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
		m_pHitSignature = std::make_unique<RootSignature>();
		m_pHitSignature->Finalize("Hit RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
		m_pMissSignature = std::make_unique<RootSignature>();
		m_pMissSignature->Finalize("Hit RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
		m_pDummySignature = std::make_unique<RootSignature>();
		m_pDummySignature->Finalize("Dummy Global RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ShaderLibrary rayGenShader("Resources/RayTracingShaders/RayGen.hlsl");
		ShaderLibrary hitShader("Resources/RayTracingShaders/Hit.hlsl");
		ShaderLibrary missShader("Resources/RayTracingShaders/Miss.hlsl");

		CD3DX12_STATE_OBJECT_DESC desc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
		{
			CD3DX12_DXIL_LIBRARY_SUBOBJECT* pRayGenDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
			pRayGenDesc->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE(rayGenShader.GetByteCode(), rayGenShader.GetByteCodeSize()));
			pRayGenDesc->DefineExport(L"RayGen");
			CD3DX12_DXIL_LIBRARY_SUBOBJECT* pHitDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
			pHitDesc->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE(hitShader.GetByteCode(), hitShader.GetByteCodeSize()));
			pHitDesc->DefineExport(L"ClosestHit");
			CD3DX12_DXIL_LIBRARY_SUBOBJECT* pMissDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
			pMissDesc->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE(missShader.GetByteCode(), missShader.GetByteCodeSize()));
			pMissDesc->DefineExport(L"Miss");
		}
		{
			CD3DX12_HIT_GROUP_SUBOBJECT* pHitGroupDesc = desc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
			pHitGroupDesc->SetHitGroupExport(L"HitGroup");
			pHitGroupDesc->SetClosestHitShaderImport(L"ClosestHit");
		}
		{
			CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pRayGenRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
			pRayGenRs->SetRootSignature(m_pRayGenSignature->GetRootSignature());
			CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pRayGenAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			pRayGenAssociation->AddExport(L"RayGen");
			pRayGenAssociation->SetSubobjectToAssociate(*pRayGenRs);

			CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pMissRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
			pMissRs->SetRootSignature(m_pMissSignature->GetRootSignature());
			CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pMissAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			pMissAssociation->AddExport(L"Miss");
			pMissAssociation->SetSubobjectToAssociate(*pMissRs);

			CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT* pHitRs = desc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
			pHitRs->SetRootSignature(m_pHitSignature->GetRootSignature());
			CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT* pHitAssociation = desc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
			pHitAssociation->AddExport(L"HitGroup");
			pHitAssociation->SetSubobjectToAssociate(*pHitRs);
		}
		{
			CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT* pRtConfig = desc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
			pRtConfig->Config(4 * sizeof(float), 2 * sizeof(float));

			CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT* pRtPipelineConfig = desc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
			pRtPipelineConfig->Config(1);

			CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT* pGlobalRs = desc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
			pGlobalRs->SetRootSignature(m_pDummySignature->GetRootSignature());
		}
		D3D12_STATE_OBJECT_DESC stateObject = *desc;

		ComPtr<ID3D12Device5> pDevice;
		pGraphics->GetDevice()->QueryInterface(IID_PPV_ARGS(pDevice.GetAddressOf()));

		HR(pDevice->CreateStateObject(&stateObject, IID_PPV_ARGS(m_pStateObject.GetAddressOf())));
		HR(m_pStateObject->QueryInterface(IID_PPV_ARGS(m_pStateObjectProperties.GetAddressOf())));
	}
}