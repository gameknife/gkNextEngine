// NextEngine per-frame camera UBO assembly: projection build, temporal Halton
// jitter, Android pre-rotation and sun shadow cascade caching.
// Split from Engine.cpp; same class, separate TU.
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/Acceleration/AmbientCascadeLayout.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <cstdlib>

namespace
{
    constexpr uint32_t maxJitterFrameCount = 256;

    // Generate one dimension of a Halton sequence.
    float HaltonSequence(int index, int base)
    {
        float f = 1.0f;
        float result = 0.0f;
        while (index > 0)
        {
            f = f / base;
            result = result + f * (index % base);
            index = index / base;
        }
        return result;
    }

    const std::array<glm::vec2, maxJitterFrameCount>& HaltonJitterSequence()
    {
        static const std::array<glm::vec2, maxJitterFrameCount> sequence = []
        {
            std::array<glm::vec2, maxJitterFrameCount> result{};
            for (uint32_t i = 0; i < maxJitterFrameCount; ++i)
            {
                const float x = HaltonSequence(static_cast<int>(i + 1), 2);
                const float y = HaltonSequence(static_cast<int>(i + 1), 3);
                result[i] = glm::vec2(x, y) - glm::vec2(0.5f, 0.5f);
            }
            return result;
        }();
        return sequence;
    }

    glm::vec2 GetTemporalJitter(uint32_t frameIndex, uint32_t frameCount)
    {
        frameCount = std::clamp(frameCount, 1u, maxJitterFrameCount);
        return HaltonJitterSequence()[frameIndex % frameCount];
    }
} // namespace

Assets::Camera NextEngine::GetActiveRenderCamera() const
{
    Assets::Camera renderCamera = scene_->GetRenderCamera();
    if (gameInstance_)
    {
        gameInstance_->OverrideRenderCamera(renderCamera);
    }

    // This is an explicit global policy: changing r.camera.farPlane affects
    // every main-view camera, including game/editor camera overrides.
    renderCamera.FarPlane = std::max(
        config_.userSettings.CameraFarPlane, renderCamera.NearPlane + 1.0e-3f);
    return renderCamera;
}

