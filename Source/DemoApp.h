#pragma once
#include "App.h"
#include "Renderer/Renderer.h"
#include "Scene/World.h"

class DemoApp : public App
{
public:
	DemoApp();
	~DemoApp();

	virtual void Init() override;
	virtual void Update() override;
	virtual void Shutdown() override;

private:
	void SetupScene(const char* pFilePath);

	void DrawOutliner();

	void DrawImGui();

	Ref<Texture> m_pViewportTexture;
	Renderer m_Renderer;
	World m_World;
};
