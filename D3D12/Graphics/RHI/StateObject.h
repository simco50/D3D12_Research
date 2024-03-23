#pragma once
#include "GraphicsResource.h"
#include "Shader.h"
#include "Core/TaskQueue.h"

class ShaderManager;
struct Shader;

class StateObjectInitializer
{
public:
	friend class StateObject;

	void AddHitGroup(const std::string& name, const std::string& closestHit = "", const std::string& anyHit = "", const std::string& intersection = "", RootSignature* pRootSignature = nullptr);
	void AddLibrary(const char* pShaderPath, const std::vector<std::string>& exports = {}, Span<ShaderDefine> defines = {});
	void AddCollection(StateObject* pOtherObject);
	void AddMissShader(const std::string& exportName, RootSignature* pRootSignature = nullptr);

	bool CreateStateObjectStream(class StateObjectStream& stateObjectStream, GraphicsDevice* pDevice);
	void SetMaxPipelineStackSize(StateObject* pStateObject);

	std::string Name;
	uint32 MaxRecursion = 1;
	RootSignature* pGlobalRootSignature = nullptr;
	uint32 MaxPayloadSize = 0;
	uint32 MaxAttributeSize = sizeof(float) * 2; // Default size for barycentrics
	std::string RayGenShader;
	D3D12_STATE_OBJECT_TYPE Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	D3D12_RAYTRACING_PIPELINE_FLAGS Flags = D3D12_RAYTRACING_PIPELINE_FLAG_NONE;

private:
	struct HitGroupDefinition
	{
		HitGroupDefinition() = default;
		std::string Name;
		std::string ClosestHit;
		std::string AnyHit;
		std::string Intersection;
		RootSignature* pLocalRootSignature = nullptr;
	};
	struct LibraryShaderExport
	{
		std::string Name;
		RootSignature* pLocalRootSignature = nullptr;
	};
	struct LibraryExports
	{
		std::string Path;
		std::vector<ShaderDefine> Defines;
		std::vector<std::string> Exports;
	};
	std::vector<Shader*> m_Shaders;
	std::vector<LibraryExports> m_Libraries;
	std::vector<HitGroupDefinition> m_HitGroups;
	std::vector<LibraryShaderExport> m_MissShaders;
	std::vector<StateObject*> m_Collections;
};

class StateObject : public DeviceObject
{
public:
	StateObject(GraphicsDevice* pParent, const StateObjectInitializer& initializer);
	StateObject(const StateObject& rhs) = delete;
	StateObject& operator=(const StateObject& rhs) = delete;

	void ConditionallyReload();
	const StateObjectInitializer& GetDesc() const { return m_Desc; }

	ID3D12StateObject* GetStateObject() const { return m_pStateObject; }
	ID3D12StateObjectProperties* GetStateObjectProperties() const { return m_pStateObjectProperties.Get(); }
	D3D12_PROGRAM_IDENTIFIER GetIdentifier();
	uint64 GetWorkgraphBufferSize() const;

private:
	void CreateInternal();
	void OnLibraryReloaded(Shader* pLibrary);

	bool m_NeedsReload = false;
	Ref<ID3D12StateObject> m_pStateObject;
	Ref<ID3D12StateObjectProperties> m_pStateObjectProperties;
	StateObjectInitializer m_Desc;
	DelegateHandle m_ReloadHandle;
};
