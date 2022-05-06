#include "stdafx.h"
#include "RenderGraph.h"
#include <sstream>

template<typename T>
std::string BitmaskToString(T mask, const char* (*pValueToString)(T))
{
	std::string outString;
	uint32 value = (uint32)mask;

	if (value == 0)
	{
		const char* pStr = pValueToString((T)0);
		return pStr ? pStr : "NONE";
	}

	uint32 bitIndex = 0;
	uint32 valueIndex = 0;
	while (value > 0)
	{
		if (value & 1)
		{
			const char* pStr = pValueToString((T)(1 << bitIndex));
			if (pStr)
			{
				if (valueIndex > 0)
					outString += '/';
				outString += pStr;
				valueIndex++;
			}
		}
		bitIndex++;
		value >>= 1;
	}
	return outString;
}

std::string PassFlagToString(RGPassFlag flags)
{
	return BitmaskToString<RGPassFlag>(flags,
		[](RGPassFlag flag) -> const char*
		{
			switch (flag)
			{
			case RGPassFlag::None: return "None";
			case RGPassFlag::Compute: return "Compute";
			case RGPassFlag::Raster: return "Raster";
			case RGPassFlag::Copy: return "Copy";
			case RGPassFlag::NeverCull: return "Never Cull";
			default: return nullptr;
			}
		});
}

void RGGraph::DumpGraph(const char* pPath) const
{
	std::stringstream stream;

	constexpr const char* pMermaidTemplate = R"(
			<!DOCTYPE html>
				<html lang="en">
				<head>
					<meta charset="utf-8">
					<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.1.1/css/all.min.css"
						integrity="sha512-KfkfwYDsLkIlwQp6LFnl8zNdLGxu9YAA1QvwINks4PhcElQSvqcyVLLD9aMhXd13uQjoXtEKNosOWaZqXgel0g=="
						crossorigin="anonymous" referrerpolicy="no-referrer" />
				</head>
				<body>
					<script src="https://cdn.jsdelivr.net/npm/mermaid/dist/mermaid.min.js"></script>
					<script>
						mermaid.initialize({ startOnLoad: true, flowchart: { useMaxWidth: false, htmlLabels: true }});
					</script>
					<div class="mermaid">
						%s
					</div>
				</body>
			</html>
		)";

	stream << "graph TD;\n";

	stream << "classDef referencedPass fill:#fa0,stroke:#333,stroke-width:4px;\n";
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
		if (EnumHasAnyFlags(pPass->Flags, RGPassFlag::Invisible))
			continue;

		stream << "Pass" << pPass->ID;
		stream << "[";
		stream << pPass->Name << "<br/>";
		stream << "Flags: " << PassFlagToString(pPass->Flags) << "<br/>";
		stream << "Refs: " << pPass->References << "<br/>";
		stream << "Index: " << passIndex;
		stream << "]:::";

		stream << (pPass->References ? "referencedPass" : "unreferenced");

		if (pPass->References)
			++passIndex;

		stream << "\n";
	}

	//Resource declaration
	for (const RGNode& node : m_ResourceNodes)
	{
		if (node.Version == 0 && node.Reads == 0)
		{
			continue;
		}

		stream << "Resource" << node.pResource->Id << "_" << node.Version;
		stream << (node.pResource->IsImported ? "[(" : "([");
		stream << node.pResource->Name << "<br/>";
		//stream << "Id:" << node.pResource->Id << "<br/>";
		//stream << "Version: " << node.Version << "<br/>";

		if (node.pResource->Type == RGResourceType::Texture)
		{
			const TextureDesc& desc = node.pResource->TextureDesc;
			stream << desc.Width << "x" << desc.Height << "x" << desc.DepthOrArraySize << "<br/>";
			stream << D3D::FormatToString(desc.Format) << "<br/>";
			stream << "Mips: " << desc.Mips << "<br/>";
			stream << "Size: " << Math::PrettyPrintDataSize(D3D::GetFormatRowDataSize(desc.Format, desc.Width) * desc.Height * desc.DepthOrArraySize) << "</br>";
		}
		else if (node.pResource->Type == RGResourceType::Buffer)
		{
			const BufferDesc& desc = node.pResource->BufferDesc;
			stream << Math::PrettyPrintDataSize(desc.Size) << "<br/>";
			stream << D3D::FormatToString(desc.Format) << "<br/>";
		}

		stream << "Refs: " << node.pResource->References << "<br/>";

		stream << (node.pResource->IsImported ? ")]" : "])");
		if (node.pResource->IsImported)
		{
			stream << ":::importedResource";
		}
		else if (node.pResource->References == 0)
		{
			stream << ":::unreferenced";
		}
		else
		{
			stream << ":::referencedResource";
		}
		stream << "\n";
	}

	//Writes
	for (RGPass* pPass : m_RenderPasses)
	{
		if (pPass->Writes.size() > 0)
		{
			stream << "Pass" << pPass->ID << " -- Write --> ";
			for (uint32 i = 0; i < pPass->Writes.size(); ++i)
			{
				const RGNode& node = GetResourceNode(pPass->Writes[i]);
				if (i != 0)
					stream << " & ";
				stream << "Resource" << node.pResource->Id << "_" << node.Version;
			}
			stream << "\nlinkStyle " << linkIndex++ << " " << writeLinkStyle << "\n";
		}
	}
	stream << "\n";

	//Reads
	for (RGPass* pPass : m_RenderPasses)
	{
		if (pPass->Reads.size() > 0)
		{
			for (uint32 i = 0; i < pPass->Reads.size(); ++i)
			{
				const RGNode& readNode = GetResourceNode(pPass->Reads[i]);
				if (i != 0)
					stream << " & ";
				stream << "Resource" << readNode.pResource->Id << "_" << readNode.Version;
			}
			stream << " -- Read --> Pass" << pPass->ID << "\n";
			stream << "linkStyle " << linkIndex++ << " " << readLinkStyle << "\n";
		}
	}
	stream << "\n";

	//Aliases
	for (const RGResourceAlias& alias : m_Aliases)
	{
		stream << "Resource" << GetResourceNode(alias.From).pResource->Id << "_" << GetResourceNode(alias.From).Version << " --> ";
		stream << "Resource" << GetResourceNode(alias.To).pResource->Id << "_" << GetResourceNode(alias.To).Version << "\n";
		stream << "linkStyle " << linkIndex++ << " " << aliasLinkStyle << "\n";
		stream << "\n";
	}

	std::string output = Sprintf(pMermaidTemplate, stream.str().c_str());
	Paths::CreateDirectoryTree(pPath);
	FILE* pFile = nullptr;
	fopen_s(&pFile, pPath, "w");
	if (pFile)
	{
		fwrite(output.c_str(), sizeof(char), output.length(), pFile);
		fclose(pFile);
	}
}
