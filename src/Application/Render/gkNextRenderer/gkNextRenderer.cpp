#include "gkNextRenderer.hpp"

#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <tuple>

#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Modules/SceneContent/SceneList.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/ScreenShotService.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Modules/NextUI/ImGuiScaling.hpp"
#include "Engine/Rendering/RendererChoices.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/DevTools/UI/DeveloperStatusBar.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"
#include "Modules/DevTools/Command/DuplicateNodesCommand.hpp"
#include "Modules/LiveCoding/LiveCodingModule.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Engine/Utilities/Format.hpp"
#include "Engine/Utilities/AboutDialog.hpp"
#include "Engine/Utilities/ImGui.hpp"

#include <SDL3/SDL_misc.h>
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Modules/DevTools/UiDevPanels.hpp"
#include "Engine/Vulkan/Allocator.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Modules/RenderViews/OffscreenRenderViewController.hpp"
#if GK_WITH_VITURE
#include "Modules/DevTools/VitureDebugPanel.hpp"
#include "Modules/NextViture/VitureModule.hpp"
#endif
#include "Application/Common/DemoScenes.hpp"


namespace
{
using Utilities::FormatBytes;
using NextUI::Theme::DrawFlatViewportButton;
using NextUI::Theme::DrawViewportComboOption;
using NextUI::Theme::PopViewportPopupStyle;
using NextUI::Theme::PopViewportToolbarStyle;
using NextUI::Theme::PushViewportPopupStyle;
using NextUI::Theme::PushViewportToolbarStyle;

constexpr uint32_t dropSphereGridSize = 20;
constexpr uint32_t dropSphereCount = dropSphereGridSize * dropSphereGridSize;

constexpr float shootHintDurationSeconds = 3.5f;

// Everything the Space shortcut puts into the world -- the projectile, the weapon it leaves and
// the muzzle velocity -- is derived from a single scene scale, in the reference units below.
// playground.glb, whose scene AABB has a longest side of about 29 m, is the reference. The
// camera only decides how far away the weapon is held, never how big any of it is.
constexpr float launchReferenceSceneExtent = 28.0f;
constexpr float launchMinSceneScale = 0.35f;
constexpr float launchMaxSceneScale = 2.2f;
constexpr float projectileBaseSize = 0.22f;   // cube edge length at scene scale 1
constexpr float projectileBaseSpeed = 34.0f;  // m/s at scene scale 1
constexpr float projectileSpinPitch = 1.6f;   // rad/s about the view right axis
constexpr float projectileSpinYaw = 0.7f;     // rad/s about the view up axis

// Camera-relative weapon. The barrel is four walls around an open bore just wider than the
// projectile, and the projectile is spawned centred on the muzzle mouth, so a shell is always
// exactly as wide as the hole it comes out of. A solid barrel pointed down the view axis is seen
// almost end on and reads as a featureless slab; the hole is what makes it a muzzle.
//
// The whole rig therefore takes the scene scale and nothing else, exactly like the shell it
// fires, and the field of view is answered by moving it along the view axis instead of resizing
// it. Scaling the rig by the field of view as well used to shrink the bore, and with it the
// shell, by another factor of two in a narrow view -- which cancelled the scene scale outright
// and fired the same 13 cm cube in a 10 m room and in a 29 m playground.
constexpr float muzzleReferenceFovDegrees = 60.0f;
constexpr float muzzleDepthScaleMin = 0.5f;
constexpr float muzzleDepthScaleMax = 4.0f;
constexpr float muzzleBoreHalfWidth = projectileBaseSize * 0.5f + 0.01f;
constexpr float muzzleBarrelHalfWidth = muzzleBoreHalfWidth + 0.045f;
constexpr float muzzleBarrelWallHalfThickness = (muzzleBarrelHalfWidth - muzzleBoreHalfWidth) * 0.5f;
constexpr float muzzleBarrelWallOffset = (muzzleBarrelHalfWidth + muzzleBoreHalfWidth) * 0.5f;
constexpr float muzzleBarrelHalfLength = 0.52f;
constexpr float muzzleReceiverHalfWidth = 0.17f;
constexpr float muzzleReceiverHalfHeight = 0.13f;
constexpr float muzzleReceiverHalfLength = 0.30f;
constexpr float muzzleReceiverOffsetY = -0.30f;
constexpr float muzzleReceiverOffsetZ = 0.30f;
constexpr float muzzleRigRearExtent = muzzleReceiverOffsetZ + muzzleReceiverHalfLength;
constexpr float muzzleRigNearMargin = 0.30f;
constexpr float muzzleDepth = 1.75f;
constexpr float muzzleRightOffset = 0.77f;
constexpr float muzzleDownOffset = 0.55f;

float GetLaunchSceneScale(const Assets::Scene& scene)
{
    const glm::vec3 boundsSize = scene.GetSceneAABBMax() - scene.GetSceneAABBMin();
    const float sceneExtent = std::max({boundsSize.x, boundsSize.y, boundsSize.z});
    if (!std::isfinite(sceneExtent) || sceneExtent <= 0.0f)
    {
        return 1.0f;
    }

    // Square root rather than the raw ratio: a kilometre-wide city tile is a hundred times
    // playground, and a projectile a hundred times bigger would be a building. The continuous
    // curve also removes the visible jump the previous half/double thresholds produced between
    // two scenes of almost the same size.
    return std::clamp(std::sqrt(sceneExtent / launchReferenceSceneExtent),
                      launchMinSceneScale, launchMaxSceneScale);
}

// Field-of-view compensation for the weapon, applied to its distance from the camera alone,
// since its size is now spoken for: a narrow view needs the rig further out to keep the same
// share of the frame, a wide one needs it closer. The tangent ratio is compensated in full, so
// the weapon and the shell leaving it hold exactly the same on-screen size and position at every
// field of view, and only their world size follows the scene. Distance is the one lever left --
// shrinking the rig instead would shrink the bore, and the bore is the shell -- so it is simply
// clamped at both ends rather than softened: far enough out and the weapon starts picking up
// scene geometry in front of it, close enough in and its rear reaches the camera.
float GetMuzzleDepthScale(float fieldOfViewDegrees)
{
    const float halfFovTangent = glm::tan(glm::radians(fieldOfViewDegrees) * 0.5f);
    const float referenceTangent = glm::tan(glm::radians(muzzleReferenceFovDegrees) * 0.5f);
    return std::clamp(referenceTangent / halfFovTangent,
                      muzzleDepthScaleMin, muzzleDepthScaleMax);
}

uint32_t HashUint(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}
float HashToUnitFloat(uint32_t value)
{
    return static_cast<float>(HashUint(value) >> 8) * (1.0f / 16777216.0f);
}

glm::vec3 HsvToRgb(float hue, float saturation, float value)
{
    const float scaledHue = hue * 6.0f;
    const int sector = static_cast<int>(std::floor(scaledHue)) % 6;
    const float fraction = scaledHue - std::floor(scaledHue);
    const float low = value * (1.0f - saturation);
    const float falling = value * (1.0f - saturation * fraction);
    const float rising = value * (1.0f - saturation * (1.0f - fraction));

    switch (sector)
    {
    case 0: return {value, rising, low};
    case 1: return {falling, value, low};
    case 2: return {low, value, rising};
    case 3: return {low, falling, value};
    case 4: return {rising, low, value};
    default: return {value, low, falling};
    }
}

std::vector<uint32_t> SelectedNodeIds(Assets::Scene& scene)
{
    std::vector<uint32_t> ids = scene.GetSelectedIds();
    if (ids.empty())
    {
        const uint32_t selectedId = scene.GetSelectedId();
        if (selectedId != static_cast<uint32_t>(-1))
        {
            ids.push_back(selectedId);
        }
    }
    return ids;
}

} // namespace

