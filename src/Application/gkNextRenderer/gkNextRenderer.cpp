#include "gkNextRenderer.hpp"

#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

#include "Assets/FProcModel.h"
#include "Assets/Node.h"
#include "Runtime/Engine.hpp"
#include "Utilities/Localization.hpp"
#include "Utilities/ImGui.hpp"
#include "Runtime/Platform/PlatformCommon.h"

constexpr float TITLEBAR_SIZE = 40;
constexpr float TITLEBAR_CONTROL_SIZE = TITLEBAR_SIZE * 3;
constexpr float ICON_SIZE = 64;
constexpr float PALATE_SIZE = 46;
constexpr float BUTTON_SIZE = 36;
constexpr float BUILD_BAR_WIDTH = 240;
constexpr float SIDE_BAR_WIDTH = 300;
constexpr float SHORTCUT_SIZE = 10;

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<NextRendererGameInstance>(config, options, engine);
}

NextRendererGameInstance::NextRendererGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine):NextGameInstanceBase(config,options,engine),engine_(engine)
{
	config.HideTitleBar = true;
}

void NextRendererGameInstance::OnInit()
{
	std::string initializedScene = SceneList::AllScenes[GetEngine().GetUserSettings().SceneIndex];
	if (!GOption->SceneName.empty())
	{
		initializedScene = GOption->SceneName;
	}
	GetEngine().RequestLoadScene(initializedScene);
}

void NextRendererGameInstance::OnTick(double deltaSeconds)
{
    modelViewController_.UpdateCamera(10.0f, deltaSeconds);
}

std::vector<Assets::FMaterial> matPreparedForAdd;

void NextRendererGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
	std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
	std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
{
	models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0,0,0), 0.2f));
	modelId_ = static_cast<uint32_t>(models.size() - 1);

	matIds_.clear();
	
	matPreparedForAdd.push_back({Assets::Material::Lambertian(glm::vec3(1,1,1))});
	materials.push_back(matPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
	matPreparedForAdd.push_back({Assets::Material::Metallic(glm::vec3(0.5,0.5,0.5), 0.4f)});
	materials.push_back(matPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
	matPreparedForAdd.push_back({Assets::Material::Dielectric(1.5f, 0.0f)});
	materials.push_back(matPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
	matPreparedForAdd.push_back({Assets::Material::Mixture(glm::vec3(1.0f, 0.3f, 0.3f), 0.01f)});
	materials.push_back(matPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
}

void NextRendererGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();
    modelViewController_.Reset( GetEngine().GetScene().GetRenderCamera() );

	GetEngine().GetScene().PlayAllTracks();
}

void NextRendererGameInstance::OnPreConfigUI()
{
    NextGameInstanceBase::OnPreConfigUI();
}

bool NextRendererGameInstance::OnRenderUI()
{
	DrawTitleBar();
	DrawSettings();
	if (GOption->ReferenceMode)
	{
		// 获取屏幕尺寸
		ImGuiIO& io = ImGui::GetIO();
        
		// 渲染器名称数组
		const char* rendererNames[] = {"SoftModern", "SoftTracing", "VoxelTracing" , "PathTracing"};
        
		// 四个象限的位置
		ImVec2 positions[] = {
			ImVec2(io.DisplaySize.x * 0.25f, io.DisplaySize.y * 0.45f),  // 左上
			ImVec2(io.DisplaySize.x * 0.75f, io.DisplaySize.y * 0.45f),  // 右上
			ImVec2(io.DisplaySize.x * 0.25f, io.DisplaySize.y * 0.95f),  // 左下
			ImVec2(io.DisplaySize.x * 0.75f, io.DisplaySize.y * 0.95f)   // 右下
		};
        
		// 为每个象限添加渲染器名称标签
		for (int i = 0; i < 4; i++)
		{
			ImGui::SetNextWindowPos(positions[i], ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowBgAlpha(0.5f);
            
			char windowName[32];
			snprintf(windowName, sizeof(windowName), "RendererName%d", i);
            
			if (ImGui::Begin(windowName, nullptr, 
				ImGuiWindowFlags_NoDecoration | 
				ImGuiWindowFlags_AlwaysAutoResize | 
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoFocusOnAppearing))
			{
				// 如果i小于渲染器数量，则显示对应渲染器名称
				if (i < sizeof(rendererNames)/sizeof(rendererNames[0]))
				{
					ImGui::Text("%s", rendererNames[i]);
				}
			}
			ImGui::End();
		}
	}
    return false;
}

void NextRendererGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();

	if (bigFont_ == nullptr)
	{
		ImFontGlyphRangesBuilder builder;
		builder.AddText("gkNextRenderer");
		const ImWchar* glyphRange = ImGui::GetIO().Fonts->GetGlyphRangesDefault();
		bigFont_ = ImGui::GetIO().Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/Roboto-BoldCondensed.ttf").c_str(), 24, nullptr, glyphRange);
	}
}

bool NextRendererGameInstance::OverrideRenderCamera(Assets::Camera& OutRenderCamera) const
{
    OutRenderCamera.ModelView = modelViewController_.ModelView();
	OutRenderCamera.FieldOfView = modelViewController_.FieldOfView();
    return true;
}

bool NextRendererGameInstance::OnKey(SDL_Event& event)
{
    modelViewController_.OnKey(event);

	if (event.key.type == SDL_EVENT_KEY_DOWN)
	{
		switch (event.key.key)
		{
		case SDLK_ESCAPE: GetEngine().GetScene().SetSelectedId(-1); return true;
			break;
		case SDLK_F1: GetEngine().GetUserSettings().ShowSettings = !GetEngine().GetUserSettings().ShowSettings; return true;
			break;
		case SDLK_F2: GetEngine().GetUserSettings().ShowOverlay = !GetEngine().GetUserSettings().ShowOverlay; return true;
			break;
		case SDLK_SPACE: CreateSphereAndPush(); return true;
			break;
		default: break;
		}
	}
    return false;
}

bool NextRendererGameInstance::OnCursorPosition(double xpos, double ypos)
{
    modelViewController_.OnCursorPosition(  xpos,  ypos);
    return true;
}

bool NextRendererGameInstance::OnMouseButton(SDL_Event& event)
{
    modelViewController_.OnMouseButton(event);

	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
	{
		auto mousePos = GetEngine().GetMousePos();
		glm::vec3 org;
		glm::vec3 dir;
		GetEngine().GetScreenToWorldRay(mousePos, org, dir);
		GetEngine().RayCastGPU( org, dir, [this](Assets::RayCastResult result)
		{
			if (result.Hitted)
			{
				GetEngine().GetScene().GetRenderCamera().FocalDistance = result.T;
				GetEngine().DrawAuxPoint( result.HitPoint, glm::vec4(0.2, 1, 0.2, 1), 2, 60 );
			}
			return true;
		});
		return true;
	}

    return true;
}

bool NextRendererGameInstance::OnScroll(double xoffset, double yoffset)
{
	modelViewController_.OnScroll( xoffset,  yoffset);
	return true;
}

bool NextRendererGameInstance::OnGamepadInput(float leftStickX, float leftStickY, float rightStickX, float rightStickY,
	float leftTrigger, float rightTrigger)
{
	return modelViewController_.OnGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, leftTrigger, rightTrigger);
}


void NextRendererGameInstance::CreateSphereAndPush()
{
	glm::vec3 forward = modelViewController_.GetForward();
	glm::vec3 center = modelViewController_.GetPosition() + forward * 0.1f + modelViewController_.GetRight() * 0.5f + modelViewController_.GetUp() * -0.5f;
	glm::vec3 farTarget = modelViewController_.GetPosition() + forward * 1000.0f + modelViewController_.GetUp() * 100.f;
	glm::vec3 shotDir = normalize((farTarget - center));
	uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());
	std::shared_ptr<Assets::Node> newNode = Assets::Node::CreateNode("temp", center, glm::quat(), glm::vec3(1), modelId_,
															   instanceId, false);
	// int random = std::rand() % matPreparedForAdd.size();
	// Assets::FMaterial instanced = matPreparedForAdd[random];
	// instanced.gpuMaterial_.Diffuse = glm::vec4(static_cast <float> (rand()) / static_cast <float> (RAND_MAX) * 0.8f + 0.2f,
	// 											static_cast <float> (rand()) / static_cast <float> (RAND_MAX) * 0.8f + 0.2f,
	// 											static_cast <float> (rand()) / static_cast <float> (RAND_MAX) * 0.8f + 0.2f, 1.0);
	//
	// uint32_t newMatId = GetEngine().GetScene().AddMaterial(instanced);

	uint32_t newMatId = matIds_[std::rand() % matIds_.size()];
	newNode->SetMaterial( { newMatId } );
	newNode->SetVisible(true);
	newNode->SetMobility(Assets::Node::ENodeMobility::Dynamic);
	auto id = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(center, 0.2f, JPH::EMotionType::Dynamic);
	newNode->BindPhysicsBody(id);

	GetEngine().GetScene().Nodes().push_back(newNode);
	GetEngine().GetScene().MarkDirty();

	GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 70000.f);
}

