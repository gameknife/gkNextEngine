#pragma once

#include <memory>
#include "Application/Game/NextDayz/Weapons/AttachmentDefs.hpp"

namespace Assets { class Node; class Scene; }

namespace NextDayz
{
    // Eight detachable parts and the SVD's factory PSO optic share the same
    // geometry in first person, in the hands, and on the holsters.
    struct FAttachmentVisualAssets
    {
        std::array<uint32_t, 9> models{};
        std::array<FWeaponMaterialSlots, 9> materials{};
        bool ready = false;
    };

    class WeaponAttachmentVisuals
    {
    public:
        void Bind(Assets::Scene& scene, const std::shared_ptr<Assets::Node>& parent,
                  const FAttachmentVisualAssets& assets, std::string_view prefix);
        void Update(std::string_view weaponId, const FWeaponAttachments& attachments, bool visible);
        void Clear();
        const auto& Nodes() const { return nodes_; }
        int VisibleCount() const;

    private:
        Assets::Scene* scene_ = nullptr;
        std::weak_ptr<Assets::Node> parent_;
        FAttachmentVisualAssets assets_;
        std::array<std::shared_ptr<Assets::Node>, kAttachmentSlotCount> nodes_{};
        std::array<int, kAttachmentSlotCount> indices_{{-1, -1, -1, -1, -1}};
        std::array<bool, kAttachmentSlotCount> inScene_{};
    };
}
