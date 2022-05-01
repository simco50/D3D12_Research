#include "stdafx.h"
#include "RenderGraph.h"
#include <fstream>

void RGGraph::DumpGraph(const char* pPath) const
{
	Paths::CreateDirectoryTree(pPath);
	std::ofstream stream(pPath);

	stream << "<script src=\"https://unpkg.com/mermaid@9.0.1/dist/mermaid.min.js\"></script>\n";
	stream << "<script>mermaid.initialize({ startOnLoad:true });</script>\n";
	stream << "<div class=\"mermaid\">\n";

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
	for (RGPass* pPass : m_RenderPasses)
	{
		stream << "Pass" << pPass->ID;
		stream << "[";
		stream << pPass->pName << "<br/>";
		stream << "Refs: " << pPass->References << "<br/>";
		stream << "Index: " << passIndex;
		stream << "]:::";
		if (pPass->References == 0)
		{
			stream << "unreferenced";
		}
		else
		{
			stream << "referencedPass";
		}
		if (pPass->References)
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
	for (RGPass* pPass : m_RenderPasses)
	{
		for (RGResourceHandle handle : pPass->Writes)
		{
			const RGNode& node = GetResourceNode(handle);
			stream << "Pass" << pPass->ID << " --> " << "Resource" << node.pResource->m_Id << "_" << node.Version << "\n";
			stream << "linkStyle " << linkIndex++ << " " << writeLinkStyle << "\n";
		}
	}
	stream << "\n";

	//Reads
	for (const RGNode& node : m_ResourceNodes)
	{
		for (RGPass* pPass : m_RenderPasses)
		{
			for (RGResourceHandle read : pPass->Reads)
			{
				const RGNode& readNode = GetResourceNode(read);
				if (node.Version == readNode.Version && node.pResource->m_Id == readNode.pResource->m_Id)
				{
					stream << "Resource" << node.pResource->m_Id << "_" << node.Version << " --> ";
					stream << "Pass" << pPass->ID << "\n";
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