// should use 1em instead of 1px
constexpr float constTitlebarSize = 48;
constexpr float constTitlebarRightInfoWidth = 0;
constexpr float constModeRailWidth = 56;
constexpr float constModeRailButtonSize = 40;

float TitlebarSize = constTitlebarSize;
float TitlebarRightInfoWidth = constTitlebarRightInfoWidth;
float ModeRailWidth = constModeRailWidth;
float ModeRailButtonSize = constModeRailButtonSize;

static void UpdateUiScaledMetrics()
{
    float scale = 1.0f;

#if !ANDROID
    if (ImGui::GetCurrentContext() != nullptr)
    {
        const float fontSize = ImGui::GetFontSize();
        if (fontSize > 0.0f)
        {
            constexpr float referenceFontSize = 16.0f;
            scale *= fontSize / referenceFontSize;
        }
    }
#endif

    TitlebarSize = constTitlebarSize * scale;
    TitlebarRightInfoWidth = constTitlebarRightInfoWidth * scale;
    ModeRailWidth = constModeRailWidth * scale;
    ModeRailButtonSize = constModeRailButtonSize * scale;
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    Modules::LDraw::Register();
    Modules::Scad::Register();
    AppCommon::RegisterDemoScenes();
    return std::make_unique<NextRendererGameInstance>(config, options, engine);
}

NextRendererGameInstance::NextRendererGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    config.HideTitleBar = true;
}

void NextRendererGameInstance::OnInit()
{
    // Reference comparison is an auxiliary multi-view render path. Keep the
    // provider installed even when the application starts in normal mode so
    // r.reference can switch it on without restarting the process.
    RenderViews::OffscreenViews(GetEngine().GetRenderer());

    // Keep the viewport clean on startup. The Stats button remains available in
    // the bottom status bar for sessions that need the diagnostic overlay.
    GetEngine().GetUserSettings().ShowOverlay = false;
    GetEngine().GetShowFlags().DebugCVarPanel = false;

    // First-run scene. CornellBox.proc is a near-square box that letterboxes badly on a
    // 16:9 window; playground fills the frame and shows GI, shadows and materials at once.
    std::string initializedScene = "assets/models/playground.glb";
    if (!GOption->SceneName.empty())
    {
        initializedScene = GOption->SceneName;
    }

    const std::filesystem::path initializedPath(initializedScene);
    for (int sceneIndex = 0;
         sceneIndex < static_cast<int>(Runtime::Scene::SceneList::AllScenes.size());
         ++sceneIndex)
    {
        const std::filesystem::path candidatePath(Runtime::Scene::SceneList::AllScenes[sceneIndex]);
        if (candidatePath == initializedPath || candidatePath.filename() == initializedPath.filename())
        {
            GetEngine().GetUserSettings().SceneIndex = sceneIndex;
            break;
        }
    }

    GetEngine().RequestLoadScene({.filename = initializedScene});

#if GK_WITH_VITURE
    if (GOption != nullptr && GOption->ArMode)
    {
        headPoseTracker_ = Modules::Viture::CreateHeadPoseTracker(
            GOption->ArDof == 6, static_cast<double>(GOption->ArPredictionMs) * 0.001);
        if (!headPoseTracker_->Start())
        {
            SPDLOG_ERROR("AR mode could not start {}: {}", headPoseTracker_->Name(), headPoseTracker_->Status());
        }
    }
#endif
}

void NextRendererGameInstance::OnTick(double deltaSeconds)
{
    shootHintRemainingSeconds_ = std::max(
        0.0f, shootHintRemainingSeconds_ - static_cast<float>(std::max(0.0, deltaSeconds)));

    bool arMoving = false;
#if GK_WITH_VITURE
    arMoving = UpdateArTracking(deltaSeconds);
#endif
    if (playbackPaused_ && !stepRequested_)
    {
        GetEngine().SetProgressiveRendering(GetEngine().GetUserSettings().ProgressiveRender && !arMoving);
        return;
    }

    const bool moving = modelViewController_.UpdateCamera(10.0f, deltaSeconds) || arMoving;
    const bool fovChanged = std::abs(modelViewController_.FieldOfView() - muzzleFieldOfView_) > 0.001f;
    if ((moving || fovChanged) && muzzleNode_)
    {
        RefreshMuzzleTransform();
    }
    GetEngine().SetProgressiveRendering(GetEngine().GetUserSettings().ProgressiveRender && !moving);
    stepRequested_ = false;
}

void NextRendererGameInstance::OnDestroy()
{
#if GK_WITH_VITURE
    if (headPoseTracker_)
    {
        headPoseTracker_->Stop();
    }
#endif
}

#if GK_WITH_VITURE
bool NextRendererGameInstance::UpdateArTracking(const double deltaSeconds)
{
    if (!headPoseTracker_)
    {
        return false;
    }

    const std::optional<Modules::Viture::FHeadPose> pose = headPoseTracker_->PollPose();
    latestArPose_ = pose;
    const float smoothingHz = GOption != nullptr ? GOption->ArSmoothingHz : 0.0f;
    return pose.has_value() && arCamera_.Update(*pose, deltaSeconds, smoothingHz);
}

void NextRendererGameInstance::DrawVitureDebugPanel()
{
    if (!headPoseTracker_)
    {
        return;
    }

    const float worldUnitsPerMeter = GOption != nullptr ? GOption->ArWorldUnitsPerMeter : 1.0f;
    const float predictionMs = GOption != nullptr ? GOption->ArPredictionMs : 20.0f;
    const float smoothingHz = GOption != nullptr ? GOption->ArSmoothingHz : 0.0f;
    DevTools::FVitureDebugPanelData data{};
    data.tracker = headPoseTracker_.get();
    data.pose = latestArPose_.has_value() ? &latestArPose_.value() : nullptr;
    const std::optional<glm::quat> relativeOrientation = arCamera_.RelativeOrientation();
    const std::optional<glm::vec3> cameraEulerDegrees = latestArPose_.has_value() && relativeOrientation.has_value()
        ? std::optional<glm::vec3>(glm::degrees(glm::eulerAngles(*relativeOrientation)))
        : std::nullopt;
    data.cameraEulerDegrees = cameraEulerDegrees.has_value() ? &cameraEulerDegrees.value() : nullptr;
    data.sixDof = GOption == nullptr || GOption->ArDof == 6;
    data.worldUnitsPerMeter = worldUnitsPerMeter;
    data.predictionMs = predictionMs;
    data.pollHz = 25.0f;
    data.smoothingHz = smoothingHz;
    data.recenter = [this]() { return arCamera_.Recenter(); };
    data.restart = [this]()
    {
        latestArPose_.reset();
        arCamera_ = {};
        const bool started = headPoseTracker_ != nullptr && headPoseTracker_->Start();
        if (!started && headPoseTracker_ != nullptr)
        {
            SPDLOG_ERROR("AR mode could not restart {}: {}", headPoseTracker_->Name(), headPoseTracker_->Status());
        }
        return started;
    };
    DevTools::DrawVitureDebugPanel(vitureDebugPanelVisible_, data, GetGraphicsDebugPanelTopOffset());
}
#endif

