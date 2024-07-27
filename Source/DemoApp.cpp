#include "stdafx.h"
#include "DemoApp.h"

#include "Core/CommandLine.h"
#include "Core/Profiler.h"
#include "Core/ConsoleVariables.h"

#include "Renderer/Renderer.h"
#include "Renderer/Techniques/DDGI.h"
#include "Renderer/Techniques/CBTTessellation.h"
#include "Renderer/Techniques/DebugRenderer.h"
#include "Renderer/Mesh.h"
#include "Renderer/Light.h"

#include "Scene/SceneLoader.h"
#include "Scene/Camera.h"

#include <imgui_internal.h>
#include <IconsFontAwesome4.h>
#include <ImGuizmo.h>

bool sScreenshotNextFrame = false;

DemoApp::DemoApp() = default;

DemoApp::~DemoApp() = default;

void DemoApp::Init()
{
	const char* pScene = "Resources/Scenes/Sponza/Sponza.gltf";
	CommandLine::GetValue("scene", &pScene);
	SetupScene(pScene);

	m_Renderer.Init(m_pDevice, &m_World);
}

void DemoApp::Update()
{
	DrawImGui();

	{
		PROFILE_CPU_SCOPE("Update Entity Transforms");
		auto view = m_World.Registry.view<Transform>();
		view.each([&](Transform& transform)
			{
				transform.WorldPrev = transform.World;
				transform.World = Matrix::CreateScale(transform.Scale) *
					Matrix::CreateFromQuaternion(transform.Rotation) *
					Matrix::CreateTranslation(transform.Position);
			});
	}

	if (m_pViewportTexture)
	{
		m_Renderer.Render(m_pViewportTexture);

		if (sScreenshotNextFrame)
		{
			sScreenshotNextFrame = false;
			m_Renderer.MakeScreenshot(m_pViewportTexture);
		}
	}
}

void DemoApp::Shutdown()
{
	m_Renderer.Shutdown();
}

void DemoApp::SetupScene(const char* pFilePath)
{
	m_World.Camera = m_World.CreateEntity("Main Camera");
	FreeCamera& camera = m_World.Registry.emplace<FreeCamera>(m_World.Camera);
	camera.SetPosition(Vector3(-1.3f, 1.4f, -1.5f));
	camera.SetRotation(Quaternion::CreateFromYawPitchRoll(Math::PI_DIV_4, Math::PI_DIV_4 * 0.5f, 0));

	SceneLoader::Load(pFilePath, m_pDevice, m_World);

	{
		entt::entity entity = m_World.CreateEntity("Sunlight");
		Transform& transform = m_World.Registry.emplace<Transform>(entity);
		transform.Position = Vector3::Zero;
		transform.Rotation = Quaternion::CreateFromYawPitchRoll(Math::PI / 3, Math::PI_DIV_4, 0);

		Light& sunLight = m_World.Registry.emplace<Light>(entity);
		sunLight.Intensity = 5;
		sunLight.CastShadows = true;
		sunLight.VolumetricLighting = true;
		sunLight.Type = LightType::Directional;
		sunLight.Colour = Math::MakeFromColorTemperature(5900);
		m_World.Sunlight = entity;
	}

	{
		Light spot;
		spot.Range = 4;
		spot.OuterConeAngle = 70.0f * Math::DegreesToRadians;
		spot.InnerConeAngle = 50.0f * Math::DegreesToRadians;
		spot.Intensity = 100.0f;
		spot.CastShadows = true;
		spot.VolumetricLighting = true;
		spot.pLightTexture = GraphicsCommon::CreateTextureFromFile(m_pDevice, "Resources/Textures/LightProjector.png", false, "Light Cookie");
		spot.Type = LightType::Spot;

		Vector3 positions[] = {
			Vector3(9.5, 3, 3.5),
			Vector3(-9.5, 3, 3.5),
			Vector3(9.5, 3, -3.5),
			Vector3(-9.5, 3, -3.5),
		};

		for (Vector3 v : positions)
		{
			entt::entity entity = m_World.CreateEntity("Spotlight");
			Transform& transform = m_World.Registry.emplace<Transform>(entity);
			transform.Rotation = Quaternion::LookRotation(Vector3::Down, Vector3::Right);
			transform.Position = v;
			m_World.Registry.emplace<Light>(entity, spot);;
		}
	}
	{
		entt::entity entity = m_World.CreateEntity("DDGI Volume");
		Transform& transform = m_World.Registry.emplace<Transform>(entity);
		transform.Position = Vector3(-0.484151840f, 5.21196413f, 0.309524536f);

		DDGIVolume& volume = m_World.Registry.emplace<DDGIVolume>(entity);
		volume.Extents = Vector3(14.8834171f, 6.22350454f, 9.15293312f);
		volume.NumProbes = Vector3i(16, 12, 14);
		volume.NumRays = 128;
		volume.MaxNumRays = 512;
	}

	{
		entt::entity entity = m_World.CreateEntity("Fog Volume");
		Transform& transform = m_World.Registry.emplace<Transform>(entity);
		transform.Position = Vector3(0, 1, 0);

		FogVolume& volume = m_World.Registry.emplace<FogVolume>(entity);
		volume.Extents = Vector3(100, 100, 100);
		volume.Color = Vector3(1, 1, 1);
		volume.DensityBase = 0;
		volume.DensityChange = 0.03f;
	}

	{
		entt::entity entity = m_World.CreateEntity("Terrain");
		CBTData cbtData = m_World.Registry.emplace<CBTData>(entity);
	}

	auto ddgi_view = m_World.Registry.view<Transform, DDGIVolume>();
}


