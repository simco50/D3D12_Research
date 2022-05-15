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

	stream << "classDef neverCullPass fill:#ff5e00,stroke:#333,stroke-width:4px;\n";
	stream << "classDef referencedPass fill:#fa0,stroke:#333,stroke-width:4px;\n";
	stream << "classDef unreferenced stroke:#fee,stroke-width:1px;\n";
	stream << "classDef referencedResource fill:#bef,stroke:#333,stroke-width:2px;\n";
	stream << "classDef importedResource fill:#9bd,stroke:#333,stroke-width:2px;\n";

	const char* writeLinkStyle = "stroke:#f82,stroke-width:2px;";
	const char* readLinkStyle = "stroke:#9c9,stroke-width:2px;";
	int linkIndex = 0;

	constexpr D3D12_RESOURCE_STATES WriteStates =
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
		D3D12_RESOURCE_STATE_RENDER_TARGET |
		D3D12_RESOURCE_STATE_DEPTH_WRITE |
		D3D12_RESOURCE_STATE_COPY_DEST |
		D3D12_RESOURCE_STATE_RESOLVE_DEST |
		D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE |
		D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE |
		D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE;

	std::unordered_map<RGResource*, uint32> resourceVersions;

	//Pass declaration
	int passIndex = 0;
	for (RGPass* pPass : m_RenderPasses)
	{
		if (EnumHasAnyFlags(pPass->Flags, RGPassFlag::Invisible))
			continue;

		stream << "Pass" << pPass->ID;
		stream << "[";
		stream << "\"" << pPass->Name << "\"<br/>";
		stream << "Flags: " << PassFlagToString(pPass->Flags) << "<br/>";
		stream << "Index: " << passIndex++ << "<br/>";
		stream << "Culled: " << (pPass->IsCulled ? "Yes" : "No") << "<br/>";
		stream << "]:::";

		if (EnumHasAnyFlags(pPass->Flags, RGPassFlag::NeverCull))
		{
			stream << "neverCullPass";
		}
		else
		{
			stream << (pPass->IsCulled ? "unreferenced" : "referencedPass");
		}

		stream << "\n";

		auto PrintResource = [&](RGResource* pResource, uint32 version) {
			stream << "Resource" << pResource->ID << "_" << version;
			stream << (pResource->IsImported ? "[(" : "([");
			stream << "\"" << pResource->Name << "\"<br/>";

			if (pResource->Type == RGResourceType::Texture)
			{
				const TextureDesc& desc = static_cast<RGTexture*>(pResource)->Desc;
				stream << "Res: " << desc.Width << "x" << desc.Height << "x" << desc.DepthOrArraySize << "<br/>";
				stream << "Fmt: " << D3D::FormatToString(desc.Format) << "<br/>";
				stream << "Mips: " << desc.Mips << "<br/>";
				stream << "Size: " << Math::PrettyPrintDataSize(D3D::GetFormatRowDataSize(desc.Format, desc.Width) * desc.Height * desc.DepthOrArraySize) << "</br>";
			}
			else if (pResource->Type == RGResourceType::Buffer)
			{
				const BufferDesc& desc = static_cast<RGBuffer*>(pResource)->Desc;
				stream << "Stride: " << desc.ElementSize << "<br/>";
				stream << "Fmt: " << D3D::FormatToString(desc.Format) << "<br/>";
				stream << "Size: " << Math::PrettyPrintDataSize(desc.Size) << "<br/>";
			}

			stream << (pResource->IsImported ? ")]" : "])");
			if (pResource->IsImported)
			{
				stream << ":::importedResource";
			}
			else
			{
				stream << ":::referencedResource";
			}
			stream << "\n";
		};


		for (RGPass::ResourceAccess& access : pPass->Accesses)
		{
			RGResource* pResource = access.pResource;
			uint32 resourceVersion = 0;
			auto it = resourceVersions.find(pResource);
			if (it == resourceVersions.end())
			{
				resourceVersions[pResource] = resourceVersion;

				if(pResource->IsImported)
					PrintResource(pResource, resourceVersion);

			}
			resourceVersion = resourceVersions[pResource];

			if (resourceVersion > 0 || pResource->IsImported)
			{
				stream << "Resource" << pResource->ID << "_" << resourceVersion << " -- " << D3D::ResourceStateToString(access.Access) << " --> Pass" << pPass->ID << "\n";
				stream << "linkStyle " << linkIndex++ << " " << readLinkStyle << "\n";
			}

			if (EnumHasAnyFlags(access.Access, WriteStates))
			{
				++resourceVersions[pResource];
				resourceVersion++;
				PrintResource(pResource, resourceVersion);

				stream << "Pass" << pPass->ID << " -- " << D3D::ResourceStateToString(access.Access) << " --> " << "Resource" << pResource->ID << "_" << resourceVersion;
				stream << "\nlinkStyle " << linkIndex++ << " " << writeLinkStyle << "\n";
			}
		}
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