std::vector<Assets::FMaterial> MatPreparedForAdd;

void NextRendererGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
    std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
    std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
{
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0,0,0), 0.2f));
    modelId_ = static_cast<uint32_t>(models.size() - 1);
    
    constexpr float projectileHalfSize = projectileBaseSize * 0.5f;
    models.push_back(Assets::FProcModel::CreateBox(
        glm::vec3(-projectileHalfSize), glm::vec3(projectileHalfSize)));
    boxModelId_ = static_cast<uint32_t>(models.size() - 1);

    // A camera-relative weapon gives the Space shortcut a visible point of reference. Its parts
    // are added as regular render nodes after the scene is loaded, so they do not affect the
    // scene AABB the launch scale is read from. Local -Z is the muzzle mouth. One horizontal and
    // one vertical wall model are instanced twice each to box in the bore.
    models.push_back(Assets::FProcModel::CreateBox(
        glm::vec3(-muzzleBarrelHalfWidth, -muzzleBarrelWallHalfThickness, -muzzleBarrelHalfLength),
        glm::vec3(muzzleBarrelHalfWidth, muzzleBarrelWallHalfThickness, muzzleBarrelHalfLength)));
    viewBarrelWallHModelId_ = static_cast<uint32_t>(models.size() - 1);

    models.push_back(Assets::FProcModel::CreateBox(
        glm::vec3(-muzzleBarrelWallHalfThickness, -muzzleBoreHalfWidth, -muzzleBarrelHalfLength),
        glm::vec3(muzzleBarrelWallHalfThickness, muzzleBoreHalfWidth, muzzleBarrelHalfLength)));
    viewBarrelWallVModelId_ = static_cast<uint32_t>(models.size() - 1);

    models.push_back(Assets::FProcModel::CreateBox(
        glm::vec3(-muzzleReceiverHalfWidth, -muzzleReceiverHalfHeight, -muzzleReceiverHalfLength),
        glm::vec3(muzzleReceiverHalfWidth, muzzleReceiverHalfHeight, muzzleReceiverHalfLength)));
    viewReceiverModelId_ = static_cast<uint32_t>(models.size() - 1);

    matIds_.clear();

    matIds_.push_back(Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1,1,1)));
    MatPreparedForAdd.push_back(materials.back());
    MatPreparedForAdd.push_back({Assets::Material::Metallic(glm::vec3(0.8,0.8,0.8), 0.1f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
    MatPreparedForAdd.push_back({Assets::Material::Dielectric(1.5f, 0.0f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
    MatPreparedForAdd.push_back({Assets::Material::Mixture(glm::vec3(1.0f, 0.3f, 0.3f), 0.1f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));

    // Gunmetal for the weapon. The white lambertian the barrel used to borrow read as a loose
    // slab of scene geometry rather than something the camera is holding.
    materials.push_back({Assets::Material::Mixture(glm::vec3(0.30f, 0.32f, 0.37f), 0.34f), "viewWeapon"});
    viewWeaponMatId_ = static_cast<uint32_t>(materials.size() - 1);

    dropSphereMatIds_.clear();
    dropSphereMatIds_.reserve(dropSphereCount);
    dropSphereSequence_ = 0;
    for (uint32_t index = 0; index < dropSphereCount; ++index)
    {
        const float hue = HashToUnitFloat(index ^ 0xa511e9b3u);
        const float saturation = 0.25f + 0.25f * HashToUnitFloat(index ^ 0x63d83595u);
        const float value = 0.55f + 0.43f * HashToUnitFloat(index ^ 0xc2b2ae35u);
        const float roughness = 0.04f + 0.92f * HashToUnitFloat(index ^ 0x27d4eb2fu);
        const glm::vec3 color = HsvToRgb(hue, saturation, value);

        Assets::Material material;
        switch (HashUint(index ^ 0x9e3779b9u) % 5)
        {
        case 0: material = Assets::Material::Lambertian(color); break;
        case 1: material = Assets::Material::Metallic(color, roughness); break;
        case 3: material = Assets::Material::Dielectric(1.5f, 0.0f); break;
        default: material = Assets::Material::Mixture(color, roughness); break;
        }

        materials.push_back({material, fmt::format("dropSphere_{:03}", index)});
        dropSphereMatIds_.push_back(static_cast<uint32_t>(materials.size() - 1));
    }
}

void NextRendererGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();
    launchSceneScale_ = GetLaunchSceneScale(GetEngine().GetScene());
    modelViewController_.Reset( GetEngine().GetScene().GetRenderCamera() );
    shootHintRemainingSeconds_ = shootHintDurationSeconds;

    Assets::Scene& scene = GetEngine().GetScene();
    viewWeaponParts_ = {};
    muzzleNode_.reset();
    // Scene validation captures must not be covered by the debug weapon.
    if (!GetEngine().GetOptions().AgentValidation && viewReceiverModelId_ < scene.Models().size() && !matIds_.empty())
    {
        const auto createPart = [&](std::string_view name, uint32_t modelId, const glm::vec3& localOffset)
        {
            FViewWeaponPart part;
            part.localOffset = localOffset;
            part.node = Assets::SceneBuilder::CreateRenderNode(
                name, glm::vec3(0.0f), glm::vec3(1.0f),
                static_cast<uint32_t>(scene.Nodes().size()), modelId, viewWeaponMatId_, true,
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
            // No shadow: a weapon-shaped shadow sweeping the scene as the camera turns reads as
            // scene geometry gone wrong. Indirect light stays on, or the weapon is a black slab in
            // any scene whose fill comes from the sky rather than from the sun.
            if (auto* render = part.node->GetComponent<Runtime::RenderComponent>())
            {
                render->SetCastShadows(false);
            }
            scene.AddNode(part.node);
            return part;
        };

        viewWeaponParts_[0] = createPart("viewBarrelTop", viewBarrelWallHModelId_,
                                         glm::vec3(0.0f, muzzleBarrelWallOffset, 0.0f));
        viewWeaponParts_[1] = createPart("viewBarrelBottom", viewBarrelWallHModelId_,
                                         glm::vec3(0.0f, -muzzleBarrelWallOffset, 0.0f));
        viewWeaponParts_[2] = createPart("viewBarrelLeft", viewBarrelWallVModelId_,
                                         glm::vec3(-muzzleBarrelWallOffset, 0.0f, 0.0f));
        viewWeaponParts_[3] = createPart("viewBarrelRight", viewBarrelWallVModelId_,
                                         glm::vec3(muzzleBarrelWallOffset, 0.0f, 0.0f));
        viewWeaponParts_[4] = createPart("viewWeaponReceiver", viewReceiverModelId_,
                                         glm::vec3(0.0f, muzzleReceiverOffsetY, muzzleReceiverOffsetZ));
        muzzleNode_ = viewWeaponParts_[0].node;
        RefreshMuzzleTransform();
    }

    GetEngine().GetScene().PlayAllTracks();
}

void NextRendererGameInstance::OnPreConfigUI()
{
    NextGameInstanceBase::OnPreConfigUI();
}

bool NextRendererGameInstance::OnRenderUI()
{
    FGameUiFrameContext context;
    const auto& swapChain = GetEngine().GetRenderer().SwapChain();
    context.surfaceKind = FGameUiFrameContext::ESurfaceKind::MainWindow;
    context.framebufferExtent = swapChain.OutputExtent();
    context.viewCamera = &GetEngine().GetScene().GetRenderCamera();
    context.allowWindowCommands = true;
    return DrawRendererUi(context, mainUiState_);
}

bool NextRendererGameInstance::OnRenderUI(const FGameUiFrameContext& context)
{
    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::RemoteView)
    {
        return DrawRendererUi(context, GetRemoteUiState(context.sessionId));
    }
    return OnRenderUI();
}

NextUI::FUiFrameResult NextRendererGameInstance::RenderUiFrame(const FGameUiFrameContext& context)
{
    return NextUI::FUiFrameResult::FromLegacyHandled(OnRenderUI(context));
}

void NextRendererGameInstance::OnRemoteUiSessionClosed(std::string_view sessionId)
{
    remoteUiStates_.erase(std::string(sessionId));
}

NextRendererGameInstance::FRendererUiState& NextRendererGameInstance::GetRemoteUiState(std::string_view sessionId)
{
    return remoteUiStates_[std::string(sessionId)];
}

bool NextRendererGameInstance::DrawRendererUi(const FGameUiFrameContext& context, FRendererUiState& uiState)
{
    if ((isTakingScreenshot_ || isRecordingVideo_) &&
        context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow)
    {
        return true;
    }

    UpdateUiScaledMetrics();

    if (uiState.workMode != uiState.lastWorkMode)
    {
        auto& showFlags = GetEngine().GetShowFlags();
        Runtime::Config::UserSettings& userSettings = GetEngine().GetUserSettings();
        switch (uiState.workMode)
        {
        case EWorkMode::Render:
            uiState.showSettings = false;
            uiState.showCheatSheet = false;
            DevTools::FUiDevPanels::Get().SetMemoryStatisticsOpen(false);
            userSettings.ShowOverlay = false;
            showFlags.DebugCVarPanel = false;
            break;
        case EWorkMode::Detail:
            uiState.showSettings = true;
            uiState.showCheatSheet = false;
            DevTools::FUiDevPanels::Get().SetMemoryStatisticsOpen(false);
            userSettings.ShowOverlay = false;
            showFlags.DebugCVarPanel = false;
            break;
        case EWorkMode::Profile:
            uiState.showSettings = false;
            uiState.showCheatSheet = false;
            DevTools::FUiDevPanels::Get().SetMemoryStatisticsOpen(true);
            userSettings.ShowOverlay = true;
            showFlags.DebugCVarPanel = false;
            break;
        case EWorkMode::CVar:
            uiState.showSettings = false;
            uiState.showCheatSheet = false;
            DevTools::FUiDevPanels::Get().SetMemoryStatisticsOpen(false);
            userSettings.ShowOverlay = false;
            showFlags.DebugCVarPanel = true;
            break;
        default: break;
        }
        uiState.lastWorkMode = uiState.workMode;
    }
    else if (uiState.workMode == EWorkMode::CVar &&
             !GetEngine().GetShowFlags().DebugCVarPanel)
    {
        uiState.workMode = EWorkMode::Render;
        uiState.lastWorkMode = EWorkMode::Count;
    }
    else if (uiState.workMode == EWorkMode::Detail && !uiState.showSettings)
    {
        uiState.workMode = EWorkMode::Render;
        uiState.lastWorkMode = EWorkMode::Count;
    }

    DrawTitleBar(context, uiState);
#if GK_WITH_VITURE
    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow)
    {
        DrawVitureDebugPanel();
    }
#endif
    DrawModeRail(uiState);
    DrawSettings(uiState);
    DrawViewportTopBar(context, uiState);
    DrawViewportCheatSheet(uiState);
    DrawShootHint(context);
    DrawBottomStatusBar();
    Utilities::UI::ShowAboutDialog(uiState.showAbout);

    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow && ImGui::GetCurrentContext() != nullptr)
    {
        auto& swapChain = GetEngine().GetRenderer().SwapChain();
        const auto offset = swapChain.OutputOffset();
        const auto extent = swapChain.OutputExtent();
        const NextUI::Scaling::FViewportRect viewport = NextUI::Scaling::MainFramebufferToImGuiViewport(
            ImVec2(static_cast<float>(offset.x), static_cast<float>(offset.y)),
            ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height)));
        uiState.gizmoController.Draw(GetEngine(),
            glm::vec2(viewport.Position.x, viewport.Position.y),
            glm::vec2(viewport.Size.x, viewport.Size.y));
    }
    if (GOption->ReferenceMode)
    {
        ImGuiIO& io = ImGui::GetIO();
        const auto viewport = io.DisplaySize;
        static constexpr std::array<const char*, 4> rendererNames{
            "SoftwareModern", "SoftwareTracing", "SoftwareModernNoAmbient", "PathTracing"};
        const std::array<ImVec2, rendererNames.size()> labelPositions{
            ImVec2(viewport.x * 0.25f, viewport.y * 0.45f),
            ImVec2(viewport.x * 0.75f, viewport.y * 0.45f),
            ImVec2(viewport.x * 0.25f, viewport.y * 0.95f),
            ImVec2(viewport.x * 0.75f, viewport.y * 0.95f)
        };
        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoFocusOnAppearing;

        for (size_t index = 0; index < rendererNames.size(); ++index)
        {
            ImGui::SetNextWindowPos(labelPositions[index], ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.5f);

            auto windowName = fmt::format("RendererName{}", index);
            if (ImGui::Begin(windowName.c_str(), nullptr, windowFlags))
            {
                ImGui::TextUnformatted(rendererNames[index]);
            }
            ImGui::End();
        }
    }
    return false;
}

void NextRendererGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();
}

