#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/PipelineCommon/SamplePostChain.hpp"

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Utilities/Math.hpp"

namespace Vulkan::PipelineCommon
{
    void SamplePostChain::CreateSwapChain(const SwapChain& swapChain, const Assets::Scene& scene)
    {
        progressiveAccumulatePipeline_ = std::make_unique<ZeroBindPipeline>(
            swapChain, "assets/shaders/Process.ProgressiveAccumulate.comp.slang.spv", scene);
        disocclusionFilterPipeline_ = std::make_unique<ZeroBindPipeline>(
            swapChain, "assets/shaders/Process.DisocclusionFilter.comp.slang.spv", scene);
        composePipeline_ = std::make_unique<ZeroBindPipeline>(
            swapChain, "assets/shaders/Process.Compose.comp.slang.spv", scene);
    }

    void SamplePostChain::DeleteSwapChain()
    {
        progressiveAccumulatePipeline_.reset();
        disocclusionFilterPipeline_.reset();
        composePipeline_.reset();
    }

    namespace
    {
        // The filter stands in for history the temporal upscaler does not have yet, so it only
        // makes sense where such an upscaler accumulates the realtime sample, and RT_HISTORY_AGE
        // is only meaningful on a view that keeps object-id history. Ray Reconstruction denoises
        // the raw diffuse/specular itself and must see them unfiltered. Sparse checkerboard
        // lighting leaves half the pixels unwritten for Native TAAU, so a tap there is garbage.
        bool ShouldRunDisocclusionFilter(const VulkanBaseRenderer& baseRenderer,
                                         const FSamplePostSettings& settings)
        {
            const auto& userSettings = baseRenderer.FrameSettings().userSettings;
            const RenderView& view = baseRenderer.ActiveRenderView();
            return userSettings.DisocclusionFilter && userSettings.DisocclusionFilterFrames > 0 &&
                !settings.progressiveRender && view.IsPrimary() && view.CopyObjectIdHistory() &&
                baseRenderer.IsTemporalSuperResolutionActive() &&
                !baseRenderer.IsRayReconstructionActive() &&
                !baseRenderer.IsSparseCheckerboardLightingActive();
        }
    }

    void SamplePostChain::RunDisocclusionFilter(
        VulkanBaseRenderer& baseRenderer,
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex) const
    {
        SCOPED_GPU_TIMER("disocclusion filter");
        const auto& userSettings = baseRenderer.FrameSettings().userSettings;
        // At least two passes: the last one writes the signal plane in place, which is only
        // race-free when its taps come from a ping-pong plane (see the shader header).
        const uint32_t diffusePassCount = std::clamp(userSettings.DisocclusionFilterPasses, 2u, 5u);
        // A glossy lobe tolerates far less spatial reach than diffuse: radius 1, 2, 4 at most.
        const uint32_t specularPassCount = std::min(diffusePassCount, 3u);
        const uint32_t fullFrames = std::min(userSettings.DisocclusionFilterFrames, 0xFFFFu);
        const VkExtent2D extent = baseRenderer.ActiveViewRenderExtent();
        const Assets::GPUScene gpuScene =
            baseRenderer.GetScene().FetchGPUScene(imageIndex, baseRenderer.ActiveViewBankBase());

        constexpr auto compute = ERenderStage::Compute;
        constexpr auto shaderRead = EResourceAccess::ShaderRead;
        constexpr auto shaderReadWrite = EResourceAccess::ShaderRead | EResourceAccess::ShaderWrite;
        const auto runChannel = [&](const bool specular, const uint32_t passCount)
        {
            const uint32_t signal = specular
                ? Assets::Bindless::RT_SINGLE_SPECULAR
                : Assets::Bindless::RT_SINGLE_DIFFUSE;
            for (uint32_t pass = 0; pass < passCount; ++pass)
            {
                // Every pass both reads and writes the ping-pong planes and the signal plane, so
                // the tracker places a full write->read barrier between consecutive passes.
                baseRenderer.TransitionActiveViewImages(commandBuffer, {
                    {signal, compute, shaderReadWrite},
                    {Assets::Bindless::RT_DISOCCLUSION_PING, compute, shaderReadWrite},
                    {Assets::Bindless::RT_DISOCCLUSION_PONG, compute, shaderReadWrite},
                    {Assets::Bindless::RT_HISTORY_AGE, compute, shaderRead},
                    {Assets::Bindless::RT_PREV_DEPTHBUFFER, compute, shaderRead},
                    {Assets::Bindless::RT_NORMAL, compute, shaderRead},
                    {Assets::Bindless::RT_OBJECTID_0, compute, shaderRead},
                    {Assets::Bindless::RT_BSDF_DATA, compute, shaderRead},
                    {Assets::Bindless::RT_ALBEDO, compute, shaderRead},
                    {Assets::Bindless::RT_SPECULAR_HITDIST, compute, shaderRead},
                }, specular ? "disocclusion filter (specular)" : "disocclusion filter");
                const uint32_t params = pass | (passCount << 8u) | (fullFrames << 16u);
                disocclusionFilterPipeline_->BindPipeline(
                    commandBuffer, gpuScene, baseRenderer.ActiveViewBankBase(), params,
                    specular ? 1u : 0u);
                vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(extent.width, 8),
                              Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
            }
        };
        runChannel(false, diffusePassCount);
        if (userSettings.DisocclusionFilterSpecular)
        {
            runChannel(true, specularPassCount);
        }
    }

