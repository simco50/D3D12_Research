#include "stdafx.h"
#include "Blackboard.h"

namespace RG
{
	Blackboard::Blackboard()
	{

	}

	Blackboard::~Blackboard()
	{

	}

	Blackboard& Blackboard::Branch()
	{
		m_Children.emplace_back(std::make_unique<Blackboard>());
		Blackboard& b = *m_Children.back();
		b.m_pParent = this;
		return b;
	}

	void* Blackboard::GetData(const std::string& name)
	{
		auto it = m_DataMap.find(name);
		if (it != m_DataMap.end())
		{
			return it->second;
		}
		if (m_pParent)
		{
			return m_pParent->GetData(name);
		}
		return nullptr;
	}
}