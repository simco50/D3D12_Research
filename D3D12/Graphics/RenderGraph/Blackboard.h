#pragma once
#include "RenderGraphDefinitions.h"

#define RG_BLACKBOARD_DATA(clazz) \
	template<> inline constexpr StringHash RGBlackboard::GetTypeHash<clazz>() { return StringHash(#clazz STRINGIFY(__COUNTER__)); }

class RGBlackboard final
{
public:
	RGBlackboard() = default;
	~RGBlackboard() = default;

	RGBlackboard(const RGBlackboard& other) = delete;
	RGBlackboard& operator=(const RGBlackboard& other) = delete;

	template<typename T, typename... Args>
	T& Add(Args&&... args)
	{
		constexpr StringHash hash = GetTypeHash<T>();
		check(m_DataMap.find(hash) == m_DataMap.end(), "Data type already exists in blackboard");
		std::unique_ptr<TElement<T>> pAllocation = std::make_unique<TElement<T>>(std::forward<Args&&>(args)...);
		T& obj = pAllocation->Object;
		m_DataMap[hash] = &obj;
		m_Allocations.push_back(std::move(pAllocation));
		return obj;
	}

	template<typename T>
	const T* TryGet() const
	{
		constexpr StringHash hash = GetTypeHash<T>();
		auto it = m_DataMap.find(hash);
		if (it != m_DataMap.end())
		{
			return static_cast<const T*>(it->second);
		}
		return m_pParent ? m_pParent->TryGet<T>() : nullptr;
	}

	template<typename T>
	const T& Get() const
	{
		const T* pObj = TryGet<T>();
		check(pObj, "Data for given type does not exist in blackboard");
		return *pObj;
	}

	RGBlackboard& Branch();
	void Merge(const RGBlackboard& other, bool overrideExisting);

	template<typename T>
	static constexpr StringHash GetTypeHash()
	{
		static_assert(!sizeof(T), "Type requires RG_BLACKBOARD_DATA");
		return 0;
	}

private:
	struct Element
	{
		virtual ~Element() = default;
	};
	template<typename T>
	struct TElement : public Element
	{
		template<typename... Args>
		TElement(Args&&... args)
			: Object(args...)
		{}
		T Object;
	};

	std::unordered_map<StringHash, void*> m_DataMap;
	std::vector<std::unique_ptr<Element>> m_Allocations;
	std::vector<std::unique_ptr<RGBlackboard>> m_Children;
	RGBlackboard* m_pParent = nullptr;
};
