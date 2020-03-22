#include "stdafx.h"
#include "RTAO.h"
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

class ShaderBindingTable
{
private:
	struct TableEntry
	{
		std::vector<void*> data;
		void* pIdentifier = nullptr;
	};
public:
	ShaderBindingTable(ID3D12StateObject* pStateObject)
	{
		HR(pStateObject->QueryInterface(IID_PPV_ARGS(m_pObjectProperties.GetAddressOf())));
	}

	void AddRayGenEntry(const char* pName, const std::vector<void*>& data)
	{
		m_RayGenTable.push_back(CreateEntry(pName, data));
		uint32 entrySize = Math::AlignUp<uint32>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32)data.size() * 8, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
		m_RayGenEntrySize = Math::Max<int>(m_RayGenEntrySize, entrySize);
	}

	void AddMissEntry(const char* pName, const std::vector<void*>& data)
	{
		m_MissTable.push_back(CreateEntry(pName, data));
		uint32 entrySize = Math::AlignUp<uint32>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32)data.size() * 8, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
		m_MissEntrySize = Math::Max<int>(m_MissEntrySize, entrySize);
	}

	void AddHitGroupEntry(const char* pName, const std::vector<void*>& data)
	{
		m_HitTable.push_back(CreateEntry(pName, data));
		uint32 entrySize = Math::AlignUp<uint32>(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + (uint32)data.size() * 8, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
		m_HitEntrySize = Math::Max<int>(m_HitEntrySize, entrySize);
	}

	void Commit(CommandContext& context, D3D12_DISPATCH_RAYS_DESC& desc)
	{
		uint32 totalSize = 0;
		uint32 rayGenSection = m_RayGenEntrySize * (uint32)m_RayGenTable.size();
		uint32 rayGenSectionAligned = Math::AlignUp<uint32>(rayGenSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		uint32 missSection = m_MissEntrySize * (uint32)m_MissTable.size();
		uint32 missSectionAligned = Math::AlignUp<uint32>(missSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		uint32 hitSection = m_HitEntrySize * (uint32)m_HitTable.size();
		uint32 hitSectionAligned = Math::AlignUp<uint32>(hitSection, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
		totalSize = Math::AlignUp<uint32>(rayGenSectionAligned + missSectionAligned + hitSectionAligned, 256);
		DynamicAllocation allocation = context.AllocateTransientMemory(totalSize);
		char* pStart = (char*)allocation.pMappedMemory;
		char* pData = pStart;
		for (const TableEntry& e : m_RayGenTable)
		{
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64));
			pData += m_RayGenEntrySize;
		}
		pData = pStart + rayGenSectionAligned;
		for (const TableEntry& e : m_MissTable)
		{
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64));
			pData += m_RayGenEntrySize;
		}
		pData = pStart + rayGenSectionAligned + missSectionAligned;
		for (const TableEntry& e : m_HitTable)
		{
			memcpy(pData, e.pIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			memcpy(pData + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, e.data.data(), e.data.size() * sizeof(uint64));
			pData += m_RayGenEntrySize;
		}
		desc.RayGenerationShaderRecord.StartAddress = allocation.GpuHandle;
		desc.RayGenerationShaderRecord.SizeInBytes = rayGenSection;
		desc.MissShaderTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned;
		desc.MissShaderTable.SizeInBytes = missSection;
		desc.MissShaderTable.StrideInBytes = m_MissEntrySize;
		desc.HitGroupTable.StartAddress = allocation.GpuHandle + rayGenSectionAligned + missSectionAligned;
		desc.HitGroupTable.SizeInBytes = hitSection;
		desc.HitGroupTable.StrideInBytes = m_HitEntrySize;

		m_RayGenTable.clear();
		m_RayGenEntrySize = 0;
		m_MissTable.clear();
		m_MissEntrySize = 0;
		m_HitTable.clear();
		m_HitEntrySize = 0;
	}

private:
	TableEntry CreateEntry(const char* pName, const std::vector<void*>& data)
	{
		TableEntry entry;
		auto it = m_IdentifierMap.find(pName);
		if (it == m_IdentifierMap.end())
		{
			wchar_t wName[256];
			ToWidechar(pName, wName, 256);
			m_IdentifierMap[pName] = m_pObjectProperties->GetShaderIdentifier(wName);
		}
		entry.pIdentifier = m_IdentifierMap[pName];
		assert(entry.pIdentifier);
		entry.data = data;
		return entry;
	}

	ComPtr<ID3D12StateObjectProperties> m_pObjectProperties;
	std::vector<TableEntry> m_RayGenTable;
	uint32 m_RayGenEntrySize = 0;
	std::vector<TableEntry> m_MissTable;
	uint32 m_MissEntrySize = 0;
	std::vector<TableEntry> m_HitTable;
	uint32 m_HitEntrySize = 0;
	std::unordered_map<std::string, void*> m_IdentifierMap;
};

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

