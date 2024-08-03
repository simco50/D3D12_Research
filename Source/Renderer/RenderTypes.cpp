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
	Ref<RootSignature> pCommonRS;
	Ref<RootSignature> pCommonRSWithIA;

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

		pCommonRS = new RootSignature(pDevice);
		pCommonRS->AddRootCBV(0, 0);
		pCommonRS->AddRootCBV(1, 0);
		pCommonRS->AddRootCBV(2, 0);
		pCommonRS->AddDescriptorTable(0, 16, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0);
		pCommonRS->AddDescriptorTable(0, 64, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0);
		pCommonRS->Finalize("Common");

		pCommonRSWithIA = new RootSignature(pDevice);
		pCommonRSWithIA->AddRootCBV(0, 0);
		pCommonRSWithIA->AddRootCBV(1, 0);
		pCommonRSWithIA->AddRootCBV(2, 0);
		pCommonRSWithIA->AddDescriptorTable(0, 16, D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0);
		pCommonRSWithIA->AddDescriptorTable(0, 64, D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0);
		pCommonRSWithIA->Finalize("Common with IA", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
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
		pCommonRS.Reset();
		pCommonRSWithIA.Reset();
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
