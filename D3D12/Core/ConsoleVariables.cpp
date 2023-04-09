#include "stdafx.h"
#include "ConsoleVariables.h"
#include "Core/Input.h"
#include "Paths.h"
#include "imgui_internal.h"

static std::unordered_map<StringHash, IConsoleObject*> gCvarMap;
static std::vector<IConsoleObject*> gConsoleObjects;

void ConsoleManager::Initialize()
{
	std::ifstream fs(Sprintf("%sConsoleVariables.ini", Paths::SavedDir().c_str()));
	std::string line;
	while (getline(fs, line))
	{
		Execute(line.c_str());
	}
}

void ConsoleManager::RegisterConsoleObject(const char* pName, IConsoleObject* pObject)
{
	char lowerName[256];
	CString::ToLower(pName, lowerName);
	if (gCvarMap.find(lowerName) == gCvarMap.end())
	{
		gCvarMap[lowerName] = pObject;
		gConsoleObjects.push_back(pObject);
		std::sort(gConsoleObjects.begin(), gConsoleObjects.end(), [](IConsoleObject* pA, IConsoleObject* pB) { return strcmp(pA->GetName(), pB->GetName()) < 0; });
	}
}

bool ConsoleManager::Execute(const char* pCommand)
{
	char cmdLower[1024];
	strcpy_s(cmdLower, pCommand);
	CString::TrimSpaces(cmdLower);

	const char* argList[16];
	char buffer[1024];
	int numArgs = CString::SplitString(cmdLower, buffer, &argList[0], ARRAYSIZE(argList), true, ' ');
	if (numArgs > 0)
	{
		char cmd[128];
		CString::ToLower(argList[0], cmd);

		auto it = gCvarMap.find(cmd);
		if (it != gCvarMap.end())
		{
			if (IConsoleVariable* pVariable = it->second->AsVariable())
			{
				pVariable->Set(argList[1]);
			}
			else if (IConsoleCommand* pCmd = it->second->AsCommand())
			{
				pCmd->Execute(argList + 1, numArgs - 1);
			}
		}
		else
		{
			E_LOG(Warning, "Unknown command '%s'", pCommand);
			return false;
		}
	}
	return false;
}

IConsoleObject* ConsoleManager::FindConsoleObject(const char* pName)
{
	char lowerName[256];
	CString::ToLower(pName, lowerName);
	auto it = gCvarMap.find(lowerName);
	return it != gCvarMap.end() ? it->second : nullptr;
}

const std::vector<IConsoleObject*>& ConsoleManager::GetObjects()
{
	return gConsoleObjects;
}

void ImGuiConsole::Update()
{
	if (Input::Instance().IsKeyPressed(VK_OEM_3))
	{
		m_ShowConsole = !m_ShowConsole;
		m_FocusConsole = m_ShowConsole;
	}

	if (m_ShowConsole)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);

		ImVec2 viewSize = ImGui::GetIO().DisplaySize;
		ImVec2 widgetSize = ImVec2(600, 300);

		ImGui::SetNextWindowPos(ImVec2(viewSize.x / 2, viewSize.y), 0, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowSize(widgetSize);

		uint32 windowFlags =
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoDocking;

		if (ImGui::Begin("Output Log", &m_ShowConsole, windowFlags))
		{
			if (ImGui::BeginChild("History", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing())))
			{
				for (const Console::LogEntry& entry : Console::GetHistory())
				{
					switch (entry.Type)
					{
					case LogType::VeryVerbose:
					case LogType::Verbose:
					case LogType::Info:
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
						ImGui::TextWrapped("[Info] %s", entry.pMessage);
						break;
					case LogType::Warning:
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
						ImGui::TextWrapped("[Warning] %s", entry.pMessage);
						break;
					case LogType::Error:
					case LogType::FatalError:
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
						ImGui::TextWrapped("[Error] %s", entry.pMessage);
						break;
					}
					ImGui::PopStyleColor();
				}
				ImGui::SetScrollHereY();
			}
			ImGui::EndChild();

			if (ImGui::BeginChild("Command"))
			{
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

				ImGui::PushItemWidth(widgetSize.x);
				if (ImGui::InputText("##ConsoleInput", m_Input.data(), (int)m_Input.size(), inputFlags, inputCallback, this))
				{
					if (m_Input[0] != '\0')
					{
						ConsoleManager::Execute(m_Input.data());
						m_Suggestions.clear();
						m_History.push_back(m_Input.data());
						m_HistoryPos = -1;
						m_SuggestionPos = -1;
						m_Input[0] = '\0';
						m_FocusConsole = false;
					}
				}
				if (m_FocusConsole)
				{
					m_FocusConsole = false;
					ImGui::SetKeyboardFocusHere(-1);
				}

				ImGui::PopItemWidth();

				if (!m_Suggestions.empty())
				{
					ImVec2 pos = ImGui::GetCursorScreenPos();

					ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y - ImGui::GetTextLineHeightWithSpacing()), 0, ImVec2(0, 1));
					ImGui::SetNextWindowSize(ImVec2(widgetSize.x / 2, 0));
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
								m_FocusConsole = true;
								break;
							}
						}
						ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
						ImGui::End();
					}
				}
			}
			ImGui::EndChild();
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}
}

int ImGuiConsole::InputCallback(ImGuiInputTextCallbackData* pCallbackData)
{
	auto BuildSuggestions = [this, pCallbackData]() {
		m_Suggestions.clear();
		if (strlen(pCallbackData->Buf) > 0)
		{
			ConsoleManager::ForEachCvar([this, pCallbackData](IConsoleObject* pObject)
				{
					if (_strnicmp(pObject->GetName(), pCallbackData->Buf, strlen(pCallbackData->Buf)) == 0 && m_Suggestions.size() < 10)
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
			m_SuggestionPos = Math::Clamp(m_SuggestionPos, 0, m_Suggestions.size() - 1);

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
