#pragma once
#include "GraphicsResource.h"

class RootSignature;
class ShaderLibrary;
class StateObject;

class StateObjectInitializer
{
public:
	std::string Name;
	D3D12_STATE_OBJECT_TYPE Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	uint32 MaxRecursion = 1;
	D3D12_RAYTRACING_PIPELINE_FLAGS Flags = D3D12_RAYTRACING_PIPELINE_FLAG_NONE;
	RootSignature* pGlobalRootSignature = nullptr;
	uint32 MaxPayloadSize = 0;
	uint32 MaxAttributeSize = 0;
	std::string RayGenShader;
	void AddHitGroup(const std::string& name, const std::string& closestHit = "", const std::string& anyHit = "", const std::string& intersection = "", RootSignature* pRootSignature = nullptr);
	void AddLibrary(ShaderLibrary* pLibrary, const std::vector<std::string>& exports = {});
	void AddCollection(StateObject* pOtherObject);
	void AddMissShader(const std::string& exportName, RootSignature* pRootSignature = nullptr);
	void SetRayGenShader(const std::string& exportName);

	D3D12_STATE_OBJECT_DESC Desc();

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

	template<size_t SIZE>
	class DataAllocator
	{
	public:
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
		std::array<char, SIZE> m_Data;
	};

	wchar_t* GetUnicode(const std::string& text)
	{
		size_t len = text.length();
		wchar_t* pData = m_ContentData.Allocate<wchar_t>((int)len + 1);
		MultiByteToWideChar(0, 0, text.c_str(), (int)len, pData, (int)len);
		return pData;
	}

	DataAllocator<1 << 8> m_StateObjectData{};
	DataAllocator<1 << 10> m_ContentData{};
};

class StateObject : public GraphicsObject
{
public:
	StateObject(Graphics* pGraphics);

	void Create(const StateObjectInitializer& initializer);
	ID3D12StateObject* GetStateObject() const { return m_pStateObject.Get(); }

private:
	ComPtr<ID3D12StateObject> m_pStateObject;
	StateObjectInitializer m_Desc;
};
