  
--
-- Name:        winrt/winrt.lua
-- Purpose:     Define the WinRT APIs
-- Author:      Samuel Surtees
-- Copyright:   (c) 2015 Samuel Surtees and the Premake project
--

	require "vstudio"

--
-- Create a WinRT namespace to isolate the additions
--

	local p = premake

	p.modules.winrt = {}

	local m = p.modules.winrt
	m._VERSION = "0.0.1"

--
-- Register allowed config data
--

	local api = p.api

	--
	-- Register the WinRT extension
	--

	p.WINUNIVERSAL = "windowsuniversal"
	p.ARM = "arm"

	api.addAllowed("system", { p.WINUNIVERSAL })
	api.addAllowed("architecture", { p.ARM })

	--
	-- Register the AppxManifest action
	--

	newaction {
		trigger = "appxmanifest",
		shortname = "Package.appxmanifest",
		description = "Generate Package.appxmanifest files",

		valid_kinds = { "WindowedApp" },

		onProject = function(prj)
			p.modules.winrt.generateAppxManifest(prj)
		end,

		onCleanProject = function(prj)
			p.clean.directory(prj, prj.name)
		end,
	}

	--
	-- Register WinRT properties
	--

	api.register {
		name = "defaultlanguage",
		scope = "project",
		kind = "string",
	}

	api.register {
		name = "consumewinrtextension",
		scope = "config",
		kind = "string",
		allowed = {
			"true",
			"false",
		},
	}

	api.register {
		name = "deploy",
		scope = "config",
		kind = "string",
		allowed = {
			"true",
			"false",
		},
	}

	api.register {
		name = "generatewinmd",
		scope = "config",
		kind = "string",
		allowed = {
			"true",
			"false",
		}
	}

	api.register {
		name = "certificatefile",
		scope = "project",
		kind = "string",
	}

	api.register {
		name = "certificatethumbprint",
		scope = "project",
		kind = "string",
	}

	api.register {
		name = "minimumsystemversion",
		scope = "project",
		kind = "string",
	}

	--
	-- Set global environment for the default WinRT platforms
	--

	filter { "system:windowsuniversal", "kind:ConsoleApp or WindowedApp" }
		targetextension ".exe"

	filter { "system:windowsuniversal", "kind:SharedLib" }
		targetprefix ""
		targetextension ".dll"
		implibextension ".lib"

	filter { "system:windowsuniversal", "kind:StaticLib" }
		targetprefix ""
		targetextension ".lib"

	filter {}

--
-- Local functions
--

	local function isWinRT(_system)
		local system = _system or ""
		system = system:lower()
		return system == p.WINUNIVERSAL
	end

--
-- Override vs2010 project creation functions
--

	p.override(p.vstudio.vc2010.elements, "globals", function(base, prj)
		local elements = base(prj)
		if isWinRT(prj.system) then
			elements = table.join(elements, {
				m.defaultLanguage,
				m.applicationType,
			})
		end
		return elements
	end)

	p.override(p.vstudio.vc2010.elements, "configurationProperties", function(base, cfg)
		local elements = base(cfg)
		if cfg.kind ~= p.UTILITY then
			elements = table.join(elements, {
				m.windowsAppContainer,
			})
		end
		return elements
	end)

	p.override(p.vstudio.vc2010, "emitFiles", function(base, prj, group, tag, fileFunc, fileCfgFunc, checkFunc)
		if isWinRT(prj.system) then
			local oldFn = fileCfgFunc
			fileCfgFunc = function (...)
				local elements

				if type(oldFn) == "function" then
					elements = oldFn(...)
				else
					elements = oldFn
				end

				if tag == "ClCompile" then
					elements = table.join(elements or {}, {
						m.compileAsWinRT,
					})
				elseif tag == "None" then
					elements = table.join(elements or {}, {
						m.deploymentContent,
					})
				end

				return elements
			end
		end

		base(prj, group, tag, fileFunc, fileCfgFunc, checkFunc)
	end)

	p.override(p.vstudio.vc2010, "characterSet", function(base, cfg)
		if not isWinRT(cfg.system) then
			base(cfg)
		end
	end)

	p.override(p.vstudio.vc2010, "compileAs", function(base, cfg)
		base(cfg)

		if cfg.consumewinrtextension ~= nil then
			p.vstudio.vc2010.element("CompileAsWinRT", nil, cfg.consumewinrtextension)
		end
	end)

	p.override(p.vstudio.vc2010, "entryPointSymbol", function(base, cfg)
		if cfg.entrypoint or not isWinRT(cfg.system) then
			base(cfg)
		end
	end)

	p.override(p.vstudio.vc2010, "keyword", function(base, prj)
		local _isWinRT
		for cfg in p.project.eachconfig(prj) do
			if isWinRT(cfg.system) then
				_isWinRT = true
			end
		end

		if _isWinRT then
			p.vstudio.vc2010.element("AppContainerApplication", nil, "true")
		else
			base(prj)
		end
	end)

	p.override(p.vstudio.vc2010, "debuggerFlavor", function(base, cfg)
		if not isWinRT(cfg.system) then
			base(cfg)
		end
	end)

	function m.subtype(fcfg, condition)
		p.vstudio.vc2010.element("SubType", nil, "Designer")
	end

	p.vstudio.vc2010.categories.AppxManifest = {
		name = "AppxManifest",
		extensions = { ".appxmanifest" },
		priority = 99,

		emitFiles = function(prj, group)
			p.vstudio.vc2010.emitFiles(prj, group, "AppxManifest", { p.vstudio.vc2010.generatedFile, m.subtype })
		end,

		emitFilter = function(prj, group)
			p.vstudio.vc2010.filterGroup(prj, group, "AppxManifest")
		end
	}

	p.override(p.vstudio.vc2010.elements, "link", function(base, cfg, explicit)
		local elements = base(cfg, explicit)
		elements = table.join(elements, {
			m.generateWINMD,
		})
		return elements
	end)


	premake.override(p.vstudio.sln2005.elements, "projectConfigurationPlatforms", function(oldfn, cfg, context)
		local elements = oldfn(cfg, context)

		elements = table.join(elements, {
			m.deployProject
		})

		return elements
	end)


	function m.deployProject(cfg, context)
		if isWinRT(context.prj.system) and context.prj.kind == p.WINDOWEDAPP then
			p.w('{%s}.%s.Deploy.0 = %s|%s', context.prj.uuid, context.descriptor, context.platform, context.architecture)
		end
	end

	p.override(p.vstudio.vc2010, "userMacros", function(base, cfg)
		if cfg.certificatefile ~= nil or cfg.certificatethumbprint ~= nil then
			p.vstudio.vc2010.propertyGroup(nil, "UserMacros")

			if cfg.certificatefile ~= nil then
				p.vstudio.vc2010.element("PackageCertificateKeyFile", nil, cfg.certificatefile)
			end

			if cfg.certificatethumbprint ~= nil then
				p.vstudio.vc2010.element("PackageCertificateThumbprint", nil, cfg.certificatethumbprint)
			end

			p.pop('</PropertyGroup>')
		else
			base(cfg)
		end
	end)

	function m.applicationType(prj)
		local type
		local revision
		local minVersion
		if prj.system == p.WINUNIVERSAL then
			type = "Windows Store"
			revision = "10.0"
			minVersion = prj.systemversion
		end
		if prj.minimumsystemversion ~= nil then
			minVersion = prj.minimumsystemversion
		end
		p.vstudio.vc2010.element("ApplicationType", nil, type)
		p.vstudio.vc2010.element("ApplicationTypeRevision", nil, revision)
		p.vstudio.vc2010.element("WindowsTargetPlatformMinVersion", nil, minVersion)
	end

	function m.defaultLanguage(prj)
		if prj.defaultlanguage ~= nil then
			p.vstudio.vc2010.element("DefaultLanguage", nil, prj.defaultLanguage)
		end
	end

	function m.compileAsWinRT(fcfg, condition)
		if fcfg and fcfg.consumewinrtextension then
			p.vstudio.vc2010.element("CompileAsWinRT", condition, fcfg.consumewinrtextension)
		end
	end

	function m.deploymentContent(fcfg, condition)
		if fcfg and fcfg.deploy then
			p.vstudio.vc2010.element("DeploymentContent", nil, fcfg.deploy)
		end
	end

	function m.windowsAppContainer(cfg)
		if isWinRT(cfg.system) then
			p.vstudio.vc2010.element("WindowsAppContainer", nil, "true")
		end
	end

	function m.generateWINMD(cfg, explicit)
		if cfg.generatewinmd then
			p.vstudio.vc2010.element("GenerateWindowsMetadata", nil, cfg.generatewinmd)
		end
	end

