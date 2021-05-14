#pragma once
#include "CharConv.h"

extern class CVarManager gConsoleManager;

class IConsoleObject;

class CVarManager
{
public:
	void RegisterConsoleObject(const char* pName, IConsoleObject* pObject)
	{
		m_CvarMap[pName] = pObject;
	}

	IConsoleObject* GetConsoleObject(const char* pName)
	{
		return m_CvarMap[pName];
	}

	template<typename T>
	void ForEachCvar(T&& callback) const
	{
		for (const auto& cvar : m_CvarMap)
		{
			callback(cvar.second);
		}
	}

	bool ExecuteCommand(const char* pCommand, const char* pArguments);

private:
	std::unordered_map<StringHash, IConsoleObject*> m_CvarMap;
};

class IConsoleObject
{
public:
	IConsoleObject(const char* pName)
		: m_pName(pName)
	{
		gConsoleManager.RegisterConsoleObject(pName, this);
	}
	virtual ~IConsoleObject() = default;
	virtual bool Execute(const char** pArgs, uint32 numArgs) = 0;

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

	virtual bool Execute(const char** pArgs, uint32 numArgs) override
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
	bool ExecuteInternal(const char** pArgs, uint32 numArgs, std::index_sequence<Is...>)
	{
		int failIndex = -1;
		std::tuple<DecayNonPointer<Args>::Type...> arguments = CharConv::TupleFromArguments<DecayNonPointer<Args>::Type...>(pArgs, &failIndex);
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

	virtual bool Execute(const char** pArgs, uint32 numArgs) override
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
	bool ExecuteInternal(const char** pArgs, uint32 numArgs, std::index_sequence<Is...>)
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
	}

	virtual bool Execute(const char** pArgs, uint32 numArgs) override
	{
		if (numArgs > 0)
		{
			T val;
			if (CharConv::StrConvert(pArgs[0], val))
			{
				SetValue(val);
				m_OnModified.ExecuteIfBound(this);
				return true;
			}
		}
		return false;
	}

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

	virtual bool Execute(const char** pArgs, uint32 numArgs) override
	{
		if (numArgs > 0)
		{
			T val;
			if (CharConv::StrConvert(pArgs[0], val))
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
	static void Strtrim(char* s) { char* str_end = s + strlen(s); while (str_end > s && str_end[-1] == ' ') str_end--; *str_end = 0; }

	ImGuiConsole()
	{
		m_Input[0] = '\0';
	}

	void Update(const ImVec2& position, const ImVec2& size);

private:
	int InputCallback(ImGuiInputTextCallbackData* pCallbackData)
	{
		auto BuildSuggestions = [this, pCallbackData]() {
			m_Suggestions.clear();
			if (strlen(pCallbackData->Buf) > 0)
			{
				gConsoleManager.ForEachCvar([this, pCallbackData](IConsoleObject* pObject)
					{
						if (_strnicmp(pObject->GetName(), pCallbackData->Buf, strlen(pCallbackData->Buf)) == 0)
						{
							m_Suggestions.push_back(pObject->GetName());
						}
					});
			}
		};

		switch (pCallbackData->EventFlag)
		{
		case ImGuiInputTextFlags_CallbackAlways:
		{
			if (m_AutoCompleted)
			{
				pCallbackData->CursorPos = pCallbackData->BufTextLen;
				m_AutoCompleted = false;
			}
			break;
		}
		case ImGuiInputTextFlags_CallbackEdit:
		{
			BuildSuggestions();
			break;
		}
		case ImGuiInputTextFlags_CallbackCompletion:
		{
			const char* pWordEnd = pCallbackData->Buf + pCallbackData->CursorPos;
			const char* pWordStart = pWordEnd;
			while (pWordStart > pCallbackData->Buf)
			{
				const char c = pWordStart[-1];
				if (c == ' ' || c == '\t' || c == ',' || c == ';')
					break;
				pWordStart--;
			}

			if (!m_Suggestions.empty())
			{
				m_SuggestionPos = Math::Clamp(m_SuggestionPos, 0, (int)m_Suggestions.size() - 1);

				pCallbackData->DeleteChars((int)(pWordStart - pCallbackData->Buf), (int)(pWordEnd - pWordStart));
				pCallbackData->InsertChars(pCallbackData->CursorPos, m_Suggestions[m_SuggestionPos]);
				pCallbackData->InsertChars(pCallbackData->CursorPos, " ");
				BuildSuggestions();
			}
		}
		break;
		case ImGuiInputTextFlags_CallbackHistory:
		{
			if (m_Suggestions.empty())
			{
				const int prev_history_pos = m_HistoryPos;
				if (pCallbackData->EventKey == ImGuiKey_UpArrow)
				{
					if (m_HistoryPos == -1)
						m_HistoryPos = (int)m_History.size() - 1;
					else if (m_HistoryPos > 0)
						m_HistoryPos--;
				}
				else if (pCallbackData->EventKey == ImGuiKey_DownArrow)
				{
					if (m_HistoryPos != -1)
						if (++m_HistoryPos >= m_History.size())
							m_HistoryPos = -1;
				}

				if (prev_history_pos != m_HistoryPos)
				{
					const char* pHistoryStr = (m_HistoryPos >= 0) ? m_History[m_HistoryPos].c_str() : "";
					pCallbackData->DeleteChars(0, pCallbackData->BufTextLen);
					pCallbackData->InsertChars(0, pHistoryStr);
				}
			}
			else
			{
				const int prevSuggestionPos = m_SuggestionPos;
				if (pCallbackData->EventKey == ImGuiKey_UpArrow)
				{
					if (m_SuggestionPos == -1)
						m_SuggestionPos = (int)m_Suggestions.size() - 1;
					else if (m_SuggestionPos > 0)
						m_SuggestionPos--;
				}
				else if (pCallbackData->EventKey == ImGuiKey_DownArrow)
				{
					if (m_SuggestionPos != -1)
						if (++m_SuggestionPos >= m_Suggestions.size())
							m_SuggestionPos = -1;
				}

				if (prevSuggestionPos != m_SuggestionPos)
				{
					const char* pSuggestionStr = (m_SuggestionPos >= 0) ? m_Suggestions[m_SuggestionPos] : "";
					pCallbackData->DeleteChars(0, pCallbackData->BufTextLen);
					pCallbackData->InsertChars(0, pSuggestionStr);
				}
			}
		}
		break;
		case ImGuiInputTextFlags_CallbackCharFilter:
		{
			if (pCallbackData->EventChar == '`')
			{
				return 1;
			}
		}
		break;
		}
		return 0;
	}

	std::vector<std::string> m_History;
	std::vector<const char*> m_Suggestions;
	int m_HistoryPos = -1;
	int m_SuggestionPos = -1;

	bool m_ShowConsole = false;
	bool m_FocusConsole = true;
	bool m_AutoCompleted = false;
	char m_Input[1024];
};

