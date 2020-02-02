#include "stdafx.h"
#include "RenderGraph.h"
#include <fstream>

void RGGraph::DumpGraphMermaid(const char* pPath) const
{
	std::ofstream stream(pPath);

	stream << "<script src=\"https://unpkg.com/mermaid@8.4.6/dist/mermaid.min.js\"></script>";
	stream << "<script>mermaid.initialize({ startOnLoad:true });</script>";
	stream << "<div class=\"mermaid\">";

	stream << "graph LR;\n";

	stream << "classDef referencedPass fill:#fa0,stroke:#333,stroke-width:2px;\n";
	stream << "classDef unreferenced stroke:#fee,stroke-width:1px;\n";
	stream << "classDef referencedResource fill:#bef,stroke:#333,stroke-width:2px;\n";
	stream << "classDef importedResource fill:#9bd,stroke:#333,stroke-width:2px;\n";

	const char* writeLinkStyle = "stroke:#f82,stroke-width:2px;";
	const char* readLinkStyle = "stroke:#9c9,stroke-width:2px;";
	const char* aliasLinkStyle = "stroke:#e1c,stroke-width:3px;";
	int linkIndex = 0;

	//Pass declaration
	int passIndex = 0;
	for (RGPassBase* pPass : m_RenderPasses)
	{
		stream << "Pass" << pPass->m_Id;
		stream << "[";
		stream << pPass->m_Name << "<br/>";
		stream << "Refs: " << pPass->m_References << "<br/>";
		stream << "Index: " << passIndex;
		stream << "]:::";
		if (pPass->m_References == 0)
		{
			stream << "unreferenced";
		}
		else
		{
			stream << "referencedPass";
		}
		if (pPass->m_References)
		{
			++passIndex;
		}
		stream << "\n";
	}

	//Resource declaration
	for (const RGNode& node : m_ResourceNodes)
	{
		stream << "Resource" << node.pResource->m_Id << "_" << node.Version << "([";
		stream << node.pResource->m_Name << "<br/>";
		stream << "Id:" << node.pResource->m_Id << "<br/>";
		stream << "Refs: " << node.pResource->m_References;
		stream << "]):::";
		if (node.pResource->m_References == 0)
		{
			stream << "unreferenced";
		}
		else if (node.pResource->m_IsImported)
		{
			stream << "importedResource";
		}
		else
		{
			stream << "referencedResource";
		}
		stream << "\n";
	}

	//Writes
	for (RGPassBase* pPass : m_RenderPasses)
	{
		for (RGResourceHandle handle : pPass->m_Writes)
		{
			const RGNode& node = GetResourceNode(handle);
			stream << "Pass" << pPass->m_Id << " --> " << "Resource" << node.pResource->m_Id << "_" << node.Version << "\n";
			stream << "linkStyle " << linkIndex++ << " " << writeLinkStyle << "\n";
		}
	}
	stream << "\n";

	//Reads
	for (const RGNode& node : m_ResourceNodes)
	{
		for (RGPassBase* pPass : m_RenderPasses)
		{
			for (RGResourceHandle read : pPass->m_Reads)
			{
				const RGNode& readNode = GetResourceNode(read);
				if (node.Version == readNode.Version && node.pResource->m_Id == readNode.pResource->m_Id)
				{
					stream << "Resource" << node.pResource->m_Id << "_" << node.Version << " --> ";
					stream << "Pass" << pPass->m_Id << "\n";
					stream << "linkStyle " << linkIndex++ << " " << readLinkStyle << "\n";
				}
			}
		}
	}
	stream << "\n";

	//Aliases
	for (const RGResourceAlias& alias : m_Aliases)
	{
		stream << "Resource" << GetResourceNode(alias.From).pResource->m_Id << "_" << GetResourceNode(alias.From).Version << " --> ";
		stream << "Resource" << GetResourceNode(alias.To).pResource->m_Id << "_" << GetResourceNode(alias.To).Version << "\n";
		stream << "linkStyle " << linkIndex++ << " " << aliasLinkStyle << "\n";
		stream << "\n";
	}
	stream << "</div>";
}

void RGGraph::DumpGraphViz(const char* pPath) const
{
	std::ofstream stream(pPath);

	stream << "digraph RenderGraph {\n";
	stream << "rankdir = LR\n";

	//Pass declaration
	int passIndex = 0;
	for (RGPassBase* pPass : m_RenderPasses)
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
	for (const RGNode& node : m_ResourceNodes)
	{
		stream << "Resource" << node.pResource->m_Id << "_" << node.Version << " [";
		stream << "shape=rectangle, style=filled, ";
		if (node.pResource->m_References == 0)
		{
			stream << "fillcolor = azure2";
		}
		else if (node.pResource->m_IsImported)
		{
			stream << "fillcolor = lightskyblue3";
		}
		else
		{
			stream << "fillcolor = lightskyblue1";
		}
		stream << ", label = \"";
		stream << node.pResource->m_Name << "\n";
		stream << "Id:" << node.pResource->m_Id << "\n";
		stream << "Refs: " << node.pResource->m_References;
		stream << "\"";
		stream << "]\n";
	}

	//Writes
	for (RGPassBase* pPass : m_RenderPasses)
	{
		for (RGResourceHandle handle : pPass->m_Writes)
		{
			const RGNode& node = GetResourceNode(handle);
			stream << "Pass" << pPass->m_Id << " -> " << "Resource" << node.pResource->m_Id << "_" << node.Version;
			stream << " [color=chocolate1]\n";
		}
	}
	stream << "\n";

	//Reads
	for (const RGNode& node : m_ResourceNodes)
	{
		stream << "Resource" << node.pResource->m_Id << "_" << node.Version << " -> {\n";
		for (RGPassBase* pPass : m_RenderPasses)
		{
			for (RGResourceHandle read : pPass->m_Reads)
			{
				const RGNode& readNode = GetResourceNode(read);
				if (node.Version == readNode.Version && node.pResource->m_Id == readNode.pResource->m_Id)
				{
					stream << "Pass" << pPass->m_Id << "\n";
				}
			}
		}
		stream << "} " << "[color=darkseagreen]";
	}
	stream << "\n";

	//Aliases
	for (const RGResourceAlias& alias : m_Aliases)
	{
		stream << "Resource" << GetResourceNode(alias.From).pResource->m_Id << "_" << GetResourceNode(alias.From).Version << " -> ";
		stream << "Resource" << GetResourceNode(alias.To).pResource->m_Id << "_" << GetResourceNode(alias.To).Version;
		stream << " [color=magenta]\n";
	}

	stream << "}";
}
