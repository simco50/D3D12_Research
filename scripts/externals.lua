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
	links {	"d3d12.lib", "dxgi", "d3dcompiler" }
end

function AddAssimp()
	filter {}
	includedirs (ROOT .. "Libraries/Assimp/include")
	libdirs	(ROOT .. "Libraries/Assimp/lib/x64")
	postbuildcommands { ("copy \"$(SolutionDir)Libraries\\Assimp\\bin\\x64\\assimp-vc140-mt.dll\" \"$(OutDir)\"") }
	links { "assimp-vc140-mt" }
end