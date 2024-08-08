#pragma once
#include "DeviceResource.h"
#include "Shader.h"
#include "Core/TaskQueue.h"

class ShaderManager;
struct Shader;

class StateObjectInitializer
{
public:
	friend class StateObject;

	void AddHitGroup(const String& name, const String& closestHit = "", const String& anyHit = "", const String& intersection = "", RootSignature* pRootSignature = nullptr);
	void AddLibrary(const char* pShaderPath, Span<const char*> exports = {}, Span<ShaderDefine> defines = {});
	void AddCollection(StateObject* pOtherObject);
	void AddMissShader(const String& exportName, RootSignature* pRootSignature = nullptr);

	bool CreateStateObjectStream(class StateObjectStream& stateObjectStream, GraphicsDevice* pDevice);
	void SetMaxPipelineStackSize(StateObject* pStateObject);

	String Name;
	uint32 MaxRecursion = 1;
	RootSignature* pGlobalRootSignature = nullptr;
	uint32 MaxPayloadSize = 0;
	uint32 MaxAttributeSize = sizeof(float) * 2; // Default size for barycentrics
	String RayGenShader;
	D3D12_STATE_OBJECT_TYPE Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	D3D12_RAYTRACING_PIPELINE_FLAGS Flags = D3D12_RAYTRACING_PIPELINE_FLAG_NONE;

private:
	struct HitGroupDefinition
	{
		HitGroupDefinition() = default;
		String Name;
		String ClosestHit;
		String AnyHit;
		String Intersection;
		RootSignature* pLocalRootSignature = nullptr;
	};
	struct LibraryShaderExport
	{
		String Name;
		RootSignature* pLocalRootSignature = nullptr;
	};
	struct LibraryExports
	{
		String Path;
		Array<ShaderDefine> Defines;
		Array<const char*> Exports;
	};
	Array<Shader*> m_Shaders;
	Array<LibraryExports> m_Libraries;
	Array<HitGroupDefinition> m_HitGroups;
	Array<LibraryShaderExport> m_MissShaders;
	Array<StateObject*> m_Collections;
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
	ID3D12StateObjectProperties1* GetStateObjectProperties() const { return m_pStateObjectProperties.Get(); }
	uint64 GetWorkgraphBufferSize() const;

	void Create(ID3D12StateObject* pStateObject);

private:
	void CreateInternal();
	void OnLibraryReloaded(Shader* pLibrary);

	bool m_NeedsReload = false;
	Ref<ID3D12StateObject> m_pStateObject;
	Ref<ID3D12StateObjectProperties1> m_pStateObjectProperties;
	StateObjectInitializer m_Desc;
	DelegateHandle m_ReloadHandle;
};