    FSamplePostSettings MakeSamplePostSettings(const VulkanBaseRenderer& baseRenderer)
    {
        const FFrameRenderSettings& frameSettings = baseRenderer.FrameSettings();
        return {
            .progressiveRender =
                (baseRenderer.ActiveRenderView().IsPrimary() && frameSettings.progressiveRendering) ||
                baseRenderer.IsReferenceViewAccumulationActive(),
            .progressiveSampleCount = baseRenderer.ProgressiveSampleCountForActiveView(),
            .progressiveTargetSampleCount = frameSettings.progressiveTargetFrames,
        };
    }

    void SamplePostChain::Run(
        VulkanBaseRenderer& baseRenderer,
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        const FSamplePostSettings& settings) const
    {
        const VkExtent2D extent = baseRenderer.ActiveViewRenderExtent();
        if (ShouldRunDisocclusionFilter(baseRenderer, settings))
        {
            RunDisocclusionFilter(baseRenderer, commandBuffer, imageIndex);
        }

        if (settings.progressiveRender)
        {
            baseRenderer.EnsureProgressiveRenderTarget();
            SCOPED_GPU_TIMER("progressive accumulate");
            baseRenderer.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_SINGLE_DIFFUSE, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_SINGLE_SPECULAR, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_ALBEDO, ERenderStage::Compute, EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_PROGRESSIVE_SCENE_COLOR, ERenderStage::Compute,
                 EResourceAccess::ShaderRead | EResourceAccess::ShaderWrite},
            }, "progressive accumulate");

            progressiveAccumulatePipeline_->BindPipeline(
                commandBuffer,
                baseRenderer.GetScene().FetchGPUScene(imageIndex, baseRenderer.ActiveViewBankBase()),
                baseRenderer.ActiveViewBankBase(), settings.progressiveSampleCount,
                settings.progressiveTargetSampleCount);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(extent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
        }

        {
            SCOPED_GPU_TIMER("sample compose");
            if (settings.progressiveRender)
            {
                baseRenderer.TransitionActiveViewImages(commandBuffer, {
                    {Assets::Bindless::RT_PROGRESSIVE_SCENE_COLOR, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_OBJECTID_0, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_OBJECTID_1, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_SCENE_COLOR, ERenderStage::Compute, EResourceAccess::ShaderWrite},
                    {Assets::Bindless::RT_UPSCALER_REACTIVE, ERenderStage::Compute, EResourceAccess::ShaderWrite},
                }, "sample compose");
            }
            else
            {
                baseRenderer.TransitionActiveViewImages(commandBuffer, {
                    {Assets::Bindless::RT_SINGLE_DIFFUSE, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_SINGLE_SPECULAR, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_ALBEDO, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_OBJECTID_0, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_OBJECTID_1, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_PREV_DEPTHBUFFER, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_NORMAL, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_BSDF_DATA, ERenderStage::Compute, EResourceAccess::ShaderRead},
                    {Assets::Bindless::RT_SCENE_COLOR, ERenderStage::Compute, EResourceAccess::ShaderWrite},
                    {Assets::Bindless::RT_UPSCALER_REACTIVE, ERenderStage::Compute, EResourceAccess::ShaderWrite},
                }, "sample compose");
            }

            // Firefly suppression only where a temporal upscaler accumulates the raw sample:
            // reference accumulation must stay unbiased, Ray Reconstruction denoises the raw planes
            // itself, and sparse checkerboard lighting leaves half the neighbours unwritten.
            const auto& userSettings = baseRenderer.FrameSettings().userSettings;
            const bool fireflyClamp = !settings.progressiveRender &&
                !baseRenderer.FrameSettings().offlineProgressivePathTracing &&
                !baseRenderer.IsRayReconstructionActive() &&
                !baseRenderer.IsSparseCheckerboardLightingActive() &&
                userSettings.ComposeFireflyClamp;
            composePipeline_->BindPipeline(
                commandBuffer,
                baseRenderer.GetScene().FetchGPUScene(imageIndex, baseRenderer.ActiveViewBankBase()),
                baseRenderer.ActiveViewBankBase(),
                settings.progressiveRender ? 1u : 0u,
                fireflyClamp ? std::bit_cast<uint32_t>(userSettings.TemporalUpscalerFireflySigma) : 0u);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(extent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
        }
    }
}
