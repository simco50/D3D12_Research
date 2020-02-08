function SetPlatformDefines()
	filter "system: windows"
		defines { "PLATFORM_WINDOWS" }
	filter {}
end

function AddPix()
	filter {}
	includedirs (ROOT .. "Libraries/Pix/include")
	libdirs (ROOT .. "Libraries/Pix/lib")
	postbuildcommands { ("copy \"$(SolutionDir)Libraries\\Pix\\bin\\WinPixEventRuntime.dll\" \"$(OutDir)\"") }
	links { "WinPixEventRuntime" }
end

function AddD3D12()
	filter {}
	links {	"d3d12.lib", "dxgi", "d3dcompiler", "dxguid" }
end

function AddAssimp()
	filter {}
	includedirs (ROOT .. "Libraries/Assimp/include")
	libdirs	(ROOT .. "Libraries/Assimp/lib/x64")
	postbuildcommands { ("copy \"$(SolutionDir)Libraries\\Assimp\\bin\\x64\\assimp-vc140-mt.dll\" \"$(OutDir)\"") }
	links { "assimp-vc140-mt" }
end

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

function AddDxc()
	postbuildcommands { ("copy \"$(SolutionDir)Libraries\\Dxc\\dxcompiler.dll\" \"$(OutDir)\"") }
	postbuildcommands { ("copy \"$(SolutionDir)Libraries\\Dxc\\dxil.dll\" \"$(OutDir)\"") }
end