#include "stdafx.h"
#include "ConsoleVariables.h"
#include "Core/Input.h"

CVarManager gConsoleManager;

CVarManager& CVarManager::Get()
{
	return gConsoleManager;
}

void CVarManager::RegisterConsoleObject(const char* pName, IConsoleObject* pObject)
{
	char lowerName[256];
	CharConv::ToLower(pName, lowerName);
	m_Map[lowerName] = pObject;
	m_Objects.push_back(pObject);
	std::sort(m_Objects.begin(), m_Objects.end(), [](IConsoleObject* pA, IConsoleObject* pB) { return strcmp(pA->GetName(), pB->GetName()) < 0; });
}

bool CVarManager::ExecuteCommand(const char* pCommand, const char* pArguments)
{
	const char* argList[16];
	char buffer[1024];
	int numArgs = CharConv::SplitString(pArguments, buffer, &argList[0], 16, true, ' ');
	auto it = m_Map.find(pCommand);
	if (it != m_Map.end())
	{
		return it->second->Execute(argList, numArgs);
	}
	else
	{
		E_LOG(Warning, "Unknown command '%s'", pCommand);
		return false;
	}
}

void ImGuiConsole::Update(const ImVec2& position, const ImVec2& size)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(position, 0, ImVec2(0, 1));
	ImGui::SetNextWindowSize(size);
	ImGui::SetNextWindowCollapsed(!m_ShowConsole);

	bool show = ImGui::Begin("Output Log", &m_ShowConsole, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
	if (show)
	{
		for (const Console::LogEntry& entry : Console::GetHistory())
		{
			switch (entry.Type)
			{
			case LogType::VeryVerbose:
			case LogType::Verbose:
			case LogType::Info:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
				ImGui::TextWrapped("[Info] %s", entry.Message.c_str());
				break;
			case LogType::Warning:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
				ImGui::TextWrapped("[Warning] %s", entry.Message.c_str());
				break;
			case LogType::Error:
			case LogType::FatalError:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
				ImGui::TextWrapped("[Error] %s", entry.Message.c_str());
				break;
			}
			ImGui::PopStyleColor();
		}

		int inputFlags =
			ImGuiInputTextFlags_EnterReturnsTrue |
			ImGuiInputTextFlags_CallbackHistory |
			ImGuiInputTextFlags_CallbackCompletion |
			ImGuiInputTextFlags_CallbackCharFilter |
			ImGuiInputTextFlags_CallbackEdit |
			ImGuiInputTextFlags_CallbackAlways;

		auto inputCallback = [](ImGuiInputTextCallbackData* pData)
		{
			ImGuiConsole* pConsole = (ImGuiConsole*)pData->UserData;
			return pConsole->InputCallback(pData);
		};

		ImGui::PushItemWidth(size.x);
		if (ImGui::InputText("", m_Input.data(), (int)m_Input.size(), inputFlags, inputCallback, this))
		{
			char lowerName[256];
			CharConv::ToLower(m_Input.data(), lowerName);
			if (*lowerName != '\0')
			{
				char* pArgs = strchr(lowerName, ' ');
				if (pArgs)
				{
					*pArgs++ = '\0';
				}
				CVarManager::Get().ExecuteCommand(lowerName, pArgs ? pArgs : "");
				m_Suggestions.clear();
				m_History.push_back(m_Input.data());
				m_HistoryPos = -1;
				m_SuggestionPos = -1;
				m_Input[0] = '\0';
			}
		}
		if (m_FocusConsole)
		{
			m_FocusConsole = false;
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::PopItemWidth();

		if (!m_Suggestions.empty())
		{
			ImVec2 pos = ImGui::GetCursorScreenPos();
			ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y - ImGui::GetFontSize() * 2), 0, ImVec2(0, 1));
			if (ImGui::Begin("Suggestions", 0, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize))
			{
				for (uint32 i = 0; i < (uint32)m_Suggestions.size(); ++i)
				{
					if (ImGui::Selectable(m_Suggestions[i], i == (uint32)m_SuggestionPos))
					{
						m_SuggestionPos = (int)i;
						strncpy_s(m_Input.data(), (int)m_Input.size(), m_Suggestions[i], strlen(m_Suggestions[i]));
						m_Suggestions.clear();
						m_AutoCompleted = true;
						ImGui::SetKeyboardFocusHere();
						break;
					}
				}
				ImGui::End();
			}
		}

		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		{
			ImGui::SetScrollHereY(1.0f);
		}

	}
	ImGui::End();
	ImGui::PopStyleVar();

	if (Input::Instance().IsKeyPressed(VK_OEM_3))
	{
		m_ShowConsole = !m_ShowConsole;
		m_FocusConsole = m_ShowConsole;
	}
}

int ImGuiConsole::InputCallback(ImGuiInputTextCallbackData* pCallbackData)
{
	auto BuildSuggestions = [this, pCallbackData]() {
		m_Suggestions.clear();
		if (strlen(pCallbackData->Buf) > 0)
		{
			CVarManager::Get().ForEachCvar([this, pCallbackData](IConsoleObject* pObject)
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
					if (++m_HistoryPos >= (int)m_History.size())
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
					if (++m_SuggestionPos >= (int)m_Suggestions.size())
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
