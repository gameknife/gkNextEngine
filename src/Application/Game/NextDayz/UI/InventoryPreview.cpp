#include "Engine/Common/CoreMinimal.hpp"
#include "InventoryPreview.hpp"
#include "InventoryImageResolve.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <unordered_map>

namespace NextDayz
{
    InventoryPreview::InventoryPreview(Vulkan::VulkanBaseRenderer& renderer) : renderer_(renderer) {}

    InventoryPreview::~InventoryPreview()
    {
        Vulkan::RenderViewResourceFactory(renderer_).DestroyView(view_);
    }

    uint32_t InventoryPreview::SampleSlot()
    {
        // NextDayz has no editor material preview; this application owns that preview slot.
        return static_cast<uint32_t>(Assets::Bindless::RES_MATERIAL_PREVIEW);
    }

    void InventoryPreview::Request(const std::shared_ptr<Assets::Node>& root, VkExtent2D extent,
                                   float yaw, float distance)
    {
        if (root_.lock() != root) ready_ = false;
        root_ = root;
        // Bound the displayed image at high DPI; rendering uses twice this extent per axis.
        const float scale = std::min({1.0f, 768.0f / std::max(1u, extent.width), 1024.0f / std::max(1u, extent.height)});
        extent_ = {std::max(1u, static_cast<uint32_t>(extent.width * scale)),
                   std::max(1u, static_cast<uint32_t>(extent.height * scale))};
        yaw_ = yaw;
        distance_ = distance;
        requested_ = true;
    }

    void InventoryPreview::BeforeNextFrame()
    {
        const uint64_t completed = renderer_.CompletedSubmitSerial();
        std::erase_if(retiredScenes_, [completed](const FRetiredScene& retired)
        {
            return retired.releaseAfterSubmitSerial <= completed;
        });
    }

    void InventoryPreview::OnMainSceneChanged()
    {
        // Main-scene commits already wait for the GPU. Drop geometry and old render-pass references.
        Vulkan::RenderViewResourceFactory(renderer_).DestroyView(view_);
        target_.ResetSwapChainResources(false);
        resolvePipeline_.reset();
        scene_.reset();
        retiredScenes_.clear();
        nodes_.clear();
        meshKeys_.clear();
        root_.reset();
        requested_ = ready_ = false;
    }

    void InventoryPreview::OnSwapChainResourcesInvalidated(bool releaseSampledOutputs)
    {
        target_.ResetSwapChainResources(releaseSampledOutputs);
        if (releaseSampledOutputs) outputInitialized_ = false;
        resolvePipeline_.reset();
        if (auto* view = renderer_.RenderViews().Resolve(view_))
        {
            view->SetSceneOverride(nullptr);
            view->SetVisibilityFramebuffer(nullptr);
        }
        ready_ = false;
    }

    void InventoryPreview::RebuildScene(const std::vector<Assets::Node*>& sources,
                                       const std::vector<FMeshKey>& keys)
    {
        if (scene_)
        {
            // UI and scene changes may precede the next fence wait. Keep old GPU buffers in flight.
            retiredScenes_.push_back({std::move(scene_), renderer_.RecordingSubmitSerial() + 1u});
        }
        scene_ = std::make_unique<Assets::Scene>(renderer_.CommandPool(), false, false, false, false);
        nodes_.clear();
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::unordered_map<uint32_t, uint32_t> modelMap, materialMap;
        const auto& sourceScene = renderer_.GetScene();
        for (size_t i = 0; i < sources.size(); ++i)
        {
            const FMeshKey& key = keys[i];
            if (!modelMap.contains(key.model))
            {
                modelMap[key.model] = static_cast<uint32_t>(models.size());
                models.push_back(sourceScene.Models().at(key.model));
            }
            std::array<uint32_t, 16> slots{};
            for (size_t section = 0; section < slots.size(); ++section)
            {
                const uint32_t id = key.materials[section];
                if (!materialMap.contains(id))
                {
                    materialMap[id] = static_cast<uint32_t>(materials.size());
                    materials.push_back(sourceScene.Materials().at(id));
                }
                slots[section] = materialMap.at(id);
            }
            nodes_.push_back(Assets::SceneBuilder::CreateRenderNode(
                sources[i]->GetName(), glm::vec3(0.0f), glm::vec3(1.0f),
                static_cast<uint32_t>(i + 1), modelMap.at(key.model), slots, false));
        }
        // Reload takes the staging arrays; keep handles to the preview nodes for subsequent poses.
        auto sceneNodes = nodes_;
        scene_->Reload(sceneNodes, models, materials, lights, tracks);
        scene_->PostLoad({});
        scene_->RebuildMeshBuffer(renderer_.CommandPool(), false);
        auto& env = scene_->GetEnvSettings();
        env.Reset();
        env.HasSky = true;
        env.BackgroundMode = Assets::EBackgroundMode::Studio;
        env.HasSun = true;
        env.SunIntensity = 700.0f;
        env.SunRotation = 0.35f;
        env.SkyIntensity = 350.0f;
        scene_->UpdateHDRSH();
        meshKeys_ = keys;
    }

