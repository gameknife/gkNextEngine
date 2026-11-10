#include "Engine/Common/CoreMinimal.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/packing.hpp>
#include <cstring>

#include "TestCommon.hpp"
#include "Engine/Rendering/Exposure/AutoExposurePass.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"

namespace
{
    struct FExposureTestBuffer
    {
        std::unique_ptr<Vulkan::DeviceMemory> memory;
        std::unique_ptr<Vulkan::Buffer> buffer;

        FExposureTestBuffer(const Vulkan::Device& device, size_t size, VkBufferUsageFlags usage)
        {
            buffer = std::make_unique<Vulkan::Buffer>(device, size, usage);
            memory = std::make_unique<Vulkan::DeviceMemory>(buffer->AllocateMemory(
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT}));
        }
    };
}

TEST_CASE_METHOD(EngineTestFixture, "Auto exposure meters, adapts and limits brightness on the GPU",
                 "[GPU][Integration][AutoExposure]")
{
    auto& renderer = engine_->GetRenderer();
    const auto& device = renderer.Device();
    device.WaitIdle();
    const auto* image = renderer.GetStorageImage(Assets::Bindless::RT_SCENE_COLOR);
    REQUIRE(image != nullptr);
    REQUIRE(image->GetImage().Format() == VK_FORMAT_R16G16B16A16_SFLOAT);
    const VkExtent2D extent = image->GetImage().Extent();
    constexpr VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    FExposureTestBuffer state(device, sizeof(glm::vec4),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    Vulkan::PipelineCommon::ZeroBindCustomPushConstantPipeline pipeline(
        renderer.SwapChain(), "assets/shaders/Process.AutoExposure.comp.slang.spv",
        sizeof(Vulkan::FAutoExposurePushConstants));
    Vulkan::FAutoExposurePushConstants params{
        Assets::Bindless::RT_SCENE_COLOR, extent.width, extent.height, 1u,
        state.buffer->GetDeviceAddress(), 1.0f, 1.0f / 60.0f,
        0.18f, -2.0f, 3.0f, 1.0f, 3.0f, 0.0f,
    };

    const auto fill = [&](float luminance)
    {
        Vulkan::SingleTimeCommands::Submit(renderer.CommandPool(), [&](VkCommandBuffer commandBuffer)
        {
            Vulkan::ImageMemoryBarrier::Insert(commandBuffer,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                image->GetImage().Handle(), range, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            const VkClearColorValue color{{luminance, luminance, luminance, 1.0f}};
            vkCmdClearColorImage(commandBuffer, image->GetImage().Handle(),
                                 VK_IMAGE_LAYOUT_GENERAL, &color, 1, &range);
            Vulkan::ImageMemoryBarrier::Insert(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                image->GetImage().Handle(), range, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        });
    };
    const auto meter = [&](uint32_t frames = 1u)
    {
        Vulkan::SingleTimeCommands::Submit(renderer.CommandPool(), [&](VkCommandBuffer commandBuffer)
        {
            for (uint32_t frame = 0; frame < frames; ++frame)
            {
                Vulkan::BufferMemoryBarrier::Insert(commandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    state.buffer->Handle(), VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                pipeline.BindPipeline(commandBuffer, &params);
                vkCmdDispatch(commandBuffer, 1, 1, 1);
                params.reset = 0u;
            }
            Vulkan::BufferMemoryBarrier::Insert(commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                state.buffer->Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
        });
        glm::vec4 result;
        std::memcpy(&result, state.memory->Map(0, sizeof(result)), sizeof(result));
        state.memory->Unmap();
        return result;
    };

    fill(0.18f);
    const glm::vec4 neutral = meter();
    CHECK(neutral.x == Catch::Approx(0.0f).margin(0.13f)); // half-bin EV quantization

    fill(0.005f);
    const glm::vec4 enteringDark = meter();
    CHECK(enteringDark.y == Catch::Approx(3.0f));
    CHECK(enteringDark.x > neutral.x);
    CHECK(enteringDark.x < neutral.x + 0.2f);
    const glm::vec4 adaptedDark = meter(120);
    CHECK(adaptedDark.x > 2.4f);
    CHECK(adaptedDark.x <= 3.0f);

    fill(10.0f);
    const glm::vec4 enteringLight = meter();
    CHECK(enteringLight.y == Catch::Approx(-2.0f));
    CHECK(enteringLight.x < adaptedDark.x);
    const glm::vec4 adaptedLight = meter(120);
    CHECK(adaptedLight.x < -1.9f);
    CHECK(adaptedLight.x >= -2.0f);

    fill(0.0f);
    CHECK(meter().x == Catch::Approx(adaptedLight.x).margin(0.001f));

    // HDR pre-exposure must not change the meter's physical luminance.
    params.reset = 1u;
    params.luminanceScale = 1.0f / 3.45f;
    fill(0.18f * 3.45f);
    CHECK(meter().x == Catch::Approx(neutral.x).margin(0.01f));
    params.luminanceScale = 1.0f;

    // A small glaring light at the edge is discarded by percentile trimming.
    std::vector<std::array<uint16_t, 4>> texels(extent.width * extent.height);
    const uint16_t middle = glm::packHalf1x16(0.18f);
    const uint16_t bright = glm::packHalf1x16(10000.0f);
    const uint16_t alpha = glm::packHalf1x16(1.0f);
    for (uint32_t y = 0; y < extent.height; ++y)
    {
        for (uint32_t x = 0; x < extent.width; ++x)
        {
            const uint16_t value = x < extent.width / 20u ? bright : middle;
            texels[y * extent.width + x] = {value, value, value, alpha};
        }
    }
    const size_t uploadSize = texels.size() * sizeof(texels[0]);
    FExposureTestBuffer upload(device, uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::memcpy(upload.memory->Map(0, uploadSize), texels.data(), uploadSize);
    upload.memory->Unmap();
    Vulkan::SingleTimeCommands::Submit(renderer.CommandPool(), [&](VkCommandBuffer commandBuffer)
    {
        Vulkan::ImageMemoryBarrier::Insert(commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            image->GetImage().Handle(), range, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyBufferToImage(commandBuffer, upload.buffer->Handle(), image->GetImage().Handle(),
                               VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        Vulkan::ImageMemoryBarrier::Insert(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            image->GetImage().Handle(), range, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    });
    params.reset = 1u;
    CHECK(meter().x == Catch::Approx(neutral.x).margin(0.01f));
}
