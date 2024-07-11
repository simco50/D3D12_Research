#pragma once

struct World;
class GraphicsDevice;

class SceneLoader
{
public:
	static bool Load(const char* pFilePath, GraphicsDevice* pDevice, World& world, float scale = 1.0f);
};
