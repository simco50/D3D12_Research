require "utility"

ENGINE_NAME = "D3D12"
ROOT = "../"
SOURCE_DIR = ROOT .. ENGINE_NAME .. "/"

workspace (ENGINE_NAME)
	basedir (ROOT)
	configurations { "Debug", "Release" }
    platforms { "x64" }
	defines { "_CONSOLE" }
	language ("C++")
	startproject (ENGINE_NAME)
	symbols ("On")
	kind ("ConsoleApp")
	characterset ("MBCS")

    filter "platforms:x64"
		defines {"x64"}
		architecture ("x64")

	filter "configurations:Debug"
		defines { "_DEBUG" }
		optimize ("Off")

	filter "configurations:Release"
		defines { "RELEASE" }
		optimize ("Full")

	project (ENGINE_NAME)
		location (ROOT .. ENGINE_NAME)
		targetdir (ROOT .. "Build/$(ProjectName)_$(Platform)_$(Configuration)")
		objdir (ROOT .. "Build/Intermediate/$(ProjectName)_$(Platform)_$(Configuration)")

		pchheader ("stdafx.h")
		pchsource (ROOT .. ENGINE_NAME .. "/stdafx.cpp")
		includedirs { "$(ProjectDir)" }

		SetPlatformDefines()

		filter {"system:windows", "action:vs*"}
			systemversion ("10.0.17763.0")
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
		}

		filter ("files:" .. SOURCE_DIR .. "External/**")
			flags { "NoPCH" }
		filter {}

		---- External libraries ----
		AddAssimp()
		filter "system:Windows"
			AddD3D12()
			AddPix()
			
--------------------------------------------------------