void NextRendererGameInstance::DrawShootHint(const FGameUiFrameContext& context)
{
    if (context.surfaceKind != FGameUiFrameContext::ESurfaceKind::MainWindow ||
        shootHintRemainingSeconds_ <= 0.0f || ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float scale = std::max(0.75f, ImGui::GetFontSize() / 16.0f);
    const float elapsed = shootHintDurationSeconds - shootHintRemainingSeconds_;
    const float fadeIn = std::clamp(elapsed / 0.18f, 0.0f, 1.0f);
    const float fadeOut = std::clamp(shootHintRemainingSeconds_ / 0.65f, 0.0f, 1.0f);
    const float alpha = fadeIn * fadeOut;
    if (alpha <= 0.0f)
    {
        return;
    }

    constexpr const char* prefix = "Press";
    constexpr const char* key = "SPACE";
    constexpr const char* suffix = "to shoot a cube";
    const ImVec2 prefixSize = ImGui::CalcTextSize(prefix);
    const ImVec2 keySize = ImGui::CalcTextSize(key);
    const ImVec2 suffixSize = ImGui::CalcTextSize(suffix);
    const float gap = 8.0f * scale;
    const float paddingX = 16.0f * scale;
    const float paddingY = 9.0f * scale;
    const float keyPaddingX = 11.0f * scale;
    const float keyPaddingY = 5.0f * scale;
    const float keyWidth = keySize.x + keyPaddingX * 2.0f;
    const float keyHeight = keySize.y + keyPaddingY * 2.0f;
    const float panelWidth = paddingX * 2.0f + prefixSize.x + gap + keyWidth + gap + suffixSize.x;
    const float panelHeight = paddingY * 2.0f + std::max(keyHeight, ImGui::GetFontSize());
    const ImVec2 panelCenter(
        viewport->Pos.x + viewport->Size.x * 0.5f,
        viewport->Pos.y + viewport->Size.y - 92.0f * scale);
    const ImVec2 panelMin(panelCenter.x - panelWidth * 0.5f, panelCenter.y - panelHeight * 0.5f);
    const ImVec2 panelMax(panelCenter.x + panelWidth * 0.5f, panelCenter.y + panelHeight * 0.5f);
    const float textY = panelCenter.y - ImGui::GetFontSize() * 0.5f;

    ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
    drawList->AddRectFilled(
        panelMin, panelMax,
        NextUI::Theme::ColorU32(NextUI::Theme::EColor::SurfaceElevated, 0.94f * alpha),
        8.0f * scale);
    drawList->AddRect(
        panelMin, panelMax,
        NextUI::Theme::ColorU32(NextUI::Theme::EColor::BorderStrong, 0.9f * alpha),
        8.0f * scale, 0, 1.0f * scale);

    float cursorX = panelMin.x + paddingX;
    drawList->AddText(
        ImVec2(cursorX, textY),
        NextUI::Theme::ColorU32(NextUI::Theme::EColor::Text, alpha), prefix);
    cursorX += prefixSize.x + gap;

    const ImVec2 keyMin(cursorX, panelCenter.y - keyHeight * 0.5f);
    const ImVec2 keyMax(keyMin.x + keyWidth, keyMin.y + keyHeight);
    drawList->AddRectFilled(
        keyMin, keyMax,
        NextUI::Theme::ColorU32(NextUI::Theme::EColor::Accent, 0.95f * alpha),
        5.0f * scale);
    drawList->AddRect(
        keyMin, keyMax,
        NextUI::Theme::ColorU32(NextUI::Theme::EColor::AccentHover, alpha),
        5.0f * scale, 0, 1.0f * scale);
    drawList->AddText(
        ImVec2(keyMin.x + keyPaddingX, keyMin.y + keyPaddingY),
        NextUI::Theme::ColorU32(NextUI::Theme::EColor::Text, alpha), key);
    cursorX = keyMax.x + gap;
    drawList->AddText(
        ImVec2(cursorX, textY),
        NextUI::Theme::ColorU32(NextUI::Theme::EColor::TextMuted, alpha), suffix);
}

void NextRendererGameInstance::UpdateMuzzleTransform()
{
    if (!muzzleNode_)
    {
        return;
    }

    const glm::vec3 forward = glm::normalize(modelViewController_.GetForward());
    const glm::vec3 right = glm::normalize(modelViewController_.GetRight());
    const glm::vec3 up = glm::normalize(modelViewController_.GetUp());
    const glm::quat yawOffset = glm::angleAxis(glm::radians(muzzleYawOffsetDegrees_), up);
    const glm::quat pitchOffset = glm::angleAxis(glm::radians(muzzlePitchOffsetDegrees_), right);
    const glm::quat aimOffset = glm::normalize(pitchOffset * yawOffset);
    const glm::vec3 muzzleForward = glm::normalize(aimOffset * forward);
    const glm::vec3 muzzleRight = glm::normalize(aimOffset * right);
    const glm::vec3 muzzleUp = glm::normalize(aimOffset * up);
    const glm::mat3 cameraBasis(muzzleRight, muzzleUp, -muzzleForward);
    const glm::quat rotation = glm::normalize(glm::quat_cast(cameraBasis));
    const float fieldOfView = std::clamp(modelViewController_.FieldOfView(), 5.0f, 170.0f);
    const float rigScale = launchSceneScale_;
    // The rig extends backwards from the barrel, and a wide field of view pulls it closer, so its
    // rear must not be allowed to pass through the camera: a ray that starts inside an opaque box
    // fills the frame with its inner walls.
    const float depth = std::max(muzzleDepth * rigScale * GetMuzzleDepthScale(fieldOfView),
                                 (muzzleRigRearExtent + muzzleRigNearMargin) * rigScale);
    const glm::vec3 origin = modelViewController_.GetPosition() +
        right * (muzzleRightOffset * rigScale) + up * (-muzzleDownOffset * rigScale) +
        forward * depth;

    muzzleFieldOfView_ = fieldOfView;
    muzzleOrigin_ = origin;
    muzzleRotation_ = rotation;
    muzzleRigScale_ = rigScale;

    for (const FViewWeaponPart& part : viewWeaponParts_)
    {
        if (part.node)
        {
            part.node->SetTransform(origin + rotation * (part.localOffset * rigScale), rotation,
                                    glm::vec3(rigScale));
        }
    }

    // Scene node proxies are prepared asynchronously and normally become visible one frame
    // later. The backup vector is the proxy set that this frame will upload, so patch just the
    // camera-relative weapon here instead of synchronously rebuilding every scene node.
    Assets::Scene& scene = GetEngine().GetScene();
    bool patchedProxy = false;
    for (auto& proxy : scene.GetNodeProxies())
    {
        for (const FViewWeaponPart& part : viewWeaponParts_)
        {
            if (!part.node || proxy.instanceId != part.node->GetInstanceId())
            {
                continue;
            }

            const glm::mat4 worldTransform = part.node->WorldTransform();
            proxy.combinedPrevTS = proxy.worldTS * glm::inverse(worldTransform);
            proxy.worldTS = worldTransform;
            patchedProxy = true;
            break;
        }
    }
    if (patchedProxy)
    {
        scene.RequestGpuNodeProxyUpload();
    }
}

glm::vec3 NextRendererGameInstance::MuzzleMouthPosition() const
{
    return muzzleOrigin_ + muzzleRotation_ * glm::vec3(0.0f, 0.0f, -muzzleBarrelHalfLength * muzzleRigScale_);
}

glm::vec3 NextRendererGameInstance::MuzzleDirection() const
{
    return glm::normalize(muzzleRotation_ * glm::vec3(0.0f, 0.0f, -1.0f));
}

void NextRendererGameInstance::RefreshMuzzleTransform()
{
    if (!muzzleNode_)
    {
        return;
    }

    UpdateMuzzleTransform();
    GetEngine().GetScene().MarkTransformDirty();
}

void NextRendererGameInstance::RequestScreenshot(bool openFolder, const std::string& tag)
{
    if (isTakingScreenshot_ || GetEngine().GetScreenShotService().IsBusy())
    {
        return;
    }

    isTakingScreenshot_ = true;
    Runtime::IScreenShotService::FRequest request;
    request.tag = tag;
    request.onCompleted = [this, openFolder](const std::string&) {
        if (openFolder)
        {
            const std::string folderPath = GetEngine().GetScreenShotService().GetDirectory();
            GetEngine().GetScreenShotService().EnsureDirectory();
            NextRenderer::OSCommand(folderPath.c_str());
        }
        isTakingScreenshot_ = false;
    };

    if (!GetEngine().GetScreenShotService().Request(std::move(request)))
    {
        isTakingScreenshot_ = false;
    }
}

void NextRendererGameInstance::RequestThreeSecondVideo(
    const Runtime::IScreenShotService::EVideoOutputScale outputScale)
{
    if (isTakingScreenshot_ || isRecordingVideo_ || GetEngine().GetScreenShotService().IsBusy())
    {
        return;
    }

    isRecordingVideo_ = true;
    Runtime::IScreenShotService::FThreeSecondVideoRequest request;
    // Without ffmpeg only the libwebp path can produce output; asking for Both would
    // just log an encoding error for the GIF half.
    request.format = GetEngine().GetScreenShotService().IsGifEncodingAvailable()
        ? Runtime::IScreenShotService::EAnimationFormat::Both
        : Runtime::IScreenShotService::EAnimationFormat::AnimatedWebp;
    request.outputScale = outputScale;
    request.onCaptureFinished = [this]()
    {
        isRecordingVideo_ = false;
    };
    request.onCompleted = [](const std::string& path)
    {
        if (path.empty())
        {
            spdlog::error("Three-second video recording failed");
        }
        else
        {
            std::filesystem::path webpPath(path);
            webpPath.replace_extension(".webp");
            spdlog::info("Three-second GIF/WebP saved: {} and {}", path, webpPath.string());
        }
    };

    if (!GetEngine().GetScreenShotService().RequestThreeSecondVideo(std::move(request)))
    {
        isRecordingVideo_ = false;
    }
}

void NextRendererGameInstance::DrawVideoCaptureMenuItems()
{
    const auto outputScaleLabel = [](const Runtime::IScreenShotService::EVideoOutputScale outputScale)
    {
        switch (outputScale)
        {
        case Runtime::IScreenShotService::EVideoOutputScale::Half:
            return "50% Swapchain";
        case Runtime::IScreenShotService::EVideoOutputScale::Quarter:
            return "25% Swapchain";
        case Runtime::IScreenShotService::EVideoOutputScale::Full:
        default:
            return "100% Swapchain";
        }
    };

    const std::string outputScaleMenuLabel = fmt::format(
        "Recording size ({})", outputScaleLabel(videoOutputScale_));
    if (ImGui::BeginMenu(outputScaleMenuLabel.c_str()))
    {
        struct FVideoOutputScaleOption
        {
            Runtime::IScreenShotService::EVideoOutputScale scale;
            const char* label;
        };
        static constexpr std::array<FVideoOutputScaleOption, 3> options{{
            {Runtime::IScreenShotService::EVideoOutputScale::Full, "100% Swapchain"},
            {Runtime::IScreenShotService::EVideoOutputScale::Half, "50% Swapchain"},
            {Runtime::IScreenShotService::EVideoOutputScale::Quarter, "25% Swapchain"},
        }};

        for (const FVideoOutputScaleOption& option : options)
        {
            if (ImGui::MenuItem(option.label, nullptr, videoOutputScale_ == option.scale))
            {
                videoOutputScale_ = option.scale;
            }
        }
        ImGui::EndMenu();
    }

    // GIF encoding needs ffmpeg next to the executable, which release packages do not
    // ship. Say so in the menu instead of failing silently into the log.
    const bool gifAvailable = GetEngine().GetScreenShotService().IsGifEncodingAvailable();
    if (ImGui::MenuItem(gifAvailable ? "Record 3s GIF + Animated WebP" : "Record 3s Animated WebP"))
    {
        RequestThreeSecondVideo(videoOutputScale_);
    }
    if (!gifAvailable && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("GIF output is unavailable: ffmpeg was not found next to the executable.");
    }
}

bool NextRendererGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = modelViewController_.ModelView();
    outRenderCamera.FieldOfView = modelViewController_.FieldOfView();
#if GK_WITH_VITURE
    if (headPoseTracker_ && GOption != nullptr)
    {
        outRenderCamera.ModelView = arCamera_.BuildModelView(
            outRenderCamera.ModelView, GOption->ArWorldUnitsPerMeter);
    }
#endif
    return true;
}

