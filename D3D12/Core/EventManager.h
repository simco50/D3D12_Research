#pragma once

class EventManager
{
public:
	template<typename ...Args>
	static DelegateHandle Register(StringHash name, typename MulticastDelegate<Args...>::DelegateT&& callback)
	{
		return Get<Args...>(name).Add(std::move(callback));
	}

	template<typename ...Args>
	static void Unregister(StringHash name, DelegateHandle& handle)
	{
		MulticastDelegate<Args...>* pDel = (MulticastDelegate<Args...>*)m_DelegateMap.at(name);
		return pDel->Remove(handle);
	}

	static void Remove(StringHash name)
	{
		m_DelegateMap.erase(name);
	}

	template<typename ...Args>
	static void Broadcast(StringHash name, Args... args)
	{
		Get<Args...>(name).Broadcast(std::forward<Args>(args)...);
	}

	static void Shutdown()
	{
		m_DelegateMap.clear();
	}

private:
	template<typename ...Args>
	static MulticastDelegate<Args...>& Get(StringHash name)
	{
		auto it = m_DelegateMap.find(name);
		if (it == m_DelegateMap.end())
		{
			m_DelegateMap[name] = std::make_unique<MulticastDelegate<Args...>>();
		}
		return *(MulticastDelegate<Args...>*)m_DelegateMap[name].get();
	}

	static std::unordered_map<StringHash, std::unique_ptr<MulticastDelegateBase>> m_DelegateMap;
};