void RTAO::Execute(RGGraph& graph, const RaytracingInputResources& resources)
{
	graph.AddPass("Raytracing", [&](RGPassBuilder& builder)
		{
			builder.NeverCull();
			return [=](CommandContext& context, const RGPassResources& passResources)
			{
				{
					context.InsertResourceBarrier(resources.pDepthTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(resources.pNormalsTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(resources.pNoiseTexture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
					context.InsertResourceBarrier(resources.pRenderTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

					const int descriptorsToAllocate = 5;
					int totalAllocatedDescriptors = 0;
					DescriptorHandle descriptors = context.AllocateTransientDescriptors(descriptorsToAllocate, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ID3D12Device* pDevice = m_pGraphics->GetDevice();

					auto pfCopyDescriptors = [&](const std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>& sourceDescriptors)
					{
						DescriptorHandle originalHandle = descriptors;
						for (size_t i = 0; i < sourceDescriptors.size(); ++i)
						{
							if (totalAllocatedDescriptors >= descriptorsToAllocate)
							{
								assert(false);
							}

							pDevice->CopyDescriptorsSimple(1, descriptors.GetCpuHandle(), sourceDescriptors[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
							descriptors += pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
							++totalAllocatedDescriptors;
						}
						return originalHandle;
					};

					DescriptorHandle renderTargetUAV = pfCopyDescriptors({ resources.pRenderTarget->GetUAV() });
					DescriptorHandle tlasSRV = pfCopyDescriptors({ m_pTLAS->GetSRV()->GetDescriptor() });
					DescriptorHandle textureSRVs = pfCopyDescriptors({ resources.pNormalsTexture->GetSRV(), resources.pDepthTexture->GetSRV(), resources.pNoiseTexture->GetSRV() });

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
					memcpy(cameraData.RandomVectors, randoms, sizeof(Vector4) * numRandomVectors);

					cameraData.ViewInverse = resources.pCamera->GetViewInverse();
					cameraData.ProjectionInverse = resources.pCamera->GetProjectionInverse();

					DynamicAllocation allocation = context.AllocateTransientMemory(sizeof(CameraParameters), &cameraData);

					D3D12_DISPATCH_RAYS_DESC rayDesc{};
					ShaderBindingTable bindingTable(m_pStateObject.Get());
					bindingTable.AddRayGenEntry("RayGen", { reinterpret_cast<uint64*>(allocation.GpuHandle), reinterpret_cast<uint64*>(renderTargetUAV.GetGpuHandle().ptr), reinterpret_cast<uint64*>(tlasSRV.GetGpuHandle().ptr), reinterpret_cast<uint64*>(textureSRVs.GetGpuHandle().ptr) });
					bindingTable.AddMissEntry("Miss", {});
					bindingTable.AddHitGroupEntry("HitGroup", {});
					bindingTable.Commit(context, rayDesc);

					ID3D12GraphicsCommandList4* pCmd = context.GetRaytracingCommandList();

					rayDesc.Width = resources.pRenderTarget->GetWidth();
					rayDesc.Height = resources.pRenderTarget->GetHeight();
					rayDesc.Depth = 1;

					pCmd->SetPipelineState1(m_pStateObject.Get());
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
		m_pRayGenSignature->SetConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
		m_pRayGenSignature->SetDescriptorTableSimple(1, 0, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pRayGenSignature->SetDescriptorTableSimple(2, 0, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, D3D12_SHADER_VISIBILITY_ALL);
		m_pRayGenSignature->SetDescriptorTableSimple(3, 1, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, D3D12_SHADER_VISIBILITY_ALL);
		D3D12_SAMPLER_DESC samplerDesc{};
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		m_pRayGenSignature->AddStaticSampler(0, samplerDesc, D3D12_SHADER_VISIBILITY_ALL);
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		m_pRayGenSignature->AddStaticSampler(1, samplerDesc, D3D12_SHADER_VISIBILITY_ALL);
		m_pRayGenSignature->Finalize("Ray Gen RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
		m_pHitSignature = std::make_unique<RootSignature>();
		m_pHitSignature->Finalize("Hit RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
		m_pMissSignature = std::make_unique<RootSignature>();
		m_pMissSignature->Finalize("Miss RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
		m_pDummySignature = std::make_unique<RootSignature>();
		m_pDummySignature->Finalize("Dummy Global RS", pGraphics->GetDevice(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ShaderLibrary shaderLibrary("Resources/Shaders/RTAO.hlsl");

		CD3DX12_STATE_OBJECT_DESC desc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

		//Shaders
		{
			CD3DX12_DXIL_LIBRARY_SUBOBJECT* pRayGenDesc = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
			pRayGenDesc->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE(shaderLibrary.GetByteCode(), shaderLibrary.GetByteCodeSize()));
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
			pGlobalRs->SetRootSignature(m_pDummySignature->GetRootSignature());
		}
		D3D12_STATE_OBJECT_DESC stateObject = *desc;

		HR(pGraphics->GetRaytracingDevice()->CreateStateObject(&stateObject, IID_PPV_ARGS(m_pStateObject.GetAddressOf())));
		HR(m_pStateObject->QueryInterface(IID_PPV_ARGS(m_pStateObjectProperties.GetAddressOf())));
	}
}