float NextRendererGameInstance::GetGraphicsDebugPanelTopOffset() const
{
    return TitlebarSize;
}

bool NextRendererGameInstance::OnKey(SDL_Event& event)
{
    // WASDQE camera movement (only active when right mouse is pressed)
    modelViewController_.OnKey(event);

    if (event.key.type == SDL_EVENT_KEY_DOWN)
    {
#if GK_WITH_VITURE
        if (event.key.key == SDLK_R && headPoseTracker_ && arCamera_.Recenter())
        {
            SPDLOG_INFO("VITURE AR: tracking origin recentered");
            return true;
        }
#endif

        switch (event.key.key)
        {
        case SDLK_ESCAPE:
            GetEngine().GetScene().SetSelectedId(-1);
            GetEngine().GetShowFlags().ShowEdge = false;
            return true;
        case SDLK_F:
            {
                glm::vec3 focusCenter;
                float radius;
                if (GetEngine().GetScene().GetSelectedNodeBounds(focusCenter, radius))
                {
                    modelViewController_.Focus(focusCenter, radius);
                }
            }
            break;
        case SDLK_SPACE:
            shootHintRemainingSeconds_ = 0.0f;
            CreateBoxAndPush();
            return true;
        case SDLK_B: DropPhysicsSphereGrid(); return true;
        case SDLK_DELETE:
        case SDLK_BACKSPACE:
        {
            std::vector<uint32_t> ids = SelectedNodeIds(GetEngine().GetScene());
            if (ids.empty()) break;
            auto cmd = std::make_unique<Runtime::Command::DeleteNodesCommand>(GetEngine().GetScene(), std::move(ids));
            GetEngine().GetCommandHistory().Execute(std::move(cmd));
            return true;
        }
        case SDLK_D:
        {
            if (!(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) break;
            std::vector<uint32_t> ids = SelectedNodeIds(GetEngine().GetScene());
            if (ids.empty()) break;
            auto cmd = std::make_unique<Runtime::Command::DuplicateNodesCommand>(GetEngine().GetScene(), std::move(ids));
            GetEngine().GetCommandHistory().Execute(std::move(cmd));
            return true;
        }
        default: break;
        }
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    {
        switch (event.gbutton.button)
        {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            CreateSphereAndPush(); return true;
            break;
        default: break;
        }
    }
    return false;
}

bool NextRendererGameInstance::OnCursorPosition(double xpos, double ypos)
{
    // Update Controller Context
    bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    modelViewController_.SetAltPressed(alt);
    
    glm::vec3 center;
    float radius;
    if (GetEngine().GetScene().GetSelectedNodeBounds(center, radius))
    {
        modelViewController_.SetOrbitTarget(center);
    }
    else
    {
        modelViewController_.SetOrbitTarget(std::nullopt);
    }

    if (!mainUiState_.gizmoController.IsInteracting())
    {
        modelViewController_.OnCursorPosition(xpos, ypos);
    }
    return true;
}

bool NextRendererGameInstance::OnMouseButton(SDL_Event& event)
{
    if (!mainUiState_.gizmoController.IsInteracting())
    {
        modelViewController_.OnMouseButton(event);
    }
    else
    {
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        auto mousePos = GetEngine().GetMousePos();
        glm::vec3 org;
        glm::vec3 dir;
        if (GetEngine().GetOptions().ReferenceMode)
        {
            // Reference views occupy a 2x2 grid. Keep picking anchored to the first
            // view so the shared scene camera is unprojected with that view's aspect.
            const auto& renderExtent = GetEngine().GetRenderer().SwapChain().RenderExtent();
            const glm::vec2 referenceExtent{
                static_cast<float>(std::max(1u, renderExtent.width / 2u)),
                static_cast<float>(std::max(1u, renderExtent.height / 2u))};
            Assets::Camera pickCamera = GetEngine().GetScene().GetRenderCamera();
            OverrideRenderCamera(pickCamera);
            Runtime::EngineHelper::GetScreenToWorldRayWithCamera(
                pickCamera, mousePos, glm::vec2(0.0f), referenceExtent, org, dir);
        }
        else
        {
            Runtime::EngineHelper::GetScreenToWorldRay(mousePos, org, dir);
        }
        GetEngine().RayCast( org, dir, [this](Assets::RayCastResult result)
        {
            if (result.Hit)
            {
                GetEngine().GetScene().GetRenderCamera().FocalDistance = result.T;
                Runtime::EngineHelper::DrawAuxPoint( result.HitPoint, glm::vec4(0.2, 1, 0.2, 1), 2, 60 );
                GetEngine().GetScene().SetSelectedId(result.InstanceId);
                GetEngine().GetShowFlags().ShowEdge = true;
            }
            else
            {
                GetEngine().GetScene().SetSelectedId(-1);
                GetEngine().GetShowFlags().ShowEdge = false;
            }
            return true;
        });
        return true;
    }

    return true;
}

bool NextRendererGameInstance::OnTouch(SDL_Event& event)
{
#if IOS || ANDROID
    const SDL_TouchFingerEvent& touch = event.tfinger;

    if (event.type == SDL_EVENT_FINGER_DOWN)
    {
        uint64_t& activeFinger = touch.x < 0.5f ? mobilePanFinger_ : mobileRotateFinger_;
        if (activeFinger == 0)
        {
            activeFinger = touch.fingerID;
            if (touch.x < 0.5f)
            {
                mobilePanCenter_ = {touch.x, touch.y};
                modelViewController_.SetTouchMovement(0.0f, 0.0f);
            }
        }
        return true;
    }

    if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED)
    {
        if (mobilePanFinger_ == touch.fingerID)
        {
            mobilePanFinger_ = 0;
            modelViewController_.SetTouchMovement(0.0f, 0.0f);
        }
        if (mobileRotateFinger_ == touch.fingerID)
        {
            mobileRotateFinger_ = 0;
        }
        return true;
    }

    const bool pan = mobilePanFinger_ == touch.fingerID;
    const bool rotate = mobileRotateFinger_ == touch.fingerID;
    if (event.type != SDL_EVENT_FINGER_MOTION || (!pan && !rotate))
    {
        return true;
    }

    if (mainUiState_.gizmoController.IsInteracting())
    {
        if (pan)
        {
            modelViewController_.SetTouchMovement(0.0f, 0.0f);
        }
        return true;
    }

    const VkExtent2D windowSize = GetEngine().GetWindow().WindowSize();
    const double windowWidth = static_cast<double>(std::max(1u, windowSize.width));
    const double windowHeight = static_cast<double>(std::max(1u, windowSize.height));
    if (pan)
    {
        const double deltaX = (static_cast<double>(touch.x) - mobilePanCenter_.x) * windowWidth;
        const double deltaY = (static_cast<double>(touch.y) - mobilePanCenter_.y) * windowHeight;
        const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        const double joystickRadius = std::min(windowWidth, windowHeight) * 0.2;
        const float magnitude = static_cast<float>(std::min(distance / joystickRadius, 1.0));
        if (distance > 0.0001)
        {
            modelViewController_.SetTouchMovement(
                static_cast<float>(deltaX / distance) * magnitude,
                -static_cast<float>(deltaY / distance) * magnitude);
        }
        else
        {
            modelViewController_.SetTouchMovement(0.0f, 0.0f);
        }
        return true;
    }

    const double pixelDeltaX = static_cast<double>(touch.dx) * windowWidth;
    const double pixelDeltaY = static_cast<double>(touch.dy) * windowHeight;
    modelViewController_.OnTouchMove(false, pixelDeltaX, pixelDeltaY);
    return true;
#else
    (void)event;
    return false;
#endif
}

bool NextRendererGameInstance::OnScroll(double xoffset, double yoffset)
{
    if (!mainUiState_.gizmoController.IsInteracting())
    {
        modelViewController_.OnScroll(xoffset, yoffset);
    }
    return true;
}

bool NextRendererGameInstance::OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX, int16_t rightStickY,
    int16_t leftTrigger, int16_t rightTrigger)
{
    return modelViewController_.OnGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, leftTrigger, rightTrigger);
}