    bool InventoryPreview::ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        const auto root = root_.lock();
        if (!requested_ || !root) return false;

        std::vector<Assets::Node*> sources;
        std::vector<FMeshKey> keys;
        const auto collect = [&](auto&& self, Assets::Node& node) -> void
        {
            if (const auto* render = node.GetComponent<Runtime::RenderComponent>(); render && render->IsDrawable())
            {
                sources.push_back(&node);
                keys.push_back({render->GetModelId(), render->GetMaterials()});
            }
            for (const auto& child : node.Children()) self(self, *child);
        };
        collect(collect, *root);
        if (sources.empty()) return false;
        if (!scene_ || keys != meshKeys_) RebuildScene(sources, keys);

        const glm::mat4 toLocal = glm::inverse(root->WorldTransform());
        glm::vec3 boundsMin(std::numeric_limits<float>::max());
        glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
        for (size_t i = 0; i < sources.size(); ++i)
        {
            const glm::mat4 transform = toLocal * sources[i]->WorldTransform();
            glm::vec3 scale, position, skew;
            glm::quat rotation;
            glm::vec4 perspective;
            glm::decompose(transform, scale, rotation, position, skew, perspective);
            nodes_[i]->SetTransform(position, rotation, scale);
            // Copy wardrobe visibility, but leave camera fading and first-person opacity behind.
            const bool visible = sources[i]->GetComponent<Runtime::RenderComponent>()->GetVisible();
            nodes_[i]->GetComponent<Runtime::RenderComponent>()->SetVisible(visible);
            if (visible)
            {
                const auto& model = renderer_.GetScene().Models().at(keys[i].model);
                for (int corner = 0; corner < 8; ++corner)
                {
                    const glm::vec3 local(corner & 1 ? model.GetLocalAABBMax().x : model.GetLocalAABBMin().x,
                                          corner & 2 ? model.GetLocalAABBMax().y : model.GetLocalAABBMin().y,
                                          corner & 4 ? model.GetLocalAABBMax().z : model.GetLocalAABBMin().z);
                    const glm::vec3 point(transform * glm::vec4(local, 1.0f));
                    boundsMin = glm::min(boundsMin, point);
                    boundsMax = glm::max(boundsMax, point);
                }
            }
        }
        scene_->MarkDirty();
        scene_->SyncUpdateScene();