void NextRendererGameInstance::DrawSettings()
{
	UserSettings& UserSetting = GetEngine().GetUserSettings();
	
	if (!UserSetting.ShowSettings)
	{
		return;
	}

	const float distance = 10.0f;
	const ImVec2 pos = ImVec2(distance, TITLEBAR_SIZE + distance);
	const ImVec2 posPivot = ImVec2(0.0f, 0.0f);
	ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30,-1));
	ImGui::SetNextWindowBgAlpha(0.9f);
	
	const auto flags =
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("Settings", &UserSetting.ShowSettings, flags))
	{
		// if( ImGui::CollapsingHeader(LOCTEXT("Help"), ImGuiTreeNodeFlags_None) )
		// {
		// 	ImGui::Separator();
		// 	ImGui::BulletText("%s", LOCTEXT("F1: toggle Settings."));
		// 	ImGui::BulletText("%s", LOCTEXT("F2: toggle Statistics."));
		// 	ImGui::BulletText("%s", LOCTEXT("Click: Click Object to Focus."));
		// 	ImGui::BulletText("%s", LOCTEXT("DropFile: if glb file, load it."));
		// 	ImGui::NewLine();
		// }

		if( ImGui::CollapsingHeader(LOCTEXT("Renderer"), ImGuiTreeNodeFlags_DefaultOpen) )
		{
			std::vector<const char*> renderers {"PathTracing", "SoftTracing", "SoftModern", "VoxelTracing"};
			
			ImGui::Text("%s", LOCTEXT("Renderer"));
			
			ImGui::PushItemWidth(-1);
			ImGui::Combo("##RendererList", &UserSetting.RendererType, renderers.data(), static_cast<int>(renderers.size()));
			ImGui::PopItemWidth();
			ImGui::NewLine();
		}
		
		if( ImGui::CollapsingHeader(LOCTEXT("Scene"), ImGuiTreeNodeFlags_DefaultOpen) )
		{
			std::vector<std::string> sceneNames;
			for (const auto& scene : SceneList::AllScenes)
			{
				std::filesystem::path path(scene);
				sceneNames.push_back(path.filename().string());
			}

			std::vector<const char*> scenes;
			for (const auto& scene : sceneNames)
			{
				scenes.push_back(scene.c_str());
			}
			
			std::vector<const char*> camerasList;
			for (const auto& cam : GetEngine().GetScene().GetCameras())
			{
				camerasList.emplace_back(cam.name.c_str());
			}
			
			ImGui::Text("%s", LOCTEXT("Scene"));
			
			ImGui::PushItemWidth(-1);
			if (ImGui::Combo("##SceneList", &UserSetting.SceneIndex, scenes.data(), static_cast<int>(scenes.size())) )
			{
				// Request Scene Load
				GetEngine().RequestLoadScene(SceneList::AllScenes[UserSetting.SceneIndex]);
			}
			ImGui::PopItemWidth();

			int prevCameraIdx = UserSetting.CameraIdx;
			ImGui::Text("%s", LOCTEXT("Camera"));
			ImGui::PushItemWidth(-1);
			ImGui::Combo("##CameraList", &UserSetting.CameraIdx, camerasList.data(), static_cast<int>(camerasList.size()));
			ImGui::PopItemWidth();
			if(prevCameraIdx != UserSetting.CameraIdx)
			{
				GetEngine().GetScene().SetRenderCamera( GetEngine().GetScene().GetCameras()[UserSetting.CameraIdx] );
				modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
			}

			auto& Camera = GetEngine().GetScene().GetRenderCamera();
			ImGui::SliderFloat(LOCTEXT("Aperture"), &Camera.Aperture, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat(LOCTEXT("Focus(cm)"), &Camera.FocalDistance, 0.001f, 1000.0f, "%.3f");
			ImGui::NewLine();
		}

		if( ImGui::CollapsingHeader(LOCTEXT("Ray Tracing"), ImGuiTreeNodeFlags_DefaultOpen) )
		{
			ImGui::Checkbox(LOCTEXT("AntiAlias"), &UserSetting.TAA);
			ImGui::SliderInt(LOCTEXT("Samples"), &UserSetting.NumberOfSamples, 1, 16);
			ImGui::SliderInt(LOCTEXT("TemporalSteps"), &UserSetting.AdaptiveSteps, 2, 64);
			ImGui::Checkbox(LOCTEXT("FastGather"), &UserSetting.FastGather);
			ImGui::SliderInt(LOCTEXT("AmbientSpeed"), &UserSetting.BakeSpeedLevel, 0, 2);

			
			
			ImGui::NewLine();
		}

		if( ImGui::CollapsingHeader(LOCTEXT("Denoiser"), ImGuiTreeNodeFlags_DefaultOpen) )
		{
#if WITH_OIDN
			ImGui::Checkbox("Use OIDN", &UserSetting.Denoiser);
#else
			ImGui::Checkbox(LOCTEXT("Use JBF"), &UserSetting.Denoiser);
			ImGui::SliderFloat(LOCTEXT("DenoiseSigma"), &UserSetting.DenoiseSigma, 0.01f, 2.0f, "%.2f");
			ImGui::SliderFloat(LOCTEXT("DenoiseSigmaLum"), &UserSetting.DenoiseSigmaLum, 0.01f, 50.0f, "%.2f");
			ImGui::SliderFloat(LOCTEXT("DenoiseSigmaNormal"), &UserSetting.DenoiseSigmaNormal, 0.001f, 0.2f, "%.3f");
			ImGui::SliderInt(LOCTEXT("DenoiseSize"), &UserSetting.DenoiseSize, 1, 10);
#endif
			ImGui::NewLine();
		}
		
		if( ImGui::CollapsingHeader(LOCTEXT("Lighting"), ImGuiTreeNodeFlags_None) )
		{
			
			ImGui::Checkbox(LOCTEXT("HasSky"), &GetEngine().GetScene().GetEnvSettings().HasSky);
			if(GetEngine().GetScene().GetEnvSettings().HasSky)
			{
				ImGui::SliderInt(LOCTEXT("SkyIdx"), &GetEngine().GetScene().GetEnvSettings().SkyIdx, 0, 10);
				ImGui::SliderFloat(LOCTEXT("SkyRotation"), &GetEngine().GetScene().GetEnvSettings().SkyRotation, 0.0f, 2.0f, "%.2f");
				ImGui::SliderFloat(LOCTEXT("SkyLum"), &GetEngine().GetScene().GetEnvSettings().SkyIntensity, 0.0f, 1000.0f, "%.0f");
			}
			
			ImGui::Checkbox(LOCTEXT("HasSun"), &GetEngine().GetScene().GetEnvSettings().HasSun);
			if(GetEngine().GetScene().GetEnvSettings().HasSun)
			{
				ImGui::SliderFloat(LOCTEXT("SunRotation"), &GetEngine().GetScene().GetEnvSettings().SunRotation, 0.0f, 2.0f, "%.2f");
				ImGui::SliderFloat(LOCTEXT("SunLum"), &GetEngine().GetScene().GetEnvSettings().SunIntensity, 0.0f, 2000.0f, "%.0f");
			}

			ImGui::SliderFloat(LOCTEXT("PaperWhitNit"), &UserSetting.PaperWhiteNit, 100.0f, 1600.0f, "%.1f");
			ImGui::NewLine();
		}

		if( ImGui::CollapsingHeader(LOCTEXT("Misc"), ImGuiTreeNodeFlags_None) )
		{
			ImGui::Text("%s", LOCTEXT("Profiler"));
			ImGui::Separator();
			ImGui::Checkbox(LOCTEXT("ShowWireframe"), &GetEngine().GetRenderer().showWireframe_);
			ImGui::Checkbox(LOCTEXT("TickPhysics"), &UserSetting.TickPhysics);
			ImGui::Checkbox(LOCTEXT("DebugDraw"), &UserSetting.ShowVisualDebug);
			ImGui::Checkbox(LOCTEXT("DebugDraw_Lighting"), &UserSetting.DebugDraw_Lighting);
			ImGui::Checkbox(LOCTEXT("DisableSpatialReuse"), &UserSetting.DisableSpatialReuse);
			
			ImGui::SliderFloat(LOCTEXT("Time Scaling"), &UserSetting.HeatmapScale, 0.10f, 2.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
			ImGui::NewLine();

			ImGui::Text("%s", LOCTEXT("Performance"));
			ImGui::Separator();
			uint32_t min = 8, max = 32;
			ImGui::SliderScalar(LOCTEXT("Temporal Frames"), ImGuiDataType_U32, &UserSetting.TemporalFrames, &min, &max);		
		}
	}
	ImGui::End();
}


void NextRendererGameInstance::DrawTitleBar()
{
    // 获取窗口的大小
    ImVec2 windowSize = ImGui::GetMainViewport()->Size;
    auto bgColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    bgColor.w = 0.9f;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(windowSize.x, TITLEBAR_SIZE), ImGui::ColorConvertFloat4ToU32(bgColor));

    ImGui::PushFont(bigFont_);

    auto textSize = ImGui::CalcTextSize("gkNextRenderer");
    ImGui::GetForegroundDrawList()->AddText(ImVec2((windowSize.x - textSize.x) * 0.5f, (TITLEBAR_SIZE - textSize.y) * 0.5f), IM_COL32(255, 255, 255, 255), "gkNextRenderer");

    ImGui::PopFont();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::SetNextWindowPos(ImVec2(windowSize.x - TITLEBAR_CONTROL_SIZE, 0), ImGuiCond_Always, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(TITLEBAR_CONTROL_SIZE, TITLEBAR_SIZE));

    ImGui::Begin("TitleBarRight", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);

    if (ImGui::Button(ICON_FA_MINUS, ImVec2(TITLEBAR_SIZE, TITLEBAR_SIZE)))
    {
        GetEngine().RequestMinimize();
    }
    ImGui::SameLine();
    if (ImGui::Button(GetEngine().IsMaximumed() ? ICON_FA_WINDOW_RESTORE : ICON_FA_SQUARE, ImVec2(TITLEBAR_SIZE, TITLEBAR_SIZE)))
    {
        GetEngine().ToggleMaximize();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK, ImVec2(TITLEBAR_SIZE, TITLEBAR_SIZE)))
    {
        GetEngine().RequestClose();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(TITLEBAR_SIZE * 18, TITLEBAR_SIZE));

    ImGui::Begin("TitleBarLeft", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    if (ImGui::Button(ICON_FA_GITHUB, ImVec2(TITLEBAR_SIZE, TITLEBAR_SIZE)))
    {
        NextRenderer::OSCommand("https://github.com/gameknife/gkNextRenderer");
    }
    BUTTON_TOOLTIP(LOCTEXT("Open Project Page in OS Browser"))
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TWITTER, ImVec2(TITLEBAR_SIZE, TITLEBAR_SIZE)))
    {
        NextRenderer::OSCommand("https://x.com/gKNIFE_");
    }
    BUTTON_TOOLTIP(LOCTEXT("Open Twitter Page in OS Browser"))
    ImGui::SameLine();
    ImGui::GetForegroundDrawList()->AddLine(ImGui::GetCursorPos() + ImVec2(4, TITLEBAR_SIZE / 2 - 5), ImGui::GetCursorPos() + ImVec2(4, TITLEBAR_SIZE / 2 + 5), IM_COL32(255, 255, 255, 160), 2.0f);
    ImGui::Dummy(ImVec2(10, 10));
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CAMERA, ImVec2(TITLEBAR_SIZE, TITLEBAR_SIZE)))
    {

    }
    BUTTON_TOOLTIP(LOCTEXT("Take a Screenshot into the screenshots folder"))
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_LIST_CHECK, ImVec2(TITLEBAR_SIZE, TITLEBAR_SIZE)))
	{
		GetEngine().GetUserSettings().ShowSettings = !GetEngine().GetUserSettings().ShowSettings;
	}
	BUTTON_TOOLTIP(LOCTEXT("Take a Screenshot into the screenshots folder"))
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_GAUGE_SIMPLE_HIGH, ImVec2(TITLEBAR_SIZE, TITLEBAR_SIZE)))
	{
		GetEngine().GetUserSettings().ShowOverlay = !GetEngine().GetUserSettings().ShowOverlay;
	}
	BUTTON_TOOLTIP(LOCTEXT("Take a Screenshot into the screenshots folder"))
	ImGui::SameLine();
    ImGui::GetForegroundDrawList()->AddLine(ImGui::GetCursorPos() + ImVec2(4, TITLEBAR_SIZE / 2 - 5), ImGui::GetCursorPos() + ImVec2(4, TITLEBAR_SIZE / 2 + 5), IM_COL32(255, 255, 255, 160), 2.0f);
    ImGui::Dummy(ImVec2(10, 10));
    ImGui::SameLine();
    float deltaSeconds = GetEngine().GetSmoothDeltaSeconds();
    ImGui::SameLine();
    ImGui::SetCursorPosY((TITLEBAR_SIZE - ImGui::GetTextLineHeight()) / 2);
    ImGui::TextUnformatted(fmt::format("{:.0f}fps", 1.0f / deltaSeconds).c_str());
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