bool NextRendererGameInstance::OnRemoteViewAction(const FRemoteViewActionContext& context, std::string_view action)
{
    if (action != "space")
    {
        return false;
    }

    const std::string shortSession = context.sessionId.substr(0, std::min<size_t>(context.sessionId.size(), 8));
    CreateBoxAndPushFromView(FLaunchView{
        .position = context.position,
        .forward = context.forward,
        .right = context.right,
        .up = context.up,
        .debugName = fmt::format("remoteBox-{}", shortSession.empty() ? "client" : shortSession),
    });
    return true;
}

void NextRendererGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    const auto refreshMuzzle = [this]() { RefreshMuzzleTransform(); };
    cvars.RegisterFloat("r.muzzle.pitch", 0.0f, &muzzlePitchOffsetDegrees_, NextCVar::ECVarFlags::Archive,
                       "Muzzle pitch offset relative to the camera direction", refreshMuzzle, -45.0, 45.0);
    cvars.RegisterFloat("r.muzzle.yaw", 0.0f, &muzzleYawOffsetDegrees_, NextCVar::ECVarFlags::Archive,
                       "Muzzle yaw offset relative to the camera direction", refreshMuzzle, -45.0, 45.0);
    //cvars.SetDefaultFromString("r.upscaler.qualityMode", "4", &error);
}


