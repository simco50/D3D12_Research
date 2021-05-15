#pragma once
#include "CharConv.h"

class IConsoleObject;

class CVarManager
{
public:
	static CVarManager& Get();
	void Initialize();

	void RegisterConsoleObject(const char* pName, IConsoleObject* pObject);
	bool Execute(const char* pCommand);

	IConsoleObject* FindConsoleObject(const char* pName)
	{
		auto it = m_Map.find(pName);
		return it != m_Map.end() ? it->second : nullptr;
	}

	template<typename T>
	void ForEachCvar(T&& callback) const
	{
		for (const auto& cvar : m_Objects)
		{
			callback(cvar);
		}
	}

private:
	std::unordered_map<StringHash, IConsoleObject*> m_Map;
	std::vector<IConsoleObject*> m_Objects;
};

class IConsoleObject
{
public:
	IConsoleObject(const char* pName)
		: m_pName(pName)
	{
		CVarManager::Get().RegisterConsoleObject(pName, this);
	}
	virtual ~IConsoleObject() = default;
	virtual bool Execute(const char** pArgs, int numArgs) = 0;

	const char* GetName() const { return m_pName; }

private:
	const char* m_pName;
};

template<typename T, bool = std::is_pointer_v<T>>
struct DecayNonPointer { };

template<typename T>
struct DecayNonPointer<T, 0>
{
	using Type = std::remove_const_t<std::remove_reference_t<T>>;
};

template<typename T>
struct DecayNonPointer<T, 1>
{
	using Type = T;
};

#ifdef CPP_DELEGATES

template<typename ...Args>
class DelegateConsoleCommand : public IConsoleObject
{
public:
	DelegateConsoleCommand(const char* pName, Delegate<void, Args...>&& delegate)
		: IConsoleObject(pName), m_Callback(std::move(delegate))
	{
	}

	template<typename T>
	DelegateConsoleCommand(const char* pName, T&& callback)
		: IConsoleObject(pName), m_Callback(Delegate<void, Args...>::CreateLambda(std::move(callback)))
	{
	}

	virtual bool Execute(const char** pArgs, int numArgs) override
	{
		if (numArgs == sizeof...(Args))
		{
			ExecuteInternal(pArgs, numArgs, std::index_sequence_for<Args...>());
			return true;
		}
		else
		{
			E_LOG(Warning, "Incorrect number of arguments. Expected: %d. Given: %d", sizeof...(Args), numArgs);
		}
		return false;
	}

private:
	template<size_t... Is>
	bool ExecuteInternal(const char** pArgs, int numArgs, std::index_sequence<Is...>)
	{
		int failIndex = -1;
		std::tuple<typename DecayNonPointer<Args>::Type...> arguments = CharConv::TupleFromArguments<typename DecayNonPointer<Args>::Type...>(pArgs, &failIndex);
		if (failIndex >= 0)
		{
			E_LOG(Warning, "Failed to convert argument '%s'", pArgs[failIndex]);
			return false;
		}
		m_Callback.Execute(std::get<Is>(arguments)...);
		return true;
	}
	Delegate<void, Args...> m_Callback;
};

#endif

template<typename ...Args>
class ConsoleCommand : public IConsoleObject
{
public:
	using CallbackFn = void(*)(Args...);

	ConsoleCommand(const char* pName, CallbackFn fn)
		: IConsoleObject(pName), m_Callback(fn)
	{
	}

	virtual bool Execute(const char** pArgs, int numArgs) override
	{
		if (numArgs == sizeof...(Args))
		{
			return ExecuteInternal(pArgs, numArgs, std::index_sequence_for<Args...>());
		}
		else
		{
			E_LOG(Warning, "Incorrect number of arguments. Expected: %d. Given: %d", sizeof...(Args), numArgs);
		}
		return false;
	}

private:
	template<size_t... Is>
	bool ExecuteInternal(const char** pArgs, int numArgs, std::index_sequence<Is...>)
	{
		int failIndex = -1;
		std::tuple<DecayNonPointer<Args>::Type...> arguments = CharConv::TupleFromArguments<DecayNonPointer<Args>::Type...>(pArgs, &failIndex);
		if (failIndex >= 0)
		{
			E_LOG(Warning, "Failed to convert argument '%s'", pArgs[failIndex]);
			return false;
		}
		m_Callback(std::get<Is>(arguments)...);
		return true;
	}

	CallbackFn m_Callback;
};

template<typename T>
class ConsoleVariable : public IConsoleObject
{
public:
	ConsoleVariable(const char* pName, T defaultValue, Delegate<void, IConsoleObject*> onModified = {})
		: IConsoleObject(pName), m_Value(defaultValue), m_OnModified(onModified)
	{}

	void SetValue(const T& value)
	{
		m_Value = value;
		m_OnModified.ExecuteIfBound(this);
	}

	ConsoleVariable& operator=(const T& value)
	{
		m_Value = value;
		m_OnModified.ExecuteIfBound(this);
		return *this;
	}

	virtual bool Execute(const char** pArgs, int numArgs) override
	{
		if (numArgs == 0)
		{
			std::string val;
			CharConv::ToString(m_Value, &val);
			E_LOG(Info, "%s: %s", GetName(), val.c_str());
		}
		else if (numArgs > 0)
		{
			T val;
			if (CharConv::FromString(pArgs[0], val))
			{
				SetValue(val);
				m_OnModified.ExecuteIfBound(this);
				E_LOG(Info, "%s: %s", GetName(), pArgs[0]);
				return true;
			}
		}
		return false;
	}

	operator const T& () const { return m_Value; }
	T* operator&() { return &m_Value; }

	T& Get() { return m_Value; }
	const T& Get() const { return m_Value; }

private:
	T m_Value;
	Delegate<void, IConsoleObject*> m_OnModified;
};

template<typename T>
class ConsoleVariableRef : public IConsoleObject
{
public:
	ConsoleVariableRef(const char* pName, T& value, Delegate<void, IConsoleObject*> onModified = {})
		: IConsoleObject(pName), m_Value(value), m_OnModified(onModified)
	{}

	virtual bool Execute(const char** pArgs, int numArgs) override
	{
		if (numArgs > 0)
		{
			T val;
			if (CharConv::FromString(pArgs[0], val))
			{
				m_Value = val;
				m_OnModified.ExecuteIfBound(this);
				return true;
			}
		}
		return false;
	}

	T& Get() { return m_Value; }
	const T& Get() const { return m_Value; }

private:
	T& m_Value;
	Delegate<void, IConsoleObject*> m_OnModified;
};

class ImGuiConsole
{
public:
	void Update(const ImVec2& position, const ImVec2& size);

private:
	int InputCallback(ImGuiInputTextCallbackData* pCallbackData);

	std::vector<std::string> m_History;
	std::vector<const char*> m_Suggestions;
	std::array<char, 1024> m_Input{};
	int m_HistoryPos = -1;
	int m_SuggestionPos = -1;

	bool m_ShowConsole = false;
	bool m_FocusConsole = true;
	bool m_AutoCompleted = false;
};

