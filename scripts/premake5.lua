require "utility"

ENGINE_NAME = "D3D12"
ROOT = "../"
SOURCE_DIR = ROOT .. ENGINE_NAME .. "/"
WIN_SDK = "10.0.19041.0"

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
	conformancemode "On"
	warnings "Extra"

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

		SetPlatformDefines()

		filter {"system:windows", "action:vs*"}
			systemversion (WIN_SDK)
		filter {}

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
		}

		filter ("files:" .. SOURCE_DIR .. "External/**")
			flags { "NoPCH" }
			removeflags "FatalWarnings"
			warnings "Default"
		filter {}

		---- External libraries ----
		AddAssimp()
		filter "system:Windows"
			AddD3D12()
			AddPix()
			AddDxc()
			
--------------------------------------------------------