void NextRendererGameInstance::CreateSphereAndPush()
{
    glm::vec3 forward = modelViewController_.GetForward();
    glm::vec3 center = modelViewController_.GetPosition() + forward * 0.1f + modelViewController_.GetRight() * 0.5f + modelViewController_.GetUp() * -0.5f;
    glm::vec3 farTarget = modelViewController_.GetPosition() + forward * 1000.0f + modelViewController_.GetUp() * 100.f;
    glm::vec3 shotDir = normalize((farTarget - center));
    uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());

    uint32_t newMatId = matIds_[std::rand() % matIds_.size()];
    std::shared_ptr<Assets::Node> newNode = Assets::SceneBuilder::CreateRenderNode("temp", center, glm::vec3(1), instanceId, modelId_, newMatId);
    
    auto phys = std::make_shared<Runtime::PhysicsComponent>();
    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
    auto id = GetEngine().GetPhysicsEngine()->CreateSphereBody(center, 0.2f, NextMotionType::Dynamic);
    phys->BindPhysicsBody(id);
    newNode->AddComponent(phys);

    GetEngine().GetScene().AddNode(newNode);
    GetEngine().GetScene().MarkDirty();

    GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 70000.f);
}

void NextRendererGameInstance::CreateBoxAndPush()
{
    CreateBoxAndPushFromView(FLaunchView{
        .position = modelViewController_.GetPosition(),
        .forward = modelViewController_.GetForward(),
        .right = modelViewController_.GetRight(),
        .up = modelViewController_.GetUp(),
        .debugName = "tempBox",
        .useMuzzle = true,
    });
}

