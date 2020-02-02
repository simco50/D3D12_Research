#pragma once

class Graphics;
class Texture;
struct TextureDesc;

namespace RG
{
	class ResourceAllocator
	{
	public:
		ResourceAllocator(Graphics* pGraphics)
			: m_pGraphics(pGraphics)
		{}

		Texture* CreateTexture(const TextureDesc& desc);
		void ReleaseTexture(Texture* pTexture);

	private:
		Graphics* m_pGraphics;
		std::vector<std::unique_ptr<Texture>> m_Textures;
		std::vector<Texture*> m_TextureCache;
	};
}