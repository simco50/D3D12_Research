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

void* RGBlackboard::GetData(const char* name)
{
	StringHash h(name);
	auto it = m_DataMap.find(h);
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

void RGBlackboard::Merge(const RGBlackboard& other, bool overrideExisting)
{
	for (auto& element : other.m_DataMap)
	{
		if (overrideExisting || m_DataMap.find(element.first) == m_DataMap.end())
		{
			m_DataMap.insert(element);
		}
	}
}
