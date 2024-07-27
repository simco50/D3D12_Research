#pragma once

struct World;
class GraphicsDevice;

class SceneLoader
{
public:
	static bool Load(const char* pFilePath, GraphicsDevice* pDevice, World& world);
};
