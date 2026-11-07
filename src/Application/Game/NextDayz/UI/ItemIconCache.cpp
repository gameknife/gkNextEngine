#include "Engine/Common/CoreMinimal.hpp"
#include "ItemIconCache.hpp"
#include "InventoryImageResolve.hpp"

#include "Application/Game/NextDayz/Inventory/LootSystem.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/GpuResources.hpp"

#include <deque>
#include <filesystem>
#include <fstream>

namespace NextDayz
{
    namespace
    {
        constexpr uint64_t cacheMagic = 0x314e4f43495a444eull; // NDZICON1
        constexpr uint32_t iconRevision = 5;
        constexpr float iconFaceTiltDegrees = 6.0f;
        constexpr uint32_t pixelsPerCell = 64;
        constexpr uint32_t sampleBase = Assets::Bindless::RES_THUMBNAIL_BASE;
        constexpr uint32_t unavailable = std::numeric_limits<uint32_t>::max();

        uint64_t HashBytes(const void* data, size_t size, uint64_t hash = 14695981039346656037ull)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
            return hash;
        }

        struct FCacheHeader
        {
            uint64_t magic = cacheMagic;
            uint64_t contentHash = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            uint64_t pixelHash = 0;
        };

        size_t PixelBytes(VkExtent2D extent) { return size_t(extent.width) * extent.height * 8u; }

        glm::vec3 IconViewDirection(std::string_view itemId)
        {
            const auto* def = FindItemDef(itemId);
            // Catalog guns, attachments and folded clothing lie on their broad face.
            // Approach that face rather than lowering the camera toward their edge.
            const bool flatClothing = def && def->kind == EItemKind::Clothing &&
                (def->equipSlot == EEquipSlot::Torso || def->equipSlot == EEquipSlot::Vest || def->equipSlot == EEquipSlot::Legs);
            const bool laidFlat = (def && (IsWieldable(*def) || def->kind == EItemKind::Attachment)) || flatClothing ||
                itemId == "food_chocolate" || itemId == "rag" || itemId == "map" || itemId == "compass" ||
                itemId == "can_opener" || itemId == "firecrackers";
            const float tilt = std::tan(glm::radians(iconFaceTiltDegrees));
            return glm::normalize(laidFlat ? glm::vec3(0.0f, 1.0f, tilt) : glm::vec3(0.0f, tilt, 1.0f));
        }