--
-- Create Package.appxmanifest
--

	function m.generateAppxManifest(prj)
		p.eol("\r\n")
		p.indent("  ")
		p.escaper(vs2010.esc)

		p.generate(prj, "Package.appxmanifest", m.generate)
	end

	function m.generate(prj)
		_p('<?xml version="1.0" encoding="utf-8"?>')
		_p('<Package')
		if prj.system == p.WINUNIVERSAL then
			_p('xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"')
		end

		if prj.system == p.WINUNIVERSAL then
			_p('xmlns:m3="http://schemas.microsoft.com/appx/manifest/uap/windows10"')
		end

		if prj.system == p.WINUNIVERSAL then
			_p('xmlns:mp="http://schemas.microsoft.com/appx/2014/phone/manifest"')
		end

		_p('>')

		_p(1,'<Identity Name="%s"', prj.uuid)
		_p(2,'Publisher="CN=PublisherName"')
		_p(2,'Version="0.0.0.0" />')

		if prj.system == p.WINUNIVERSAL then
			_p(1,'<mp:PhoneIdentity PhoneProductId="%s" PhonePublisherId="00000000-0000-0000-0000-000000000000"/>', prj.uuid)
		end

		_p(1,'<Properties>')
		_p(2,'<DisplayName>%s</DisplayName>', prj.name)
		_p(2,'<PublisherDisplayName>PublisherName</PublisherDisplayName>')
		_p(2,'<Logo>Logo.png</Logo>')
		_p(1,'</Properties>')

		_p(1,'<Prerequisites>')
		if prj.system == p.WINUNIVERSAL then
			_p(1,'<Dependencies>')
			_p(2,'<TargetDeviceFamily Name="Windows.Universal" MinVersion="10.0.10069.0" MaxVersionTested="10.0.10069.0" />')
			_p(1,'</Dependencies>')
		end
		_p(1,'</Prerequisites>')

		_p(1,'<Resources>')
		_p(2,'<Resource Language="x-generate"/>')
		_p(1,'</Resources>')
		
		_p(1,'<Applications>')
		_p(2,'<Application Id="App"')
		_p(3,'Executable="$targetnametoken$.exe"')
		_p(3,'EntryPoint="$safeprojectname$.App">')
		_p(3,'<m3:VisualElements')
		_p(4,'DisplayName="$projectname$"')
		_p(4,'Description="$projectname$"')
		_p(4,'ForegroundText="light"')
		_p(4,'BackgroundColor="transparent">')
		_p(4,'<m3:SplashScreen Image="SplashScreen.png"/>')
		_p(3,'</m3:VisualElements>')
		_p(2,'</Application>')
		_p(1,'</Applications>')
		
		_p(1,'<Capabilities></Capabilities>')

		_p('</Package>')
	end

	return m