void DemoApp::DrawImGui()
{
	static ImGuiConsole console;
	static bool showProfiler = false;
	static bool showImguiDemo = false;
	static bool showToolMetrics = false;

	if (showImguiDemo)
		ImGui::ShowDemoWindow();

	if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_P))
		showProfiler = !showProfiler;

	ImGuiViewport* pViewport = ImGui::GetMainViewport();
	ImGuiID dockspace = ImGui::DockSpaceOverViewport(pViewport);

	if (!ImGui::FindWindowSettingsByID(ImHashStr("ViewportSettings")))
	{
		ImGui::CreateNewWindowSettings("ViewportSettings");
		ImGuiID viewportID, parametersID;
		ImGui::DockBuilderRemoveNode(dockspace);
		ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_CentralNode);
		ImGui::DockBuilderSetNodeSize(dockspace, pViewport->Size);
		ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Right, 0.2f, &parametersID, &viewportID);
		ImGui::DockBuilderDockWindow("Settings", parametersID);
		ImGui::DockBuilderGetNode(viewportID)->LocalFlags |= ImGuiDockNodeFlags_HiddenTabBar;
		ImGui::DockBuilderGetNode(viewportID)->UpdateMergedFlags();
		ImGui::DockBuilderDockWindow(ICON_FA_DESKTOP " Viewport", viewportID);
		ImGui::DockBuilderFinish(dockspace);
	}

	console.Update();

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu(ICON_FA_FILE " File"))
		{
			if (ImGui::MenuItem(ICON_FA_FILE " Load Mesh", nullptr, nullptr))
			{
				OPENFILENAME ofn{};
				TCHAR szFile[260]{};
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = m_Window.GetNativeWindow();
				ofn.lpstrFile = szFile;
				ofn.nMaxFile = sizeof(szFile);
				ofn.lpstrFilter = "Supported files (*.gltf;*.glb;*.dat;*.ldr;*.mpd)\0*.gltf;*.glb;*.dat;*.ldr;*.mpd\0All Files (*.*)\0*.*\0";;
				ofn.nFilterIndex = 1;
				ofn.lpstrFileTitle = NULL;
				ofn.nMaxFileTitle = 0;
				ofn.lpstrInitialDir = NULL;
				ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

				if (GetOpenFileNameA(&ofn) == TRUE)
				{
					SetupScene(ofn.lpstrFile);
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(ICON_FA_WINDOW_MAXIMIZE " Windows"))
		{
			if (ImGui::MenuItem(ICON_FA_CLOCK_O " Profiler", "Ctrl + P", showProfiler))
			{
				showProfiler = !showProfiler;
			}
			if (ImGui::MenuItem("RenderGraph Resource Tracker", "Ctrl + R"))
			{
				static IConsoleObject* resourceTracker = ConsoleManager::FindConsoleObject("r.RenderGraph.ResourceTracker");
				resourceTracker->Set("1");
			}
			if (ImGui::MenuItem("RenderGraph Pass View", "Ctrl + T"))
			{
				static IConsoleObject* passView = ConsoleManager::FindConsoleObject("r.RenderGraph.PassView");
				passView->Set("1");
			}
			if (ImGui::MenuItem("ImGui Metrics"))
			{
				showToolMetrics = !showToolMetrics;
			}
			bool& showConsole = console.IsVisible();
			if (ImGui::MenuItem("Output Log", "~", showConsole))
			{
				showConsole = !showConsole;
			}
			if (ImGui::MenuItem("Luminance Histogram"))
			{
				static IConsoleObject* histogram = ConsoleManager::FindConsoleObject("r.Histogram");
				histogram->Set("1");
			}

			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(ICON_FA_WRENCH " Tools"))
		{
			if (ImGui::MenuItem("Dump RenderGraph"))
			{
				static IConsoleObject* pDumpGraph = ConsoleManager::FindConsoleObject("DumpRenderGraph");
				pDumpGraph->AsCommand()->Execute(nullptr, 0);
			}
			if (ImGui::MenuItem("Screenshot"))
			{
				sScreenshotNextFrame = true;
			}
			if (ImGui::MenuItem("Pix Capture"))
			{
				D3D::EnqueuePIXCapture();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(ICON_FA_QUESTION " Help"))
		{
			if (ImGui::MenuItem("ImGui Demo", 0, showImguiDemo))
			{
				showImguiDemo = !showImguiDemo;
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	if (showToolMetrics)
		ImGui::ShowMetricsWindow(&showToolMetrics);

	ImGui::Begin(ICON_FA_DESKTOP " Viewport", 0, ImGuiWindowFlags_NoScrollbar);
	ImVec2 viewportPos = ImGui::GetWindowPos();
	ImVec2 viewportSize = ImGui::GetWindowSize();
	FloatRect viewport(viewportPos.x, viewportPos.y, viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y);
	ImVec2 imageSize = ImMax(ImGui::GetContentRegionAvail(), ImVec2(16.0f, 16.0f));
	if (!m_pViewportTexture || imageSize.x != m_pViewportTexture->GetWidth() || imageSize.y != m_pViewportTexture->GetHeight())
	{
		m_pViewportTexture = m_pDevice->CreateTexture(TextureDesc::Create2D((uint32)imageSize.x, (uint32)imageSize.y, ResourceFormat::RGBA8_UNORM, 1, TextureFlag::ShaderResource | TextureFlag::UnorderedAccess), "Viewport");
	}
	ImGui::Image(m_pViewportTexture, imageSize);
	ImVec2 viewportOrigin = ImGui::GetItemRectMin();
	ImVec2 viewportExtents = ImGui::GetItemRectSize();
	ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());


	ImGui::End();

	ImGuizmo::SetRect(viewportOrigin.x, viewportOrigin.y, viewportExtents.x, viewportExtents.y);
	DrawOutliner();

	if (showProfiler)
	{
		PROFILE_CPU_SCOPE("Profiler");
		if (ImGui::Begin("Profiler", &showProfiler))
		{
			DrawProfilerHUD();
		}
		ImGui::End();
	}
	else
	{
		gCPUProfiler.SetPaused(true);
		gGPUProfiler.SetPaused(true);
	}

	if (ImGui::Begin("Settings"))
	{
		if (ImGui::CollapsingHeader("Swapchain"))
		{
			bool vsync = m_pSwapchain->GetVSync();
			if (ImGui::Checkbox("Vertical Sync", &vsync))
				m_pSwapchain->SetVSync(vsync);
			int swapchainFrames = m_pSwapchain->GetNumFrames();
			if (ImGui::SliderInt("Swapchain Frames", &swapchainFrames, 2, 5))
				m_pSwapchain->SetNumFrames(swapchainFrames);
			bool waitableSwapChain = m_pSwapchain->GetUseWaitableSwapChain();
			if (ImGui::Checkbox("Waitable Swapchain", &waitableSwapChain))
				m_pSwapchain->SetUseWaitableSwapChain(waitableSwapChain);
			int frameLatency = m_pSwapchain->GetMaxFrameLatency();
			if (ImGui::SliderInt("Max Frame Latency", &frameLatency, 1, 5))
				m_pSwapchain->SetMaxFrameLatency(frameLatency);
		}
	}
	ImGui::End();

	m_Renderer.DrawImGui(viewport);
}


void DemoApp::DrawOutliner()
{
	static entt::entity selectedEntity{};
	if (ImGui::Begin("Outliner"))
	{
		auto entity_view = m_World.Registry.view<Identity>();
		entity_view.each([&](entt::entity entity, Identity& t)
			{
				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
				if (selectedEntity == entity)
					flags |= ImGuiTreeNodeFlags_Selected;
				ImGui::TreeNodeEx(Sprintf("%d", (int)entity).c_str(), flags, t.Name.c_str());
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
					selectedEntity = entity;
			});
	}
	ImGui::End();

	if (!m_World.Registry.valid(selectedEntity))
		selectedEntity = entt::null;

	if (selectedEntity != entt::null)
	{
		if (ImGui::Begin("Entity"))
		{
			if (Transform* transform = m_World.Registry.try_get<Transform>(selectedEntity))
			{
				Transform& t = *transform;
				if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen))
				{
					static ImGuizmo::OPERATION gizmoOperation(ImGuizmo::ROTATE);
					static ImGuizmo::MODE gizmoMode(ImGuizmo::WORLD);
					if (ImGui::IsKeyPressed(ImGuiKey_W))
						gizmoOperation = ImGuizmo::TRANSLATE;
					if (ImGui::IsKeyPressed(ImGuiKey_E))
						gizmoOperation = ImGuizmo::ROTATE;
					if (ImGui::IsKeyPressed(ImGuiKey_R))
						gizmoOperation = ImGuizmo::SCALE;

					ImGui::InputFloat3("Translation", &t.Position.x);
					ImGui::InputFloat4("Rotation", &t.Rotation.x);
					ImGui::InputFloat3("Scale", &t.Scale.x);

					if (gizmoOperation != ImGuizmo::SCALE)
					{
						if (ImGui::RadioButton("Local", gizmoMode == ImGuizmo::LOCAL))
							gizmoMode = ImGuizmo::LOCAL;
						ImGui::SameLine();
						if (ImGui::RadioButton("World", gizmoMode == ImGuizmo::WORLD))
							gizmoMode = ImGuizmo::WORLD;
					}

					Matrix worldTransform = Matrix::CreateScale(t.Scale) * Matrix::CreateFromQuaternion(t.Rotation) * Matrix::CreateTranslation(t.Position);
					if (ImGuizmo::Manipulate(&m_Renderer.GetMainView().WorldToView.m[0][0], &m_Renderer.GetMainView().ViewToClipUnjittered.m[0][0], gizmoOperation, gizmoMode, &worldTransform.m[0][0], nullptr, nullptr, nullptr, nullptr))
						worldTransform.Decompose(t.Scale, t.Rotation, t.Position);

					ImGui::TreePop();
				}
			}
			if (Light* light = m_World.Registry.try_get<Light>(selectedEntity))
			{
				Transform& t = m_World.Registry.get<Transform>(selectedEntity);
				DebugRenderer::Get()->AddLight(t, *light, Colors::Yellow);
				if (ImGui::TreeNodeEx("Light", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Combo("Type", (int*)&light->Type, gLightTypeStr, (int)LightType::MAX);
					if (light->Type == LightType::Point)
					{
						ImGui::InputFloat("Radius", &light->Range);
					}
					else if (light->Type == LightType::Spot)
					{
						ImGui::InputFloat("Range", &light->Range);
						if (ImGui::SliderAngle("Inner Angle", &light->InnerConeAngle, 0, 179))
							light->OuterConeAngle = Math::Max(light->OuterConeAngle, light->InnerConeAngle);
						if(ImGui::SliderAngle("Outer Angle", &light->OuterConeAngle, 0, 179))
							light->InnerConeAngle = Math::Min(light->OuterConeAngle, light->InnerConeAngle);
					}
					ImGui::ColorEdit3("Color", &light->Colour.x);
					ImGui::InputFloat("Intensity", &light->Intensity);
					ImGui::Checkbox("Cast Shadows", &light->CastShadows);
					ImGui::Checkbox("Volumetric Lighting", &light->VolumetricLighting);
					ImGui::TreePop();
				}
			}
			if (DDGIVolume* ddgi = m_World.Registry.try_get<DDGIVolume>(selectedEntity))
			{
				Transform& t = m_World.Registry.get<Transform>(selectedEntity);
				DebugRenderer::Get()->AddBoundingBox(BoundingBox(Vector3::Zero, ddgi->Extents), t.World, Colors::White);
				if (ImGui::TreeNodeEx("DDGI", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::SliderFloat3("Extents", &ddgi->Extents.x, -100, 100);
					ImGui::SliderInt3("Probe Count", &ddgi->NumProbes.x, 1, 100);
					ImGui::SliderInt("Max Num Rays", &ddgi->MaxNumRays, 1, 500);
					ImGui::SliderInt("Num Rays", &ddgi->NumRays, 1, 500);
					ImGui::TreePop();
				}
			}
			if (FogVolume* fog = m_World.Registry.try_get<FogVolume>(selectedEntity))
			{
				Transform& t = m_World.Registry.get<Transform>(selectedEntity);
				DebugRenderer::Get()->AddBoundingBox(BoundingBox(Vector3::Zero, fog->Extents), t.World, Colors::White);
				if (ImGui::TreeNodeEx("Fog Volume", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::SliderFloat3("Extents", &fog->Extents.x, 0, 20);
					ImGui::SliderFloat("Density Base", &fog->DensityBase, 0, 1);
					ImGui::SliderFloat("Density Change", &fog->DensityChange, 0, 1);
					ImGui::ColorEdit3("Color", &fog->Color.x);
					ImGui::TreePop();
				}
			}
			if (Model* pModel = m_World.Registry.try_get<Model>(selectedEntity))
			{
				Transform& t = m_World.Registry.get<Transform>(selectedEntity);
				DebugRenderer::Get()->AddBoundingBox(m_World.Meshes[pModel->MeshIndex].Bounds, t.World, Colors::White);
				if (ImGui::TreeNodeEx("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (pModel->AnimationIndex != -1)
					{
						ImGui::Combo("Animation", &pModel->AnimationIndex, [](void* pUserData, int index)
							{
								const World* pWorld = (World*)pUserData;
								return pWorld->Animations[index].Name.c_str();
							}, &m_World, (int)m_World.Animations.size());
					}
					ImGui::TreePop();
				}
			}
		}
		ImGui::End();
	}
}
