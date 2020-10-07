function AddPix()
	filter {}
	includedirs (ROOT .. "Libraries/Pix/include")
	libdirs (ROOT .. "Libraries/Pix/lib")
	postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries\\Pix\\bin\\WinPixEventRuntime.dll\" \"$(OutDir)\"") }
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
	postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries\\Assimp\\bin\\x64\\assimp-vc140-mt.dll\" \"$(OutDir)\"") }
	links { "assimp-vc140-mt" }
end

function AddDxc()
	links { "dxcompiler" }
	libdirs	(ROOT .. "Libraries/Dxc/lib/")
	includedirs (ROOT .. "Libraries/Dxc/include")
	postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries\\Dxc\\bin\\dxcompiler.dll\" \"$(OutDir)\"") }
	postbuildcommands { ("{COPY} \"$(SolutionDir)Libraries\\Dxc\\bin\\dxil.dll\" \"$(OutDir)\"") }
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

newoption {
	trigger     = "uwp",
	description = "Generates a UWP solution"
  }

with_uwp = _OPTIONS["uwp"]