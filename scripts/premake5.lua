require "uwp"

ENGINE_NAME = "D3D12"
ROOT = "../"
SOURCE_DIR = ROOT .. ENGINE_NAME .. "/"
WIN_SDK = "10.0.19041.0"
WITH_UWP = _OPTIONS["uwp"]

workspace (ENGINE_NAME)
	basedir (ROOT)
	configurations { "Debug", "Release" }
    platforms { "x64" }
	defines { "x64" }
	language "C++"
	cppdialect "c++17"
	startproject (ENGINE_NAME)
	symbols "On"
	architecture "x64"
	kind "WindowedApp"
	characterset "MBCS"
	flags {"MultiProcessorCompile", "ShadowedVariables", "FatalWarnings"}
	rtti "Off"
	warnings "Extra"
	justmycode "Off"
	editAndContinue "Off"

	--Unreferenced variable
	disablewarnings {"4100"}
	
	filter "configurations:Debug"
		defines { "_DEBUG" }
		optimize ("Off")
		inlining "Explicit"

	filter "configurations:Release"
		defines { "RELEASE" }
		optimize ("Full")
		flags { "NoIncrementalLink" }

	filter {}

	project (ENGINE_NAME)
		location (ROOT .. ENGINE_NAME)
		targetdir (ROOT .. "Build/$(ProjectName)_$(Platform)_$(Configuration)")
		objdir (ROOT .. "Build/Intermediate/$(ProjectName)_$(Platform)_$(Configuration)")

		pchheader ("stdafx.h")
		pchsource (ROOT .. ENGINE_NAME .. "/stdafx.cpp")
		includedirs { "$(ProjectDir)", "$(ProjectDir)External/" }

		if WITH_UWP then 
			system "uwp"
			defines { "PLATFORM_UWP=1" }
			consumewinrtextension "false"
			systemversion (WIN_SDK)
			defaultlanguage "en-GB"
			certificatefile "D3D12_TemporaryKey.pfx"
			generatewinmd "false"

			filter ("files:" ..(SOURCE_DIR .. "Resources/**"))
				deploy "true"
			filter ("files:../Libraries/**.dll")
				deploy "true"
			filter {}

			files
			{ 
				(SOURCE_DIR .. "**.appxmanifest"),
				(SOURCE_DIR .. "Resources/**"),
				(SOURCE_DIR .. "Assets/**"),
				("../Libraries/**.dll")
			}
		else
			system "windows"
			conformancemode "On"
			defines { "PLATFORM_WINDOWS=1" }
			systemversion (WIN_SDK)
		end

		---- File setup ----
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
		includedirs (ROOT .. "Libraries/D3D12/include")
		postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries\\D3D12\\bin\\D3D12Core.dll\" \"$(OutDir)\\D3D12\\\"") }
		postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries\\D3D12\\bin\\d3d12SDKLayers.dll\" \"$(OutDir)\\D3D12\\\"") }

		-- Pix
		includedirs (ROOT .. "Libraries/Pix/include")
		libdirs (ROOT .. "Libraries/Pix/lib")
		postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries\\Pix\\bin\\WinPixEventRuntime.dll\" \"$(OutDir)\"") }
		links { "WinPixEventRuntime" }

		-- D3D12
		links {	"d3d12.lib", "dxgi", "d3dcompiler", "dxguid" }

		-- Pix
		includedirs (ROOT .. "Libraries/Assimp/include")
		libdirs	(ROOT .. "Libraries/Assimp/lib/x64")
		postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries\\Assimp\\bin\\x64\\assimp-vc142-mt.dll\" \"$(OutDir)\"") }
		links { "assimp-vc142-mt" }

		-- DXC
		links { "dxcompiler" }
		libdirs	(ROOT .. "Libraries/Dxc/lib/")
		includedirs (ROOT .. "Libraries/Dxc/include")
		postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries\\Dxc\\bin\\dxcompiler.dll\" \"$(OutDir)\"") }
		postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries\\Dxc\\bin\\dxil.dll\" \"$(OutDir)\"") }

		-- Optick
		links { "OptickCore" }
		libdirs	(ROOT .. "Libraries/Optick/lib/")
		includedirs (ROOT .. "Libraries/Optick/include")
		postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries\\Optick\\bin\\OptickCore.dll\" \"$(OutDir)\"") }


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

newoption {
	trigger     = "uwp",
	description = "Generates a UWP solution"
	}
			
--------------------------------------------------------