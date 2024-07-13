#pragma once
#include "CString.h"

class IConsoleObject;

class ConsoleManager
{
public:
	static void Initialize();
	static void RegisterConsoleObject(const char* pName, IConsoleObject* pObject);
	static bool Execute(const char* pCommand);

	NODISCARD static IConsoleObject* FindConsoleObject(const char* pName);

	template<typename T>
	static void ForEachCvar(T&& callback)
	{
		for (const auto& cvar : GetObjects())
		{
			callback(cvar);
		}
	}
private:
	static const Array<IConsoleObject*>& GetObjects();
};

class IConsoleObject
{
public:
	IConsoleObject(const char* pName)
		: m_pName(pName)
	{
		ConsoleManager::RegisterConsoleObject(pName, this);
	}
	virtual ~IConsoleObject() = default;

	virtual bool Set(const char* pValue) = 0;

	NODISCARD virtual int GetInt() const = 0;
	NODISCARD virtual float GetFloat() const = 0;
	NODISCARD virtual bool GetBool() const = 0;
	NODISCARD virtual String GetString() const = 0;

	NODISCARD virtual class IConsoleVariable* AsVariable() { return nullptr; }
	NODISCARD virtual class IConsoleCommand* AsCommand() { return nullptr; }

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

class IConsoleCommand : public IConsoleObject
{
public:
	IConsoleCommand(const char* pName)
		: IConsoleObject(pName)
	{}
	virtual bool Execute(const char** pArgs, int numArgs) = 0;

	NODISCARD virtual class IConsoleCommand* AsCommand() { return this; }
};

template<typename ...Args>
class ConsoleCommand : public IConsoleCommand
{
public:
	ConsoleCommand(const char* pName, Delegate<void, Args...>&& delegate)
		: IConsoleCommand(pName), m_Callback(std::move(delegate))
	{}

	template<typename T>
	ConsoleCommand(const char* pName, T&& callback)
		: IConsoleCommand(pName), m_Callback(Delegate<void, Args...>::CreateLambda(std::move(callback)))
	{}

	bool Execute(const char** pArgs, int numArgs) override
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

	bool Set(const char* pValue) override { return false; }

	NODISCARD int GetInt() const override { return 0; }
	NODISCARD float GetFloat() const override { return 0.0f; }
	NODISCARD bool GetBool() const override { return false; }
	NODISCARD String GetString() const override { return ""; }

private:
	template<size_t... Is>
	bool ExecuteInternal(const char** pArgs, int numArgs, std::index_sequence<Is...>)
	{
		int failIndex = -1;
		std::tuple<typename DecayNonPointer<Args>::Type...> arguments = CString::TupleFromArguments<typename DecayNonPointer<Args>::Type...>(pArgs, &failIndex);
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

class IConsoleVariable : public IConsoleObject
{
public:
	IConsoleVariable(const char* pName)
		: IConsoleObject(pName)
	{}
	NODISCARD IConsoleVariable* AsVariable() { return this; }
};

template<typename T>
class ConsoleVariable : public IConsoleVariable
{
public:
	ConsoleVariable(const char* pName, T defaultValue, Delegate<void, IConsoleObject*> onModified = {})
		: IConsoleVariable(pName), m_Value(defaultValue), m_OnModified(onModified)
	{}

	ConsoleVariable& operator=(const T& value)
	{
		m_Value = value;
		m_OnModified.ExecuteIfBound(this);
		return *this;
	}

	bool Set(const char* pValue) override
	{
		std::remove_reference_t<T> val;
		if (CString::FromString(pValue, val))
		{
			Set(val);
			E_LOG(Info, "%s: %s", GetName(), pValue);
			return true;
		}
		return false;
	}

	void Set(const T& value)
	{
		m_Value = value;
		m_OnModified.ExecuteIfBound(this);
	}

	NODISCARD int GetInt() const override
	{
		if constexpr (std::is_same_v<T, int>)
		{
			return m_Value;
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			return m_Value ? 1 : 0;
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			return static_cast<int>(m_Value);
		}
		else if constexpr (std::is_same_v<T, const char*>)
		{
			int outValue;
			gVerify(CString::FromString(m_Value, outValue), == true);
			return outValue;
		}
	}

	NODISCARD float GetFloat() const override
	{
		if constexpr (std::is_same_v<T, int>)
		{
			return static_cast<float>(m_Value);
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			return m_Value ? 1.0f : 0.0f;
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			return m_Value;
		}
		else if constexpr (std::is_same_v<T, const char*>)
		{
			int outValue;
			gVerify(CString::FromString(m_Value, outValue), == true);
			return outValue;
		}
	}

	NODISCARD bool GetBool() const override
	{
		if constexpr (std::is_same_v<T, int>)
		{
			return m_Value > 0;
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			return m_Value;
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			return m_Value > 0.0f;
		}
		else if constexpr (std::is_same_v<T, const char*>)
		{
			int outValue;
			gVerify(CString::FromString(m_Value, outValue), == true);
			return outValue;
		}
	}

	NODISCARD String GetString() const override
	{
		String output;
		if constexpr (std::is_same_v<T, int>)
		{
			CString::ToString(m_Value, &output);
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			CString::ToString(m_Value, &output);
		}
		else if constexpr (std::is_same_v<T, float>)
		{
			CString::ToString(m_Value, &output);
		}
		else if constexpr (std::is_same_v<T, const char*>)
		{
			return m_Value;
		}
		return output;
	}

	operator const T& () const { return m_Value; }

	NODISCARD T& Get() { return m_Value; }
	NODISCARD const T& Get() const { return m_Value; }

private:
	T m_Value;
	Delegate<void, IConsoleObject*> m_OnModified;
};

class ImGuiConsole
{
public:
	void Update();
	bool& IsVisible() { return m_ShowConsole; }

private:
	int InputCallback(ImGuiInputTextCallbackData* pCallbackData);

	Array<String> m_History;
	Array<const char*> m_Suggestions;
	StaticArray<char, 1024> m_Input{};
	int m_HistoryPos = -1;
	int m_SuggestionPos = -1;

	bool m_ShowConsole = false;
	bool m_FocusConsole = true;
	bool m_AutoCompleted = false;
};
