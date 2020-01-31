#include "stdafx.h"
#include "RenderGraph.h"
#include <fstream>

namespace RG
{
	void RenderGraph::DumpGraphViz(const char* pPath) const
	{
		std::ofstream stream(pPath);

		stream << "digraph RenderGraph {\n";
		stream << "rankdir = LR\n";

		//Pass declaration
		int passIndex = 0;
		for (RenderPassBase* pPass : m_RenderPasses)
		{
			stream << "Pass" << pPass->m_Id << " [";
			stream << "shape=rectangle, style=\"filled, rounded\", margin=0.2, ";
			if (pPass->m_References == 0)
			{
				stream << "fillcolor = mistyrose";
			}
			else
			{
				stream << "fillcolor = orange";
			}
			stream << ", label = \"";
			stream << pPass->m_Name << "\n";
			stream << "Refs: " << pPass->m_References << "\n";
			stream << "Index: " << passIndex;
			stream << "\"";
			stream << "]\n";
			if (pPass->m_References)
			{
				++passIndex;
			}
		}

		//Resource declaration
		for (const ResourceNode& node : m_ResourceNodes)
		{
			stream << "Resource" << node.m_pResource->m_Id << "_" << node.m_Version << " [";
			stream << "shape=rectangle, style=filled, ";
			if (node.m_pResource->m_References == 0)
			{
				stream << "fillcolor = azure2";
			}
			else if (node.m_pResource->m_IsImported)
			{
				stream << "fillcolor = lightskyblue3";
			}
			else
			{
				stream << "fillcolor = lightskyblue1";
			}
			stream << ", label = \"";
			stream << node.m_pResource->m_Name << "\n";
			stream << "Id:" << node.m_pResource->m_Id << "\n";
			stream << "Refs: " << node.m_pResource->m_References;
			stream << "\"";
			stream << "]\n";
		}

		//Writes
		for (RenderPassBase* pPass : m_RenderPasses)
		{
			for (ResourceHandle handle : pPass->m_Writes)
			{
				const ResourceNode& node = GetResourceNode(handle);
				stream << "Pass" << pPass->m_Id << " -> " << "Resource" << node.m_pResource->m_Id << "_" << node.m_Version;
				stream << " [color=chocolate1]\n";
			}
		}
		stream << "\n";

		//Reads
		for (const ResourceNode& node : m_ResourceNodes)
		{
			stream << "Resource" << node.m_pResource->m_Id << "_" << node.m_Version << " -> {\n";
			for (RenderPassBase* pPass : m_RenderPasses)
			{
				for (ResourceHandle read : pPass->m_Reads)
				{
					const ResourceNode& readNode = GetResourceNode(read);
					if (node.m_Version == readNode.m_Version && node.m_pResource->m_Id == readNode.m_pResource->m_Id)
					{
						stream << "Pass" << pPass->m_Id << "\n";
					}
				}
			}
			stream << "} " << "[color=darkseagreen]";
		}
		stream << "\n";

		//Aliases
		for (const ResourceAlias& alias : m_Aliases)
		{
			stream << "Resource" << GetResourceNode(alias.From).m_pResource->m_Id << "_" << GetResourceNode(alias.From).m_Version << " -> ";
			stream << "Resource" << GetResourceNode(alias.To).m_pResource->m_Id << "_" << GetResourceNode(alias.To).m_Version;
			stream << " [color=magenta]\n";
		}

		stream << "}";
	}
}