void NextRendererGameInstance::EnsurePhysicsTicking(const char* what)
{
    Runtime::Config::UserSettings& userSettings = GetEngine().GetUserSettings();
    if (userSettings.TickPhysics)
    {
        return;
    }

    // sys.tickPhysics is archived, and a lighting or denoiser session usually leaves it off so
    // progressive accumulation is never disturbed. Spawning a body under that setting produces a
    // shot that hangs in mid-air with no indication why, so a request that only makes sense with
    // physics running turns the tick back on and says so.
    userSettings.TickPhysics = true;
    SPDLOG_INFO("gkNextRenderer: sys.tickPhysics was off; turned it on so the {} can move", what);
}

void NextRendererGameInstance::CreateBoxAndPushFromView(const FLaunchView& view)
{
    if (matIds_.empty())
    {
        SPDLOG_WARN("gkNextRenderer: ignored box launch before dynamic materials are ready");
        return;
    }
    if (boxModelId_ >= GetEngine().GetScene().Models().size())
    {
        SPDLOG_WARN("gkNextRenderer: ignored box launch before dynamic box model is ready");
        return;
    }
    NextPhysics* physicsEngine = GetEngine().GetPhysicsEngine();
    if (physicsEngine == nullptr)
    {
        SPDLOG_WARN("gkNextRenderer: ignored box launch because physics is unavailable");
        return;
    }

    EnsurePhysicsTicking("launched projectile");

    const glm::vec3 forward = glm::normalize(view.forward);
    const glm::vec3 right = glm::normalize(view.right);
    const glm::vec3 up = glm::normalize(view.up);
    const bool fromMuzzle = view.useMuzzle && muzzleNode_;
    // A shell is a world object: how big it is depends on the scene it lands in, never on the
    // camera looking at it. The weapon takes the same scale, so the bore stays exactly as wide as
    // the shell leaving it at every field of view.
    const float projectileScale = launchSceneScale_;
    const float projectileSize = projectileBaseSize * projectileScale;

    glm::vec3 launchDirection = forward;
    glm::quat launchRotation = glm::normalize(glm::quat_cast(glm::mat3(right, up, -forward)));
    glm::vec3 center = view.position +
        (forward * muzzleDepth + right * muzzleRightOffset - up * muzzleDownOffset) * launchSceneScale_;
    if (fromMuzzle)
    {
        launchRotation = muzzleRotation_;
        launchDirection = MuzzleDirection();
        // Centre the shell on the muzzle mouth: its rear half stays inside the barrel, so it
        // reads as leaving the bore rather than appearing in mid-air ahead of it.
        center = MuzzleMouthPosition();
    }

    const uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());
    const uint32_t newMatId = matIds_[std::rand() % matIds_.size()];
    std::shared_ptr<Assets::Node> newNode =
        Assets::SceneBuilder::CreateRenderNode(
            view.debugName, center, glm::vec3(projectileScale), instanceId, boxModelId_, newMatId,
            true, launchRotation);

    auto phys = std::make_shared<Runtime::PhysicsComponent>();
    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
    auto id = physicsEngine->CreateBoxBody(
        center, launchRotation, glm::vec3(projectileSize), NextMotionType::Dynamic);
    phys->BindPhysicsBody(id);
    newNode->AddComponent(phys);

    GetEngine().GetScene().AddNode(newNode);
    GetEngine().GetScene().MarkDirty();

    // Set the muzzle velocity rather than adding a force. A one-step force divides by the frame
    // time, so the same press threw the cube at roughly 26 m/s at 60 fps and barely nudged it at
    // the 1200 fps this renderer reaches on a hidden window. Speed follows the scene scale, which
    // is the square root of the extent ratio -- exactly the relation that keeps the ballistic arc
    // looking the same when every distance in the scene grows.
    const float speed = projectileBaseSpeed * launchSceneScale_;
    physicsEngine->SetBodyVelocity(id, launchDirection * speed,
                                   right * projectileSpinPitch + up * projectileSpinYaw);
}

void NextRendererGameInstance::DropPhysicsSphereGrid()
{
    Assets::Scene& scene = GetEngine().GetScene();
    NextPhysics* physicsEngine = GetEngine().GetPhysicsEngine();
    if (physicsEngine == nullptr)
    {
        SPDLOG_WARN("gkNextRenderer: ignored sphere drop because physics is unavailable");
        return;
    }
    if (modelId_ >= scene.Models().size())
    {
        SPDLOG_WARN("gkNextRenderer: ignored sphere drop before dynamic sphere model is ready");
        return;
    }
    if (dropSphereMatIds_.size() != dropSphereCount)
    {
        SPDLOG_WARN("gkNextRenderer: ignored sphere drop before sphere materials are ready");
        return;
    }

    EnsurePhysicsTicking("dropped spheres");

    const glm::vec3 boundsMin = scene.GetSceneAABBMin() * 0.5f;
    const glm::vec3 boundsMax = scene.GetSceneAABBMax() * 0.5f;
    const glm::vec3 boundsSize = glm::max(boundsMax - boundsMin, glm::vec3(0.0f));
    const float fallbackSpan = std::max({boundsSize.x, boundsSize.y * 0.5f, boundsSize.z, 4.0f});
    const float spanX = boundsSize.x > 0.01f ? boundsSize.x : fallbackSpan;
    const float spanZ = boundsSize.z > 0.01f ? boundsSize.z : fallbackSpan;
    const float stepX = spanX / static_cast<float>(dropSphereGridSize);
    const float stepZ = spanZ / static_cast<float>(dropSphereGridSize);
    const float radius = std::max(0.05f, std::min(stepX, stepZ) * 0.3f);
    const float renderScale = radius / 0.2f;
    const float startX = boundsSize.x > 0.01f ? boundsMin.x : (boundsMin.x + boundsMax.x - spanX) * 0.5f;
    const float startZ = boundsSize.z > 0.01f ? boundsMin.z : (boundsMin.z + boundsMax.z - spanZ) * 0.5f;
    const float spawnY = boundsMax.y + radius + std::max(0.05f, boundsSize.y * 0.02f);
    std::vector<uint32_t> shuffledMaterialIds = dropSphereMatIds_;
    std::mt19937 randomEngine(HashUint(0x6d2b79f5u ^ dropSphereSequence_++));
    std::shuffle(shuffledMaterialIds.begin(), shuffledMaterialIds.end(), randomEngine);

    for (uint32_t row = 0; row < dropSphereGridSize; ++row)
    {
        for (uint32_t column = 0; column < dropSphereGridSize; ++column)
        {
            const uint32_t sphereIndex = row * dropSphereGridSize + column;
            const glm::vec3 position{
                startX + (static_cast<float>(column) + 0.5f) * stepX,
                spawnY,
                startZ + (static_cast<float>(row) + 0.5f) * stepZ,
            };
            const uint32_t instanceId = static_cast<uint32_t>(scene.Nodes().size());
            std::shared_ptr<Assets::Node> node = Assets::SceneBuilder::CreateRenderNode(
                fmt::format("dropSphere_{:03}", sphereIndex),
                position,
                glm::vec3(renderScale),
                instanceId,
                modelId_,
                shuffledMaterialIds[sphereIndex]);

            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            physics->BindPhysicsBody(
                physicsEngine->CreateSphereBody(position, radius, NextMotionType::Dynamic));
            node->AddComponent(physics);
            scene.AddNode(std::move(node));
        }
    }

    scene.MarkDirty();
    SPDLOG_INFO("gkNextRenderer: dropped {} physics spheres above scene bounds", dropSphereCount);
}
