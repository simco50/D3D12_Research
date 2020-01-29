#pragma once

namespace RG
{
	class Blackboard final
	{
#define RG_BLACKBOARD_DATA(clazz) constexpr static const char* Type() { return #clazz; }
	public:
		Blackboard();
		~Blackboard();

		Blackboard(const Blackboard& other) = delete;
		Blackboard& operator=(const Blackboard& other) = delete;

		template<typename T>
		T& Add()
		{
			RG_ASSERT(m_DataMap.find(T::Type()) == m_DataMap.end(), "Data type already exists in blackboard");
			T* pData = new T();
			m_DataMap[T::Type()] = pData;
			return *pData;
		}

		template<typename T>
		T& Get()
		{
			void* pData = GetData(T::Type());
			RG_ASSERT(pData, "Data for given type does not exist in blackboard");
			return *static_cast<T*>(pData);
		}

		template<typename T>
		const T& Get() const
		{
			void* pData = GetData(T::Type());
			RG_ASSERT(pData, "Data for given type does not exist in blackboard");
			return *static_cast<T*>(pData);
		}

		Blackboard& Branch();

	private:
		void* GetData(const std::string& name);

		std::map<std::string, void*> m_DataMap;
		std::vector<std::unique_ptr<Blackboard>> m_Children;
		Blackboard* m_pParent = nullptr;
	};
}