Assets::UniformBufferObject NextEngine::GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent)
{
    // Per-view temporal history now lives in the primary RenderView (bank 0). Multi-viewport will
    // pass the specific view's state here; the primary view is byte-identical to the old global state.
    Vulkan::FViewRenderState& viewState = renderer_->PrimaryViewState();

    // a copy, simple struct
    Assets::Camera renderCam = GetActiveRenderCamera();
    scene_->OverrideModelView(renderCam.ModelView);
    Assets::UniformBufferObject ubo = Vulkan::BuildViewCameraUbo({
        .scene = *scene_,
        .camera = renderCam,
        .extent = extent,
        .cascadeDistance = 400.0f,
        .totalFrames = frameState_.totalFrames,
        .fillSunCascades = false,
        .fillSceneLighting = false,
    });

    ubo.ExitAfterFirst = config_.userSettings.ExitAfterFirst;
    ubo.SuperResolution = GOption->ReferenceMode ? 2 : renderer_->EffectiveSuperResolutionMode();
    ubo.LowHistorySampleScale = std::clamp(config_.userSettings.LowHistorySampleScale, 1u, 16u);
    ubo.SobolSampling = config_.userSettings.SobolSampling ? 1u : 0u;
    ubo.RayReconstruction = renderer_->IsRayReconstructionActive();
    ubo.CheckerboardSparseLighting = renderer_->IsSparseCheckerboardLightingActive();

    glm::mat4x4 projectionUnJit = ubo.Projection;
    const bool noAmbientRenderer = renderer_->CurrentLogicRendererType() == Vulkan::ERT_SoftwareModernNoAmbient;
    const bool enablePrimaryRayJitter = renderer_->IsTemporalSuperResolutionActive();

    if (enablePrimaryRayJitter)
    {
        const VkExtent2D renderExtent = renderer_->SwapChain().RenderExtent();
        const uint32_t providerJitterFrames = renderer_->TemporalJitterFrameCount();
        const uint32_t jitterFrames = providerJitterFrames > 0
            ? providerJitterFrames
            : config_.userSettings.UpscalerJitterFrames;
        glm::vec2 jitter = GetTemporalJitter(frameState_.totalFrames, jitterFrames);
        if (config_.userSettings.UpscalerJitterInvertY)
        {
            jitter.y = -jitter.y;
        }

        ubo.Projection[2][0] = -jitter.x / static_cast<float>(renderExtent.width) * 2.0f;
        ubo.Projection[2][1] = -jitter.y / static_cast<float>(renderExtent.height) * 2.0f;

        ubo.Jitter = glm::vec4(jitter.x, jitter.y, 0, 0);
    }
    else
    {
        ubo.Jitter = glm::vec4(0, 0, 0, 0);
    }

    // Inverting Y for Vulkan, https://matthewwellings.com/blog/the-new-vulkan-coordinate-system/
    ubo.ModelViewInverse = glm::inverse(ubo.ModelView);
    ubo.ProjectionInverse = glm::inverse(ubo.Projection);
    ubo.ViewProjection = ubo.Projection * ubo.ModelView;
    ubo.ViewProjectionUnJit = projectionUnJit * ubo.ModelView;
    ubo.ProjectionUnJit = projectionUnJit;
    ubo.ProjectionInverseUnJit = glm::inverse(projectionUnJit);

    ubo.PrevViewProjection = viewState.previousUniformBuffer.TotalFrames != 0
                                  ? viewState.previousUniformBuffer.ViewProjection
                                  : ubo.ViewProjection;
    ubo.PrevViewProjectionUnJit = viewState.previousUniformBuffer.TotalFrames != 0
                                      ? viewState.previousUniformBuffer.ViewProjectionUnJit
                                      : ubo.ViewProjectionUnJit;

    ubo.ViewportRect =
        glm::vec4(renderer_->SwapChain().RenderOffset().x, renderer_->SwapChain().RenderOffset().y,
                  renderer_->SwapChain().RenderExtent().width, renderer_->SwapChain().RenderExtent().height);

    const glm::vec4 sunDirection = glm::vec4(scene_->GetEnvSettings().SunDirection(), 0.0f);
    const bool hasSun = scene_->GetEnvSettings().HasSun && scene_->GetEnvSettings().SunIntensity > 0;
    {
        const auto cascades = scene_->GetEnvSettings().ComputeSunCascades(
            ubo.ViewProjectionUnJit, renderCam.NearPlane, renderCam.FarPlane, 400.f);
        const uint32_t frameIndex = static_cast<uint32_t>(std::max(renderer_->FrameCount(), 0));
        bool cameraChanged = viewState.previousUniformBuffer.TotalFrames == 0;
        for (uint32_t column = 0; column < 4 && !cameraChanged; ++column)
        {
            for (uint32_t row = 0; row < 4; ++row)
            {
                if (viewState.previousUniformBuffer.ViewProjectionUnJit[column][row] !=
                    ubo.ViewProjectionUnJit[column][row])
                {
                    cameraChanged = true;
                    break;
                }
            }
        }
        const bool forceRefresh = !viewState.cachedSunCascadesValid ||
                                  cameraChanged ||
                                  (bool)viewState.previousUniformBuffer.HasSun != hasSun ||
                                  viewState.previousUniformBuffer.SunDirection != sunDirection;

        if (!hasSun)
        {
            viewState.cachedSunCascadesValid = false;
            viewState.sunShadowCascadeUpdateMask = 0u;
            viewState.sunShadowInitializedMask = 0u;
            viewState.sunShadowDirtyMask = Assets::Scene::kSunShadowCascadeMask;
        }
        else
        {
            if (!viewState.cachedSunCascadesValid)
            {
                // The uninitialized cascade image is already cleared to depth=1; provide the UBO a valid matrix.
                viewState.cachedSunCascades = cascades;
            }
            if (forceRefresh)
            {
                viewState.sunShadowDirtyMask = Assets::Scene::kSunShadowCascadeMask;
            }

            const uint32_t priorityCascadeMask =
                viewState.sunShadowDirtyMask |
                (Assets::Scene::kSunShadowCascadeMask & ~viewState.sunShadowInitializedMask);
            const uint32_t activeCascadeMask =
                Assets::Scene::BuildSunShadowCascadeUpdateMask(frameIndex, priorityCascadeMask);

            for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
            {
                if ((activeCascadeMask & (1u << cascade)) != 0u)
                {
                    viewState.cachedSunCascades.viewProjection[cascade] = cascades.viewProjection[cascade];
                    viewState.cachedSunCascades.splits[cascade] = cascades.splits[cascade];
                }
            }

            viewState.sunShadowCascadeUpdateMask = activeCascadeMask;
            viewState.sunShadowInitializedMask |= activeCascadeMask;
            viewState.sunShadowDirtyMask &= ~activeCascadeMask;
            viewState.cachedSunCascadesValid = true;
        }

        for (int i = 0; i < 4; ++i)
        {
            ubo.SunCascadeViewProjection[i] = viewState.cachedSunCascades.viewProjection[i];
        }
        ubo.CascadeSplits = viewState.cachedSunCascades.splits;
    }

    // Camera Stuff
    ubo.Aperture = renderCam.Aperture;
    ubo.FocusDistance = renderCam.FocalDistance;

    // SceneStuff
    ubo.SkyRotation = scene_->GetEnvSettings().SkyRotation;
    ubo.MaxNumberOfBounces = config_.userSettings.MaxNumberOfBounces;
    ubo.TotalFrames = frameState_.totalFrames;
    // Progressive accumulation advances one independent sample per frame. Keep the
    // configured realtime SPP untouched so it is restored when progressive mode ends.
    const bool progressiveSampling = config_.userSettings.ProgressiveRender || progressiveRender_.enabled;
    ubo.NumberOfSamples = progressiveSampling ? 1 : config_.userSettings.NumberOfSamples;
    ubo.NumberOfBounces = config_.userSettings.NumberOfBounces;
    ubo.PrimaryRayJitter = enablePrimaryRayJitter;
    ubo.SunDirection = sunDirection;
    glm::vec3 sunTransmittance(1.0f);
    if (scene_->GetEnvSettings().AtmosphereEnabled)
    {
        const glm::vec3 cameraPosition = glm::vec3(ubo.ModelViewInverse[3]);
        const auto& atmosphere = scene_->GetEnvSettings().Atmosphere;
        const float cameraAltitudeKm =
            atmosphere.WorldOriginAltitude + cameraPosition.y / std::max(atmosphere.WorldUnitsPerKm, 0.001f);
        sunTransmittance = renderer_->AtmosphereTransmittanceToSun(
            cameraAltitudeKm, glm::dot(glm::normalize(glm::vec3(sunDirection)), glm::vec3(0, 1, 0)));
    }
    ubo.SunColor = glm::vec4(scene_->GetEnvSettings().SunColor * sunTransmittance, 0.0f) *
        scene_->GetEnvSettings().SunIntensity;
    ubo.SkyColor = glm::vec4(scene_->GetEnvSettings().SkyColor, 1.0f);
    ubo.SkyIntensity = scene_->GetEnvSettings().SkyIntensity;
    ubo.SkyIdx = scene_->GetEnvSettings().SkyIdx;
    if (scene_->GetEnvSettings().AtmosphereEnabled)
    {
        ubo.SkyIdx = Assets::MAX_HDR_SH - 1;
        ubo.SkyIntensity = 1.0f;
    }
    ubo.HasSky = scene_->GetEnvSettings().HasSky;
    ubo.BackgroundMode = static_cast<uint32_t>(scene_->GetEnvSettings().BackgroundMode);
    ubo.LodBaseThreshold = config_.userSettings.LodBaseThreshold;
    ubo.LodShadowThreshold =
        config_.userSettings.LodBaseThreshold * config_.userSettings.LodShadowThresholdScale;
    ubo.AtmosphereParams = renderer_->AtmosphereParamsAddress();
    ubo.AtmosphereReserved0 = 0;
    if (auto* texturePool = Assets::GlobalTexturePool::GetInstance())
    {
        texturePool->TickHDRTextureResidency(
            ubo.SkyIdx, ubo.HasSky, frameState_.totalFrames, config_.userSettings.StreamHDRTextures);
    }
    ubo.HasSun = hasSun;

    ubo.ShowHeatmap = config_.showFlags.ShowVisualDebug;
    ubo.HeatmapScale = config_.userSettings.HeatmapScale;
    ubo.DebugDraw_Lighting = config_.showFlags.DebugDraw_Lighting;
    ubo.ForceBlackBackground = false;
    // Progressive accumulation takes exactly one independent sample per frame, so it never boosts
    // freshly disoccluded pixels; the realtime path boosts until the history is TemporalFrames old.
    ubo.TemporalFrames = noAmbientRenderer || progressiveSampling
        ? 0u
        : config_.userSettings.TemporalFrames;
    ubo.HDR = renderer_->SwapChain().IsHDR();
    ubo.HDROutputMode = renderer_->SwapChain().HDROutputMode();

    ubo.PaperWhiteNit = config_.userSettings.PaperWhiteNit;
    ubo.LightCount = scene_->GetLightCount();
    
    ubo.GTAORadius = std::max(config_.userSettings.GTAORadius, 0.01f);
    ubo.GTAOStrength = std::max(config_.userSettings.GTAOStrength, 0.0f);
    ubo.GTAOThickness = std::max(config_.userSettings.GTAOThickness, 0.01f);
    ubo.GTAODebugMode = static_cast<uint32_t>(std::clamp(config_.userSettings.GTAODebugMode, 0, 2));
    ubo.GTAOEnable = config_.userSettings.GTAOEnable;
    ubo.GTAOQuality = static_cast<uint32_t>(std::clamp(config_.userSettings.GTAOQuality, 0, 3));
    ubo.LightObjectScreenSpaceShadow = config_.userSettings.LightObjectScreenSpaceShadow;
    ubo.LightObjectShadowDistance = std::max(config_.userSettings.LightObjectShadowDistance, 0.0f);
    ubo.LightObjectMaxShadowedLights =
        std::min(config_.userSettings.LightObjectMaxShadowedLights, 2u);
    ubo.LightObjectShadowSteps =
        std::clamp(config_.userSettings.LightObjectShadowSteps, 4u, 12u);

    // The light grid anchors to the camera, but the anchor is uploaded rather than re-derived in
    // each shader: the build pass and every query must agree on cell boundaries to the bit, and a
    // shader that recomputed the camera position from its own view matrix would not.
    ubo.LightGridCascadeCount = Assets::ResolveLightGridCascadeCount(
        config_.userSettings.LightGridCascadeCount, scene_->GetLightCount(),
        renderer_->ActiveRendererRequirements().requestLightGrid);
    ubo.LightGridCullThreshold = std::max(config_.userSettings.LightGridCullThreshold, 0.0f);
    ubo.LightGridAnchor = glm::vec4(glm::vec3(ubo.ModelViewInverse[3]),
                                    std::max(config_.userSettings.LightGridBaseCellSize, 0.01f));

    ubo.ProgressiveRender = progressiveRender_.enabled;
    ubo.SceneEpsilonScale = config_.userSettings.SceneEpsilonScale;
    // Shading follows the grid the CPU bakers were built with, not the pending settings: the voxel
    // array and cube pool only move to a new grid when the bakers are re-initialized, and shading
    // the settings instead would sample the old data through the new grid until the rebake lands.
    // Without bakers (no CPU acceleration) the settings resolve to the fixed-anchor layout.
    Assets::FAmbientCascadeLayoutInputs layoutInputs;
    layoutInputs.baseUnit = Assets::SanitizeAmbientCubeUnit(config_.userSettings.AmbientCubeUnit);
    layoutInputs.offsetBias = glm::vec3(config_.userSettings.AmbientCubeOffsetX, config_.userSettings.AmbientCubeOffsetY,
                                        config_.userSettings.AmbientCubeOffsetZ);
    layoutInputs.cascadeCount = Assets::SanitizeAmbientCubeCascadeCount(config_.userSettings.AmbientCubeCascadeCount);
    layoutInputs.cascadeRatio = Assets::SanitizeAmbientCubeCascadeRatio(config_.userSettings.AmbientCubeCascadeRatio);
    float ambientCubeUnit = layoutInputs.baseUnit;
    glm::vec3 ambientCubeOffsetBias = layoutInputs.offsetBias;
    Assets::FAmbientCascadeLayout ambientLayout = Assets::ResolveAmbientCascadeLayout(layoutInputs);
    bool ambientHasSceneBox = false;
    glm::vec3 ambientSceneMin{0.0f};
    glm::vec3 ambientSceneMax{0.0f};
    if (scene_)
    {
        Assets::CPU::FAmbientGridConfig committedGrid;
        if (scene_->GetCPUAccelerationStructure().TryGetAmbientGridConfig(committedGrid))
        {
            ambientCubeUnit = committedGrid.baseUnit;
            ambientCubeOffsetBias = committedGrid.offsetBias;
            ambientLayout = committedGrid.layout;
            ambientHasSceneBox = committedGrid.layout.followScene;
            ambientSceneMin = committedGrid.sceneMin;
            ambientSceneMax = committedGrid.sceneMax;
        }
        // Never advertise more cascades than the arena was sized for.
        ambientLayout.cascadeCount = std::min(ambientLayout.cascadeCount, scene_->AmbientCubeCascadeCapacity());
    }
    ubo.AmbientCubeUnit = ambientCubeUnit;
    ubo.IndirectIntensity = std::max(config_.userSettings.IndirectIntensity, 0.0f);
    ubo.MultiBounceIntensity = std::max(config_.userSettings.MultiBounceIntensity, 0.0f);
    ubo.AmbientCubeOffset = Assets::CalculateAmbientCubeOffset(ambientCubeUnit, ambientCubeOffsetBias);
    ubo.AmbientCubeCascadeParams =
        glm::vec4(float(std::max(ambientLayout.cascadeCount, 1u)), ambientLayout.cascadeRatio,
                  static_cast<float>(std::min(config_.userSettings.AmbientCubePublishPasses, 32u)),
                  static_cast<float>(std::clamp(config_.userSettings.AmbientCubeDebugView, 0, 2)));
    for (uint32_t cascade = 0; cascade < static_cast<uint32_t>(Assets::CUBE_CASCADE_MAX); ++cascade)
    {
        ubo.AmbientCubeCascadeGrid[cascade] = glm::vec4(ambientLayout.origins[cascade], ambientLayout.units[cascade]);
        const uint32_t openFaces = cascade < ambientLayout.cascadeCount
            ? Assets::AmbientCascadeOpenFaces(ambientLayout, cascade, ambientHasSceneBox, ambientSceneMin, ambientSceneMax)
            : 0u;
        ubo.AmbientCubeCascadeWrap[cascade] = glm::vec4(
            glm::vec3(ambientLayout.wraps[cascade]),
            Assets::PackAmbientCascadeBlend(openFaces, config_.userSettings.AmbientCubeBlendCells));
    }

    // Other Setup
    renderer_->SetVisualDebugEnabled(config_.showFlags.ShowVisualDebug);
    // UBO Backup, for motion vector calc
    viewState.previousUniformBuffer = ubo;

    return ubo;
}
