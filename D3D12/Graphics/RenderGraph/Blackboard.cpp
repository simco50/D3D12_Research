#include "stdafx.h"
#include "Blackboard.h"

RGBlackboard::RGBlackboard()
{

}

RGBlackboard::~RGBlackboard()
{

}

RGBlackboard& RGBlackboard::Branch()
{
	m_Children.emplace_back(std::make_unique<RGBlackboard>());
	RGBlackboard& b = *m_Children.back();
	b.m_pParent = this;
	return b;
}

void* RGBlackboard::GetData(const std::string& name)
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
