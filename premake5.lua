require "vstudio"

PROJECT_NAME = "D3D12"
SOURCE_DIR = "Source/"
RESOURCE_DIR = "Resources/"
THIRD_PARTY_DIR = "ThirdParty/"
TARGET_DIR = "Build/"

WIN_SDK = "latest"

function runtimeDependency(source, destination)
	postbuildcommands { ("{COPY} \"$(SolutionDir)ThirdParty/" .. source .. "\" \"$(OutDir)" .. destination .. "/\"") }
end

function compileThirdPartyLibrary(name)
	includedirs ("$(SolutionDir)ThirdParty/" .. name)
	files {
		(THIRD_PARTY_DIR.. name .. "/*.cpp"),
		(THIRD_PARTY_DIR .. name .. "/*.h"),
		(THIRD_PARTY_DIR .. name .. "/*.hpp"),
		(THIRD_PARTY_DIR .. name .. "/*.inl"),
		(THIRD_PARTY_DIR .. name .. "/*.natvis"),
	}
end

workspace (PROJECT_NAME)
	basedir (ROOT)
	configurations { "Debug", "Release", "DebugASAN" }
    platforms { "x64" }
	defines { "x64" }
	language "C++"
	cppdialect "c++20"
	startproject (PROJECT_NAME)
	symbols "On"
	architecture "x64"
	characterset "MBCS"
	flags {"MultiProcessorCompile", "ShadowedVariables"}
	rtti "Off"
	warnings "Extra"
	system "windows"
	conformancemode "On"
	defines { "PLATFORM_WINDOWS=1", "WIN32" }
	targetdir (TARGET_DIR .. "$(ProjectName)_$(Platform)_$(Configuration)")
	objdir (TARGET_DIR .. "Intermediate/$(ProjectName)_$(Platform)_$(Configuration)")
	
	-- Unreferenced variable
	disablewarnings {"4100"}
	-- unreferenced function with internal linkage has been removed
	disablewarnings {"4505"}
	-- nonstandard extension used: nameless struct/union
	disablewarnings {"4201"}
	
	filter "configurations:Debug"
		runtime "Debug"
		defines { "_DEBUG" }
		optimize "Off"
		editandcontinue "On"
		--inlining "Explicit"

	filter "configurations:Release"
 		runtime "Release"
		defines { "RELEASE" }
		optimize "Full"
		flags { "NoIncrementalLink" }

	filter "configurations:DebugASAN"
 		runtime "Debug"
		defines { "_DEBUG" }
		optimize "Off"
		flags { "NoRuntimeChecks", "NoIncrementalLink"}
		sanitize { "Address" }
		removeflags {"FatalWarnings"}

	filter {}

	project (PROJECT_NAME)
		location (ROOT)
		pchheader ("stdafx.h")
		pchsource (SOURCE_DIR .. "stdafx.cpp")
		systemversion (WIN_SDK)
		kind "WindowedApp"

		includedirs { SOURCE_DIR }

		files
		{ 
			(SOURCE_DIR .. "**.h"),
			(SOURCE_DIR .. "**.hpp"),
			(SOURCE_DIR .. "**.cpp"),
			(SOURCE_DIR .. "**.inl"),
			(SOURCE_DIR .. "**.c"),
			(SOURCE_DIR .. "**.natvis"),
			(SOURCE_DIR .. "**.editorconfig"),
		}

		files
		{
			(RESOURCE_DIR .. "Shaders/Interop/**.h")
		}

		vpaths 
		{
			{ ["ShaderInterop"] = (RESOURCE_DIR .. "Shaders/Interop/**.h") }
		}

		filter ("files:" .. THIRD_PARTY_DIR .. "**")
			flags { "NoPCH" }
			removeflags "FatalWarnings"
			warnings "Off"
		filter {}

		includedirs "$(SolutionDir)Resources/Shaders/Interop"

		-- D3D12
		includedirs "$(SolutionDir)ThirdParty/D3D12/include"
		runtimeDependency("D3D12/bin/D3D12Core.dll", "D3D12")
		runtimeDependency("D3D12/bin/d3d12SDKLayers.dll", "D3D12")
		links {	"d3d12.lib", "dxgi", "dxguid" }

		-- D3D12 Warp
		runtimeDependency("D3D12Warp/bin/d3d10warp.dll", "")

		-- PIX
		includedirs "$(SolutionDir)ThirdParty/Pix/include"
		libdirs "$(SolutionDir)ThirdParty/Pix/lib"
		runtimeDependency("Pix/bin/WinPixEventRuntime.dll", "")
		links { "WinPixEventRuntime" }

		-- DXC
		includedirs "$(SolutionDir)ThirdParty/Dxc/include"
		runtimeDependency ("Dxc/bin/dxcompiler.dll", "")
		--runtimeDependency ("Dxc/bin/dxil.dll", "")

		-- DirectXMath
		includedirs "$(SolutionDir)ThirdParty/DirectXMath/include"

		-- Live++
		live_pp_path = "LivePP/API/x64/LPP_API_Version_x64_CPP.h"
		live_pp = os.pathsearch(live_pp_path, os.getenv("PATH"))
		if live_pp then
			includedirs (live_pp .. "/LivePP/API/x64")
			defines ("LIVE_PP_PATH=L\"" .. live_pp .. "/LivePP\"")
			linkoptions { "/FUNCTIONPADMIN" }
			editandcontinue "Off"
		end

		-- Superluminal
		superluminal_path = "C:/Program Files/Superluminal/Performance/API"
		if os.isdir(superluminal_path) then
			includedirs (superluminal_path .. "/include")
			defines ("SUPERLUMINAL_PATH=L\"" .. superluminal_path .. "/dll/x64/PerformanceAPI.dll\"")
		end

		compileThirdPartyLibrary("ankerl")
		compileThirdPartyLibrary("cgltf")
		compileThirdPartyLibrary("EnTT")
		compileThirdPartyLibrary("FontAwesome")
		compileThirdPartyLibrary("ImGui")
		compileThirdPartyLibrary("Ldr")
		compileThirdPartyLibrary("MeshOptimizer")
		compileThirdPartyLibrary("SimpleMath")
		compileThirdPartyLibrary("Stb")
		compileThirdPartyLibrary("ImGuizmo")

newaction {
	trigger     = "clean",
	description = "Remove all binaries and generated files",

	execute = function()
		os.rmdir("Build")
		os.rmdir(".vs")
		os.remove("*.sln")
		os.remove("*.vcxproj.*")
	end
}
			
--------------------------------------------------------