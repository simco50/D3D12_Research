#include "stdafx.h"
#include "EventManager.h"

std::unordered_map<StringHash, std::unique_ptr<MulticastDelegateBase>> EventManager::m_DelegateMap;
