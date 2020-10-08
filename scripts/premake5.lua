require "utility"
require "winrt"

ENGINE_NAME = "D3D12"
ROOT = "../"
SOURCE_DIR = ROOT .. ENGINE_NAME .. "/"
WIN_SDK = "10.0.19041.0"

workspace (ENGINE_NAME)
	basedir (ROOT)
	configurations { "Debug", "Release" }
    platforms { "x64" }
	defines { "x64" }
	language ("C++")
	cppdialect "c++17"
	startproject (ENGINE_NAME)
	symbols ("On")
	architecture ("x64")
	kind ("WindowedApp")
	characterset ("MBCS")
	flags { "MultiProcessorCompile", "ShadowedVariables" }
	rtti "Off"

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

		if with_uwp then 
			system "windowsuniversal"
			defines { "PLATFORM_UWP" }
			consumewinrtextension "true"
			systemversion (WIN_SDK)
			defaultlanguage "en-GB"
			certificatefile "D3D12_TemporaryKey.pfx"

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
			defines { "PLATFORM_WINDOWS" }
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
		}


		filter ("files:" .. SOURCE_DIR .. "External/**")
			flags { "NoPCH" }
		filter {}

		---- External libraries ----
		AddAssimp()
		AddD3D12()
		AddPix()
		AddDxc()
			
--------------------------------------------------------