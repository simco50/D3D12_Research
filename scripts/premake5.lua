require "vstudio"

ENGINE_NAME = "D3D12"
ROOT = "../"
SOURCE_DIR = ROOT .. ENGINE_NAME .. "/"
WIN_SDK = "10.0.19041.0"

-- Address Sanitizer API

premake.api.register{
	name="enableASAN",
	scope="config",
	kind="string",
	allowed={"true", "false"}
}

premake.override(premake.vstudio.vc2010, "configurationProperties", function(base, cfg)
	local m = premake.vstudio.vc2010
	m.propertyGroup(cfg, "Configuration")
	premake.callArray(m.elements.configurationProperties, cfg)
	if cfg.enableASAN then
	   m.element("EnableASAN", nil, cfg.enableASAN)
	end
	premake.pop('</PropertyGroup>')
end)

function runtimeDependency(source, destination)
	postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries/" .. source .. "\" \"$(OutDir)" .. destination .. "/\"") }
end

workspace (ENGINE_NAME)
	basedir (ROOT)
	configurations { "Debug", "Release", "DebugASAN" }
    platforms { "x64" }
	defines { "x64" }
	language "C++"
	cppdialect "c++17"
	startproject (ENGINE_NAME)
	symbols "On"
	architecture "x64"
	characterset "MBCS"
	flags {"MultiProcessorCompile", "ShadowedVariables", "FatalWarnings"}
	rtti "Off"
	warnings "Extra"
	justmycode "Off"
	editAndContinue "Off"
	system "windows"
	conformancemode "On"
	defines { "PLATFORM_WINDOWS=1" }
	targetdir (ROOT .. "Build/$(ProjectName)_$(Platform)_$(Configuration)")
	objdir (ROOT .. "Build/Intermediate/$(ProjectName)_$(Platform)_$(Configuration)")

	--Unreferenced variable
	disablewarnings {"4100"}
	
	filter "configurations:Debug"
 		runtime "Debug"
		defines { "_DEBUG" }
		optimize ("Off")
		--inlining "Explicit"

	filter "configurations:Release"
 		runtime "Release"
		defines { "RELEASE" }
		optimize ("Full")
		flags { "NoIncrementalLink" }

	filter "configurations:DebugASAN"
 		runtime "Debug"
		defines { "_DEBUG" }
		optimize ("Off")
		flags{ "NoRuntimeChecks", "NoIncrementalLink"}
		enableASAN "true"

	filter {}

	project (ENGINE_NAME)
		location (ROOT .. ENGINE_NAME)
		pchheader ("stdafx.h")
		pchsource (ROOT .. ENGINE_NAME .. "/stdafx.cpp")

		includedirs { "$(ProjectDir)" }

		for i, dir in pairs(os.matchdirs(SOURCE_DIR .. "External/*")) do
			dirname = string.explode(dir, "/")
			dir = dirname[#dirname]
			includedirs ("$(ProjectDir)External/" .. dir)
		end

		systemversion (WIN_SDK)
		kind "WindowedApp"

		files
		{ 
			(SOURCE_DIR .. "**.h"),
			(SOURCE_DIR .. "**.hpp"),
			(SOURCE_DIR .. "**.cpp"),
			(SOURCE_DIR .. "**.inl"),
			(SOURCE_DIR .. "**.c"),
			(SOURCE_DIR .. "**.natvis"),
			(SOURCE_DIR .. "**.hlsl*"),
			(SOURCE_DIR .. "**.editorconfig"),
		}

		vpaths
		{
			{["Shaders/Include"] = (SOURCE_DIR .. "**.hlsli")},
			{["Shaders/Source"] = (SOURCE_DIR .. "**.hlsl")},
		}

		filter ("files:" .. SOURCE_DIR .. "External/**")
			flags { "NoPCH" }
			removeflags "FatalWarnings"
			warnings "Default"
		filter {}

		-- D3D12
		includedirs "$(SolutionDir)Libraries/D3D12/include"
		runtimeDependency("D3D12/bin/D3D12Core.dll", "D3D12")
		runtimeDependency("D3D12/bin/d3d12SDKLayers.dll", "D3D12")
		links {	"d3d12.lib", "dxgi" }

		-- Pix
		includedirs "$(SolutionDir)Libraries/Pix/include"
		libdirs "$(SolutionDir)Libraries/Pix/lib"
		runtimeDependency("Pix/bin/WinPixEventRuntime.dll", "")
		links { "WinPixEventRuntime" }

		-- DXC
		includedirs "$(SolutionDir)Libraries/Dxc/include"
		runtimeDependency ("Dxc/bin/dxcompiler.dll", "")
		runtimeDependency ("Dxc/bin/dxil.dll", "")

		-- Optick
		links { "OptickCore" }
		libdirs	"$(SolutionDir)Libraries/Optick/lib/"
		includedirs "$(SolutionDir)Libraries/Optick/include"
		runtimeDependency ("Optick/bin/OptickCore.dll", "")


newaction {
	trigger     = "clean",
	description = "Remove all binaries and generated files",

	execute = function()
		os.rmdir("../Build")
		os.rmdir("../ipch")
		os.rmdir("../.vs")
		os.remove("../*.sln")
		os.remove(SOURCE_DIR .. "*.vcxproj.*")
	end
}
			
--------------------------------------------------------