        Assets::Camera camera{};
        camera.name = "NextDayz Loadout";
        camera.FieldOfView = 36.0f;
        camera.NearPlane = 0.05f;
        camera.FarPlane = 20.0f;
        camera.Aperture = 0.0f;
        if (boundsMin.x > boundsMax.x) return false;
        const glm::vec3 focus = (boundsMin + boundsMax) * 0.5f;
        const glm::vec3 toEye = glm::normalize(glm::vec3(std::sin(yaw_), 0.12f, std::cos(yaw_)));
        const glm::vec3 right = glm::normalize(glm::cross(-toEye, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::cross(right, -toEye);
        const float tanHalfFov = std::tan(glm::radians(camera.FieldOfView) * 0.5f);
        const float aspect = static_cast<float>(extent_.width) / extent_.height;
        float fitDistance = 3.4f;
        // Frame the complete outfit, including the muzzle of a long rifle on the back.
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 offset = glm::vec3(corner & 1 ? boundsMax.x : boundsMin.x,
                                              corner & 2 ? boundsMax.y : boundsMin.y,
                                              corner & 4 ? boundsMax.z : boundsMin.z) - focus;
            const float span = std::max(std::abs(glm::dot(offset, right)) / aspect,
                                        std::abs(glm::dot(offset, up))) / tanHalfFov;
            fitDistance = std::max(fitDistance, glm::dot(offset, toEye) + span * 1.08f);
        }
        camera.FocalDistance = fitDistance * distance_ / 3.4f;
        const glm::vec3 eye = focus + toEye * camera.FocalDistance;
        camera.ModelView = glm::lookAt(eye, focus, glm::vec3(0.0f, 1.0f, 0.0f));

        Vulkan::RenderViewResourceFactory resources(renderer_);
        const VkExtent2D renderExtent{extent_.width * inventoryImageSampleScale, extent_.height * inventoryImageSampleScale};
        Vulkan::FViewDesc desc{};
        desc.renderExtent = renderExtent;
        desc.schedule = Vulkan::EViewSchedule::OnDemand;
        auto& view = resources.EnsureView(view_, desc, "NextDayz loadout preview", false);
        if (view.AllocatedExtent().width != renderExtent.width || view.AllocatedExtent().height != renderExtent.height ||
            !target_.visibilityFramebuffer)
        {
            target_.visibilityFramebuffer = resources.RebuildVisibilityFramebuffer(view, renderExtent);
        }
        if (!target_.offscreenImage || target_.offscreenImage->GetImage().Extent().width != extent_.width ||
            target_.offscreenImage->GetImage().Extent().height != extent_.height)
        {
            target_.offscreenImage = resources.CreateSampledColorImage(extent_, "NextDayz Loadout SSAA");
            if (!target_.offscreenSampler) target_.offscreenSampler = resources.CreateClampSampler();
            target_.outputSampleSlot = SampleSlot();
            resources.BindSampledColorImage(SampleSlot(), *target_.offscreenImage, *target_.offscreenSampler);
            outputInitialized_ = false;
        }
        auto* logic = renderer_.EnsureLogicRenderer(Vulkan::ERT_SoftwareModernNoAmbient);
        if (!logic) return false;
        const auto ubo = Vulkan::BuildViewCameraUbo({
            .scene = *scene_, .camera = camera, .extent = renderExtent, .cascadeDistance = 10.0f,
            .totalFrames = 1, .fillSceneLighting = true, .thumbnailDefaults = true,
        });
        renderer_.SetRenderViewUbo(view, imageIndex, ubo);
        view.SetVisibilityFramebuffer(target_.visibilityFramebuffer.get());
        view.SetSceneOverride(scene_.get());
        view.SetPrevDepthValid(false);
        renderer_.ScheduleRenderView(view, *logic, false, [this, commandBuffer](Vulkan::RenderView& rendered)
        {
            ready_ = ResolveInventoryImage(renderer_, rendered, commandBuffer, resolvePipeline_,
                *target_.offscreenImage, outputInitialized_);
            rendered.SetSceneOverride(nullptr);
        });
        return false;
    }

    InventoryPreview& GetInventoryPreview(Vulkan::VulkanBaseRenderer& renderer)
    {
        auto& services = renderer.ViewServices();
        if (auto* provider = services.FindProvider("NextDayzLoadout"))
            return static_cast<InventoryPreview&>(*provider);
        return static_cast<InventoryPreview&>(*services.RegisterProvider(
            "NextDayzLoadout", 5, std::make_unique<InventoryPreview>(renderer)));
    }
}
