engineName = "D3D12"

ROOT = "../"

solution (engineName)
	basedir (ROOT)
	configurations { "Debug", "Release" }
    platforms {"x64"}
	defines { "_CONSOLE", "THREADING", "PLATFORM_WINDOWS"}
	language "C++"
	startproject (engineName)

    configuration { "x64" }
		defines {"x64", "_AMD64_"}

	configuration "Debug"
		defines { "_DEBUG" }
		flags { "Symbols", "ExtraWarnings" }
	configuration "Release"
		defines { "RELEASE" }
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

		nopch
		{
			("../" .. engineName .. "/External/**")
		}

		includedirs (ROOT .. "Libraries/Assimp/include")
		libdirs	(ROOT .. "Libraries/Assimp/lib/x64")
		postbuildcommands { ("copy \"$(SolutionDir)Libraries\\Assimp\\bin\\x64\\assimp-vc140-mt.dll\" \"$(OutDir)\"") }
		links { "assimp-vc140-mt" }

		configuration {}
		includedirs (ROOT .. "Libraries/Pix/include")
		libdirs (ROOT .. "Libraries/Pix/lib")
		
		if _OPTIONS["base"] == "uwp" then
			libdirs (ROOT .. "Libraries/Pix/lib/WinPixEventRuntime_UAP.lib")
			postbuildcommands { ("copy \"$(SolutionDir)Libraries\\Pix\\bin\\WinPixEventRuntime_UAP.dll\" \"$(OutDir)\"") }
			postbuildcommands { ("xcopy \"$(ProjectDir)Resources\" \"$(OutDir)AppX\" /s/h/e/k/f/c/y") }
			links { "WinPixEventRuntime_UAP" }
		else
			configuration { "windows" }
				postbuildcommands { ("copy \"$(SolutionDir)Libraries\\Pix\\bin\\WinPixEventRuntime.dll\" \"$(OutDir)\"") }
				links { "WinPixEventRuntime" }
		end

		configuration {}
		
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