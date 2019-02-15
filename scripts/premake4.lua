engineName = "D3D12"

ROOT = "../"

solution (engineName)
	basedir (ROOT)
	configurations { "Debug", "Development", "Shipping" }
    platforms {"x32", "x64"}
	defines { "_CONSOLE", "THREADING", "PLATFORM_WINDOWS"}
	language "C++"
	startproject (engineName)

    configuration { "x64" }
		defines {"x64", "_AMD64_"}

	configuration { "x32" }
		defines {"x32", "_X86_"}	

	configuration "Debug"
		defines { "_DEBUG" }
		flags { "Symbols", "ExtraWarnings" }
	configuration "Development"
		defines { "DEVELOPMENT" }
		flags {"OptimizeSpeed", "Symbols", "ExtraWarnings" }
	configuration "Shipping"
		defines { "SHIPPING" }
		flags {"OptimizeSpeed", "No64BitChecks" }

	configuration {}
		kind "WindowedApp"

	project (engineName)
		location (ROOT .. engineName)
		targetdir (ROOT .. "Build/" .. engineName .. "_$(Platform)_$(Configuration)")
		objdir (ROOT .. "Build/Intermediate/$(ProjectName)_$(Platform)_$(Configuration)")

		pchheader ("stdafx.h")
		pchsource (ROOT .. engineName .. "/stdafx.cpp")

		local windowsPlatform = "10.0.17763.0"
		local action = premake.action.current()
		action.vstudio.windowsTargetPlatformVersion    = windowsPlatform
		action.vstudio.windowsTargetPlatformMinVersion = windowsPlatform

		if _OPTIONS["base"] == "uwp" then
			defines { "PLATFORM_UWP" }
			premake.vstudio.toolset = "v141"
			premake.vstudio.storeapp = "10.0"
		else
			defines { "PLATFORM_WINDOWS" }
		end

		files
		{ 
			("../**.h"),
			("../**.hpp"),
			("../**.cpp"),
			("../**.inl"),
			("../**.c"),
			("../**.natvis"),
		}

		links
		{
			"d3d12",
			"dxgi",
			"d3dcompiler"
		}

		--includedirs (ROOT .. "Libraries/Assimp/include")
		--libdirs	(ROOT .. "Libraries/Assimp/lib/%{cfg.platform}")
		--postbuildcommands { "{COPY} \"$(SolutionDir)Libraries\\Assimp\\bin\\%{cfg.platform}\\assimp-vc140-mt.dll\" \"$(OutDir)\"" }
		
		includedirs { "$(ProjectDir)" }
		configuration {}

newoption {
	trigger     = "base",
	value       = "API",
	description = "Choose a particular 3D API for rendering",
	default     = "windows",
	allowed = {
		{ "windows", "Windows" },
		{ "uwp", "Universal Windows Platform" },
	}
}