        uint64_t ContentHash(const Assets::Scene& scene, const FLootVisual& visual, VkExtent2D extent)
        {
            uint64_t hash = HashBytes(&iconRevision, sizeof(iconRevision));
            hash = HashBytes(&extent, sizeof(extent), hash);
            const auto& model = scene.Models().at(visual.modelId);
            for (const auto& vertex : model.CPUVertices())
            {
                hash = HashBytes(&vertex.Position, sizeof(vertex.Position), hash);
                hash = HashBytes(&vertex.Normal, sizeof(vertex.Normal), hash);
                hash = HashBytes(&vertex.MaterialIndex, sizeof(vertex.MaterialIndex), hash);
            }
            hash = HashBytes(model.CPUIndices().data(), model.CPUIndices().size() * sizeof(uint32_t), hash);
            for (const auto id : visual.materials)
            {
                const auto& material = scene.Materials().at(id).gpuMaterial_;
                hash = HashBytes(&material.Diffuse, sizeof(material.Diffuse), hash);
                hash = HashBytes(&material.Metalness, sizeof(material.Metalness), hash);
                hash = HashBytes(&material.Fuzziness, sizeof(material.Fuzziness), hash);
                hash = HashBytes(&material.MaterialModel, sizeof(material.MaterialModel), hash);
            }
            return hash;
        }
    }

    struct ItemIconCache::FState
    {
        struct FSlot
        {
            std::string itemId;
            FLootVisual visual;
            VkExtent2D extent{};
            uint64_t hash = 0;
            std::filesystem::path path;
            std::unique_ptr<Vulkan::RenderImage> image;
            bool checked = false;
            bool ready = false;
            bool pending = false;
            bool initialized = false;
        };

        struct FTransfer
        {
            // Buffers must be destroyed before their bound memory.
            std::unique_ptr<Vulkan::DeviceMemory> memory;
            std::unique_ptr<Vulkan::Buffer> buffer;
            uint64_t submitSerial = 0;
            std::filesystem::path path;
            FCacheHeader header;
        };

        struct FRetiredScene
        {
            std::unique_ptr<Assets::Scene> scene;
            uint64_t submitSerial;
        };

        explicit FState(Vulkan::VulkanBaseRenderer& source) : renderer(source) {}

        Vulkan::VulkanBaseRenderer& renderer;
        Vulkan::FRenderViewHandle view;
        Vulkan::FRenderViewTargetResources target;
        std::unique_ptr<Vulkan::PipelineCommon::ZeroBindCustomPushConstantPipeline> resolvePipeline;
        std::unique_ptr<Assets::Scene> scene;
        std::vector<FRetiredScene> retiredScenes;
        std::vector<FSlot> slots;
        std::unordered_map<std::string, uint32_t> lookup;
        std::deque<uint32_t> pending;
        std::vector<FTransfer> transfers;
        uint32_t rendered = 0;
        uint32_t cacheHits = 0;

        static VkExtent2D RenderExtent(const FSlot& slot)
        {
            const uint32_t side = std::max(slot.extent.width, slot.extent.height) * inventoryImageSampleScale;
            return {side, side};
        }

        static VkOffset2D CropOffset(const FSlot& slot)
        {
            const auto extent = RenderExtent(slot);
            return {static_cast<int32_t>((extent.width - slot.extent.width * inventoryImageSampleScale) / 2u),
                    static_cast<int32_t>((extent.height - slot.extent.height * inventoryImageSampleScale) / 2u)};
        }

        void BeginImageWrite(FSlot& slot, VkCommandBuffer commandBuffer)
        {
            slot.image->InsertBarrier(commandBuffer, slot.initialized ? VK_ACCESS_SHADER_READ_BIT : 0,
                VK_ACCESS_TRANSFER_WRITE_BIT, slot.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            slot.initialized = true;
        }

        FTransfer MakeTransfer(const FSlot& slot, bool download)
        {
            FTransfer transfer;
            transfer.buffer = std::make_unique<Vulkan::Buffer>(renderer.Device(), PixelBytes(slot.extent),
                download ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            transfer.memory = std::make_unique<Vulkan::DeviceMemory>(transfer.buffer->AllocateMemory(
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
            transfer.submitSerial = renderer.RecordingSubmitSerial();
            if (download) transfer.path = slot.path;
            transfer.header = {cacheMagic, slot.hash, slot.extent.width, slot.extent.height, 0};
            return transfer;
        }

        bool LoadCached(FSlot& slot, VkCommandBuffer commandBuffer)
        {
            std::ifstream input(slot.path, std::ios::binary | std::ios::ate);
            if (!input || input.tellg() != static_cast<std::streamoff>(sizeof(FCacheHeader) + PixelBytes(slot.extent)))
                return false;
            input.seekg(0);
            FCacheHeader header;
            input.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (header.magic != cacheMagic || header.contentHash != slot.hash ||
                header.width != slot.extent.width || header.height != slot.extent.height) return false;
            std::vector<uint8_t> pixels(PixelBytes(slot.extent));
            if (!input.read(reinterpret_cast<char*>(pixels.data()), pixels.size()) ||
                HashBytes(pixels.data(), pixels.size()) != header.pixelHash) return false;

            auto transfer = MakeTransfer(slot, false);
            void* mapped = transfer.memory->Map(0, pixels.size());
            std::memcpy(mapped, pixels.data(), pixels.size());
            transfer.memory->Unmap();
            BeginImageWrite(slot, commandBuffer);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {slot.extent.width, slot.extent.height, 1};
            vkCmdCopyBufferToImage(commandBuffer, transfer.buffer->Handle(), slot.image->GetImage().Handle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            slot.image->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            transfers.push_back(std::move(transfer));
            slot.ready = true;
            ++cacheHits;
            return true;
        }

        bool CopyIconOutput(FSlot& slot, VkCommandBuffer commandBuffer, Vulkan::RenderView& view)
        {
            return ResolveInventoryImage(renderer, view, commandBuffer, resolvePipeline,
                *slot.image, slot.initialized, CropOffset(slot));
        }

        void QueueReadback(FSlot& slot, VkCommandBuffer commandBuffer)
        {
            auto transfer = MakeTransfer(slot, true);
            slot.image->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {slot.extent.width, slot.extent.height, 1};
            vkCmdCopyImageToBuffer(commandBuffer, slot.image->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                transfer.buffer->Handle(), 1, &copy);
            slot.image->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0, 1, &hostBarrier, 0, nullptr, 0, nullptr);
            transfers.push_back(std::move(transfer));
        }

        void FinishTransfers()
        {
            const uint64_t completed = renderer.CompletedSubmitSerial();
            std::erase_if(transfers, [completed](FTransfer& transfer)
            {
                if (transfer.submitSerial > completed) return false;
                if (!transfer.path.empty())
                {
                    const size_t bytes = PixelBytes({transfer.header.width, transfer.header.height});
                    void* pixels = transfer.memory->Map(0, bytes);
                    transfer.header.pixelHash = HashBytes(pixels, bytes);
                    auto temporary = transfer.path;
                    temporary += ".tmp";
                    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                    output.write(reinterpret_cast<const char*>(&transfer.header), sizeof(transfer.header));
                    output.write(static_cast<const char*>(pixels), bytes);
                    transfer.memory->Unmap();
                    output.close();
                    std::error_code error;
                    if (output)
                    {
                        // Replace a rejected/corrupt cache entry on Windows as well.
                        std::filesystem::remove(transfer.path, error);
                        error.clear();
                        std::filesystem::rename(temporary, transfer.path, error);
                    }
                    if (!output || error)
                    {
                        // A read-only/full cache must not prevent the GPU icon from being used.
                        SPDLOG_WARN("[NextDayz] cannot cache item icon at {}", transfer.path.string());
                        std::filesystem::remove(temporary, error);
                    }
                }
                return true;
            });
            std::erase_if(retiredScenes, [completed](const auto& retired) { return retired.submitSerial <= completed; });
        }

        void BuildScene(const FSlot& slot)
        {
            if (scene) retiredScenes.push_back({std::move(scene), renderer.RecordingSubmitSerial() + 1u});
            scene = std::make_unique<Assets::Scene>(renderer.CommandPool(), false, false, false, false);
            const auto& source = renderer.GetScene();
            const auto& model = source.Models().at(slot.visual.modelId);
            const glm::vec3 center = (model.GetLocalAABBMin() + model.GetLocalAABBMax()) * 0.5f;
            std::vector<Assets::Model> models{model};
            std::vector<Assets::FMaterial> materials;
            std::array<uint32_t, 16> sections{};
            std::unordered_map<uint32_t, uint32_t> materialMap;
            for (size_t i = 0; i < sections.size(); ++i)
            {
                const auto id = slot.visual.materials[i];
                if (!materialMap.contains(id))
                {
                    materialMap[id] = static_cast<uint32_t>(materials.size());
                    materials.push_back(source.Materials().at(id));
                }
                sections[i] = materialMap.at(id);
            }
            std::vector<std::shared_ptr<Assets::Node>> nodes{Assets::SceneBuilder::CreateRenderNode(
                "Item icon", -center, glm::vec3(1.0f), 1u, 0u, sections, true)};
            std::vector<Assets::LightObject> lights;
            std::vector<Assets::AnimationTrack> tracks;
            scene->Reload(nodes, models, materials, lights, tracks);
            scene->PostLoad({});
            scene->RebuildMeshBuffer(renderer.CommandPool(), false);
            auto& env = scene->GetEnvSettings();
            env.Reset();
            env.HasSky = env.HasSun = true;
            env.BackgroundMode = Assets::EBackgroundMode::Studio;
            env.SunIntensity = 800.0f;
            env.SunRotation = 0.35f;
            env.SkyIntensity = 400.0f;
            scene->UpdateHDRSH();

            const glm::vec3 half = glm::max((model.GetLocalAABBMax() - model.GetLocalAABBMin()) * 0.5f, glm::vec3(0.001f));
            // Keep the authored X axis parallel to the image plane. A lateral camera
            // offset makes gun barrels slope across the icon. A small offset from
            // the item's front face keeps its profile clear, with a little depth.
            const glm::vec3 toEye = IconViewDirection(slot.itemId);
            glm::vec3 right = glm::normalize(glm::cross(-toEye, glm::vec3(0, 1, 0)));
            glm::vec3 up = glm::cross(right, -toEye);
            const float width = glm::dot(glm::abs(right), half);
            const float height = glm::dot(glm::abs(up), half);
            if ((slot.extent.width > slot.extent.height && height > width) ||
                (slot.extent.height > slot.extent.width && width > height))
            {
                const auto previousRight = right;
                right = -up;
                up = previousRight;
            }
            Assets::Camera camera{};
            camera.name = "Item icon";
            camera.FieldOfView = 32.0f;
            camera.Aperture = 0;
            const float tangent = std::tan(glm::radians(camera.FieldOfView) * 0.5f);
            const float longest = static_cast<float>(std::max(slot.extent.width, slot.extent.height));
            const glm::vec2 fraction(slot.extent.width / longest, slot.extent.height / longest);
            float distance = 0.01f;
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 point(corner & 1 ? half.x : -half.x, corner & 2 ? half.y : -half.y,
                                      corner & 4 ? half.z : -half.z);
                const float span = std::max(std::abs(glm::dot(point, right)) / fraction.x,
                                            std::abs(glm::dot(point, up)) / fraction.y);
                distance = std::max(distance, glm::dot(point, toEye) + span / tangent * 1.12f);
            }
            camera.FocalDistance = distance;
            camera.NearPlane = std::max(0.001f, distance - glm::length(half) * 1.5f);
            camera.FarPlane = distance + glm::length(half) * 2.0f + 1.0f;
            camera.ModelView = glm::lookAt(toEye * distance, glm::vec3(0), up);
            scene->GetRenderCamera() = camera;
            scene->SyncUpdateScene();
        }
    };

    ItemIconCache::ItemIconCache(Vulkan::VulkanBaseRenderer& renderer) : state_(std::make_unique<FState>(renderer)) {}

    ItemIconCache::~ItemIconCache()
    {
        Vulkan::RenderViewResourceFactory(state_->renderer).DestroyView(state_->view);
        if (auto* pool = Assets::GlobalTexturePool::GetInstance())
            for (uint32_t i = 0; i < state_->slots.size(); ++i) pool->BindDefaultSampleTexture(sampleBase + i);
    }

    uint32_t ItemIconCache::Request(std::string_view itemId, const FLootVisual& visual)
    {
        auto& state = *state_;
        const auto* def = FindItemDef(itemId);
        if (!def || !visual.valid || visual.modelId >= state.renderer.GetScene().Models().size()) return unavailable;
        const std::string id(itemId);
        auto found = state.lookup.find(id);
        if (found == state.lookup.end())
        {
            const auto index = static_cast<uint32_t>(state.slots.size());
            if (index >= Assets::Bindless::RES_THUMBNAIL_COUNT || sampleBase + index >= state.renderer.BindlessProfile().sampledTextureSlots)
                return unavailable;
            found = state.lookup.emplace(id, index).first;
            state.slots.emplace_back();
            state.slots.back().itemId = id;
            state.slots.back().extent = {static_cast<uint32_t>(def->size.width) * pixelsPerCell,
                                         static_cast<uint32_t>(def->size.height) * pixelsPerCell};
        }
        auto& slot = state.slots[found->second];
        if (!slot.checked)
        {
            const uint64_t hash = ContentHash(state.renderer.GetScene(), visual, slot.extent);
            if (hash != slot.hash) slot.ready = false;
            slot.hash = hash;
            slot.visual = visual;
            slot.path = Utilities::FileHelper::GetWritableFilePath(
                fmt::format("cache/item-icons/{}-{:016x}.thumb", itemId, hash).c_str());
            slot.checked = true;
        }
        if (slot.ready) return sampleBase + found->second;
        if (!slot.pending)
        {
            slot.pending = true;
            state.pending.push_back(found->second);
        }
        return unavailable;
    }

    bool ItemIconCache::IsReady(std::string_view id) const
    {
        const auto found = state_->lookup.find(std::string(id));
        return found != state_->lookup.end() && state_->slots[found->second].ready;
    }

    uint32_t ItemIconCache::ReadyCount() const
    {
        return static_cast<uint32_t>(std::count_if(state_->slots.begin(), state_->slots.end(), [](const auto& slot) { return slot.ready; }));
    }
    uint32_t ItemIconCache::RenderedCount() const { return state_->rendered; }
    uint32_t ItemIconCache::CacheHitCount() const { return state_->cacheHits; }
    bool ItemIconCache::HasWork() const { return !state_->pending.empty(); }
    void ItemIconCache::BeforeNextFrame() { state_->FinishTransfers(); }

    void ItemIconCache::OnMainSceneChanged()
    {
        auto& state = *state_;
        Vulkan::RenderViewResourceFactory(state.renderer).DestroyView(state.view);
        state.target.ResetSwapChainResources(false);
        state.resolvePipeline.reset();
        state.scene.reset();
        state.retiredScenes.clear();
        state.pending.clear();
        for (auto& slot : state.slots) slot.checked = slot.pending = false;
    }

    void ItemIconCache::OnSwapChainResourcesInvalidated(bool)
    {
        state_->target.ResetSwapChainResources(false);
        state_->resolvePipeline.reset();
        if (auto* view = state_->renderer.RenderViews().Resolve(state_->view))
        {
            view->SetSceneOverride(nullptr);
            view->SetVisibilityFramebuffer(nullptr);
        }
    }

    bool ItemIconCache::ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        auto& state = *state_;
        if (state.pending.empty()) return false;
        const uint32_t index = state.pending.front();
        state.pending.pop_front();
        auto& slot = state.slots[index];
        slot.pending = false;
        Vulkan::RenderViewResourceFactory resources(state.renderer);
        if (!state.target.offscreenSampler) state.target.offscreenSampler = resources.CreateClampSampler();
        if (!slot.image)
        {
            slot.image = std::make_unique<Vulkan::RenderImage>(state.renderer.Device(), slot.extent,
                VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                false, "NextDayz item icon");
            resources.BindSampledColorImage(sampleBase + index, *slot.image, *state.target.offscreenSampler);
        }
        if (state.LoadCached(slot, commandBuffer)) return false;

        state.BuildScene(slot);
        const VkExtent2D renderExtent = FState::RenderExtent(slot);
        Vulkan::FViewDesc desc{};
        desc.renderExtent = renderExtent;
        desc.schedule = Vulkan::EViewSchedule::Transient;
        auto& view = resources.EnsureView(state.view, desc, "NextDayz item thumbnail", false);
        if (!state.target.visibilityFramebuffer || view.AllocatedExtent().width != renderExtent.width ||
            view.AllocatedExtent().height != renderExtent.height)
            state.target.visibilityFramebuffer = resources.RebuildVisibilityFramebuffer(view, renderExtent);
        auto* logic = state.renderer.EnsureLogicRenderer(Vulkan::ERT_SoftwareModernNoAmbient);
        if (!logic) return false;
        const auto ubo = Vulkan::BuildViewCameraUbo({
            .scene = *state.scene, .camera = state.scene->GetRenderCamera(), .extent = renderExtent,
            .cascadeDistance = 5.0f, .totalFrames = 1, .fillSceneLighting = true, .thumbnailDefaults = true,
        });
        state.renderer.SetRenderViewUbo(view, imageIndex, ubo);
        view.SetVisibilityFramebuffer(state.target.visibilityFramebuffer.get());
        view.SetSceneOverride(state.scene.get());
        view.SetPrevDepthValid(false);
        state.renderer.ScheduleRenderView(view, *logic, false, [this, commandBuffer, index](Vulkan::RenderView& rendered)
        {
            auto& state = *state_;
            auto& slot = state.slots[index];
            slot.ready = state.CopyIconOutput(slot, commandBuffer, rendered);
            if (slot.ready)
            {
                ++state.rendered;
                state.QueueReadback(slot, commandBuffer);
            }
            rendered.SetSceneOverride(nullptr);
        });
        return true;
    }

    ItemIconCache& GetItemIconCache(Vulkan::VulkanBaseRenderer& renderer)
    {
        auto& services = renderer.ViewServices();
        if (auto* provider = services.FindProvider("NextDayzItemIcons")) return static_cast<ItemIconCache&>(*provider);
        return static_cast<ItemIconCache&>(*services.RegisterProvider(
            "NextDayzItemIcons", 10, std::make_unique<ItemIconCache>(renderer)));
    }
}
