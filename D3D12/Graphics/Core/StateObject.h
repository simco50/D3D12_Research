#pragma once
#include "GraphicsResource.h"

class RootSignature;
class ShaderLibrary;
class StateObject;
class ShaderManager;

class StateObjectInitializer
{
public:
	class StateObjectStream
	{
		friend class StateObjectInitializer;
	public:
		D3D12_STATE_OBJECT_DESC Desc;
	private:
		template<size_t SIZE>
		struct DataAllocator
		{
			template<typename T>
			T* Allocate(uint32 count = 1)
			{
				assert(m_Offset + count * sizeof(T) <= SIZE);
				T* pData = reinterpret_cast<T*>(&m_Data[m_Offset]);
				m_Offset += count * sizeof(T);
				return pData;
			}
			void Reset() { m_Offset = 0; }
			const void* GetData() const { return m_Data.data(); }
			size_t Size() const { return m_Offset; }
		private:
			size_t m_Offset = 0;
			std::array<char, SIZE> m_Data{};
		};

		wchar_t* GetUnicode(const std::string& text)
		{
			size_t len = text.length();
			wchar_t* pData = ContentData.Allocate<wchar_t>((int)len + 1);
			MultiByteToWideChar(0, 0, text.c_str(), (int)len, pData, (int)len);
			return pData;
		}
		DataAllocator<1 << 8> StateObjectData{};
		DataAllocator<1 << 10> ContentData{};
	};

	friend class StateObject;

	void AddHitGroup(const std::string& name, const std::string& closestHit = "", const std::string& anyHit = "", const std::string& intersection = "", RootSignature* pRootSignature = nullptr);
	void AddLibrary(ShaderLibrary* pLibrary, const std::vector<std::string>& exports = {});
	void AddCollection(StateObject* pOtherObject);
	void AddMissShader(const std::string& exportName, RootSignature* pRootSignature = nullptr);

	void CreateStateObjectStream(StateObjectStream& stateObjectStream);
	void SetMaxPipelineStackSize(StateObject* pStateObject);

	std::string Name;
	uint32 MaxRecursion = 1;
	RootSignature* pGlobalRootSignature = nullptr;
	uint32 MaxPayloadSize = 0;
	uint32 MaxAttributeSize = 0;
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
		ShaderLibrary* pLibrary;
		std::vector<std::string> Exports;
	};
	std::vector<LibraryExports> m_Libraries;
	std::vector<HitGroupDefinition> m_HitGroups;
	std::vector<LibraryShaderExport> m_MissShaders;
	std::vector<StateObject*> m_Collections;
};

class StateObject : public GraphicsObject
{
public:
	StateObject(GraphicsDevice* pParent);
	StateObject(const StateObject& rhs) = delete;
	StateObject& operator=(const StateObject& rhs) = delete;

	void Create(const StateObjectInitializer& initializer);
	void ConditionallyReload();
	const StateObjectInitializer& GetDesc() const { return m_Desc; }

	ID3D12StateObject* GetStateObject() const { return m_pStateObject.Get(); }
	ID3D12StateObjectProperties* GetStateObjectProperties() const { return m_pStateObjectProperties.Get(); }

private:
	void OnLibraryReloaded(ShaderLibrary* pOldShaderLibrary, ShaderLibrary* pNewShaderLibrary);

	bool m_NeedsReload = false;
	ComPtr<ID3D12StateObject> m_pStateObject;
	ComPtr<ID3D12StateObjectProperties> m_pStateObjectProperties;
	StateObjectInitializer m_Desc;
	DelegateHandle m_ReloadHandle;
};
