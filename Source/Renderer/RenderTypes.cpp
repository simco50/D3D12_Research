#include "stdafx.h"
#include "RenderTypes.h"

#include "Core/Image.h"
#include "RHI/RHI.h"
#include "RHI/Texture.h"
#include "RHI/Device.h"
#include "RHI/RootSignature.h"
#include "RHI/PipelineState.h"
#include "RHI/CommandContext.h"

namespace GraphicsCommon
{
	static StaticArray<Ref<Texture>, (uint32)DefaultTexture::MAX> DefaultTextures;

	Ref<CommandSignature> pIndirectDrawSignature;
	Ref<CommandSignature> pIndirectDispatchSignature;
	Ref<CommandSignature> pIndirectDispatchMeshSignature;
	Ref<RootSignature> pCommonRS_DEPRECATED;
	Ref<RootSignature> pCommonRSV2;

	static void AddCommonStaticSamplers(RootSignature* pRootSignature)
	{
		int staticSamplerRegisterSlot = 0;
		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_BORDER);

		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER);

		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_BORDER);
		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_BORDER);
		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_BORDER);

		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_COMPARISON_FUNC_GREATER);
		pRootSignature->AddStaticSampler(staticSamplerRegisterSlot++, 1, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_COMPARISON_FUNC_GREATER);
	}

	void Create(GraphicsDevice* pDevice)
	{
		auto RegisterDefaultTexture = [pDevice](DefaultTexture type, const char* pName, const TextureDesc& desc, const uint32* pData)
			{
				D3D12_SUBRESOURCE_DATA data;
				data.pData = pData;
				data.RowPitch = RHI::GetRowPitch(desc.Format, desc.Width);
				data.SlicePitch = RHI::GetSlicePitch(desc.Format, desc.Width, desc.Height);
				Ref<Texture> pTexture = pDevice->CreateTexture(desc, pName, data);
				DefaultTextures[(int)type] = pTexture;
			};

		uint32 BLACK = Math::Pack_RGBA8_UNORM(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
		uint32 WHITE = Math::Pack_RGBA8_UNORM(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		uint32 MAGENTA = Math::Pack_RGBA8_UNORM(Vector4(1.0f, 0.0f, 1.0f, 1.0f));
		uint32 GRAY = Math::Pack_RGBA8_UNORM(Vector4(0.5f, 0.5f, 0.5f, 1.0f));
		uint32 DEFAULT_NORMAL = Math::Pack_RGBA8_UNORM(Vector4(0.5f, 0.5f, 1.0f, 1.0f));
		uint32 DEFAULT_ROUGHNESS_METALNESS = Math::Pack_RGBA8_UNORM(Vector4(0.5f, 0.0f, 1.0f, 1.0f));

		const TextureFlag textureFlags = TextureFlag::ShaderResource;
		RegisterDefaultTexture(DefaultTexture::Black2D, "Default Black", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &BLACK);
		RegisterDefaultTexture(DefaultTexture::White2D, "Default White", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &WHITE);
		RegisterDefaultTexture(DefaultTexture::Magenta2D, "Default Magenta", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &MAGENTA);
		RegisterDefaultTexture(DefaultTexture::Gray2D, "Default Gray", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &GRAY);
		RegisterDefaultTexture(DefaultTexture::Normal2D, "Default Normal", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &DEFAULT_NORMAL);
		RegisterDefaultTexture(DefaultTexture::RoughnessMetalness, "Default Roughness/Metalness", TextureDesc::Create2D(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &DEFAULT_ROUGHNESS_METALNESS);

		uint32 BLACK_CUBE[6] = {};
		RegisterDefaultTexture(DefaultTexture::BlackCube, "Default Black Cube", TextureDesc::CreateCube(1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), BLACK_CUBE);

		RegisterDefaultTexture(DefaultTexture::Black3D, "Default Black 3D", TextureDesc::Create3D(1, 1, 1, ResourceFormat::RGBA8_UNORM, 1, textureFlags), &BLACK);

		constexpr uint32 checkerPixels[] =
		{
			0xFFFFFFFF, 0xFF000000,
			0xFF000000, 0xFFFFFFFF
		};
		RegisterDefaultTexture(DefaultTexture::CheckerPattern, "Checker Pattern", TextureDesc::Create2D(2, 2, ResourceFormat::RGBA8_UNORM, 1, textureFlags), checkerPixels);

		DefaultTextures[(int)DefaultTexture::ColorNoise256] = CreateTextureFromFile(pDevice, "Resources/Textures/Noise.png", false, "Noise");
		DefaultTextures[(int)DefaultTexture::BlueNoise512] = CreateTextureFromFile(pDevice, "Resources/Textures/BlueNoise.dds", false, "Blue Noise");

		{
			CommandSignatureInitializer sigDesc;
			sigDesc.AddDispatch();
			pIndirectDispatchSignature = pDevice->CreateCommandSignature(sigDesc, "Default Indirect Dispatch");
		}
		{
			CommandSignatureInitializer sigDesc;
			sigDesc.AddDraw();
			pIndirectDrawSignature = pDevice->CreateCommandSignature(sigDesc, "Default Indirect Draw");
		}
		{
			CommandSignatureInitializer sigDesc;
			sigDesc.AddDispatchMesh();
			pIndirectDispatchMeshSignature = pDevice->CreateCommandSignature(sigDesc, "Default Indirect Dispatch Mesh");
		}

		pCommonRSV2 = new RootSignature(pDevice);
		pCommonRSV2->AddRootSRV(0, 100);															// PerInstance
		pCommonRSV2->AddRootSRV(1, 100);															// PerPass
		pCommonRSV2->AddRootSRV(2, 100);															// PerView
		AddCommonStaticSamplers(pCommonRSV2);
		pCommonRSV2->Finalize("Common Rootsignature V2", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		pCommonRS_DEPRECATED = new RootSignature(pDevice);
		pCommonRS_DEPRECATED->AddRootCBV(0, 100);													// PerInstance
		pCommonRS_DEPRECATED->AddRootCBV(1, 100);													// PerPass
		pCommonRS_DEPRECATED->AddRootSRV(2, 100);													// PerView
		pCommonRS_DEPRECATED->AddDescriptorTable(0, 16, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0);		// UAV
		pCommonRS_DEPRECATED->AddDescriptorTable(0, 64, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0);		// SRV
		AddCommonStaticSamplers(pCommonRS_DEPRECATED);
		pCommonRS_DEPRECATED->Finalize("Common Rootsignature DEPRECATED", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	}

	void Destroy()
	{
		for (auto& pTexture : DefaultTextures)
		{
			pTexture.Reset();
		}

		pIndirectDispatchSignature.Reset();
		pIndirectDrawSignature.Reset();
		pIndirectDispatchMeshSignature.Reset();
		pCommonRS_DEPRECATED.Reset();
		pCommonRSV2.Reset();
	}

	Texture* GetDefaultTexture(DefaultTexture type)
	{
		return DefaultTextures[(int)type];
	}

	Ref<Texture> CreateTextureFromImage(GraphicsDevice* pDevice, const Image& image, bool sRGB, const char* pName)
	{
		TextureDesc desc;
		desc.Width = image.GetWidth();
		desc.Height = image.GetHeight();
		desc.Format = image.GetFormat();
		desc.Mips = image.GetMipLevels();
		desc.Flags = TextureFlag::ShaderResource;
		if (sRGB)
		{
			desc.Flags |= TextureFlag::sRGB;
		}
		desc.Type = image.IsCubemap() ? TextureType::TextureCube : TextureType::Texture2D;
		if (RHI::GetFormatInfo(desc.Format).IsBC)
		{
			desc.Width = Math::Max(desc.Width, 4u);
			desc.Height = Math::Max(desc.Height, 4u);
		}
		if (image.GetDepth() > 1)
		{
			desc.Depth = image.GetDepth();
			desc.Type = TextureType::Texture3D;
		}

		Array<D3D12_SUBRESOURCE_DATA> subResourceData;
		const Image* pImg = &image;
		while (pImg)
		{
			for (uint32 i = 0; i < desc.Mips; ++i)
			{
				D3D12_SUBRESOURCE_DATA& data = subResourceData.emplace_back();
				data.pData = pImg->GetData(i);
				data.RowPitch = RHI::GetRowPitch(image.GetFormat(), desc.Width, i);
				data.SlicePitch = RHI::GetSlicePitch(image.GetFormat(), desc.Width, desc.Height, i);
			}
			pImg = pImg->GetNextImage();
		}
		Ref<Texture> pTexture = pDevice->CreateTexture(desc, pName ? pName : "", subResourceData);
		return pTexture;
	}

	Ref<Texture> CreateTextureFromFile(GraphicsDevice* pDevice, const char* pFilePath, bool sRGB, const char* pName)
	{
		Image image;
		if (image.Load(pFilePath))
		{
			return CreateTextureFromImage(pDevice, image, sRGB, pName);
		}
		return nullptr;
	}
}
