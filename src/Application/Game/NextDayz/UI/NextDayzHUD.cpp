#include "Engine/Common/CoreMinimal.hpp"
#include <map>
#include "Application/Game/NextDayz/UI/Localization.hpp"
#include "NextDayzHUD.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/constants.hpp>

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponSystem.hpp"
#include "HUDTheme.hpp"
#include "HUDWidgets.hpp"
#include "Modules/NextUI/Widgets.hpp"

namespace NextDayz::NextDayzHUD
{
    namespace
    {
        constexpr const char* kItemPayload = "ND_ITEM";
        constexpr const char* kLootPayload = "ND_LOOT";

        struct FInventoryDrag
        {
            FItemInstanceId item = 0;
            int loot = -1;
            bool rotated = false;
            int anchorX = 0;
            int anchorY = 0;
        };
        FInventoryDrag InventoryDrag;

        // Agent validation addresses the inventory by name instead of by pixel: every
        // drop target records where it was drawn this frame (see NextDayzHUD::AgentTargets).
        struct FAgentTarget
        {
            ImVec2 min;
            ImVec2 max;
            bool visible = true;
        };
        std::map<std::string, FAgentTarget> AgentTargetsDrawing;
        std::map<std::string, FAgentTarget> AgentTargetsShown;
        ImVec2 AgentDisplay(1.0f, 1.0f);
        std::string AgentReveal;

        void MarkTarget(const std::string& key, const ImVec2& min, const ImVec2& max)
        {
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 clipMin = draw->GetClipRectMin();
            const ImVec2 clipMax = draw->GetClipRectMax();
            AgentTargetsDrawing[key] = {min, max, center.x >= clipMin.x && center.x <= clipMax.x &&
                                                  center.y >= clipMin.y && center.y <= clipMax.y};
            if (key == AgentReveal)
            {
                ImGui::SetScrollFromPosY(center.y - ImGui::GetWindowPos().y, 0.5f);
                AgentReveal.clear();
            }
        }

        void MarkLastItem(const std::string& key) { MarkTarget(key, ImGui::GetItemRectMin(), ImGui::GetItemRectMax()); }

        // A menu entry agent scripts can find by its English label ("menu:Drop All").
        bool TargetMenuItem(const char* source, bool enabled = true)
        {
            const bool chosen = ImGui::MenuItem(L10n::Label(source), nullptr, false, enabled);
            MarkLastItem(std::string("menu:") + source);
            return chosen;
        }

        const char* EquipTargetName(EEquipSlot slot)
        {
            switch (slot)
            {
            case EEquipSlot::Head: return "head";
            case EEquipSlot::Torso: return "torso";
            case EEquipSlot::Vest: return "vest";
            case EEquipSlot::Legs: return "legs";
            case EEquipSlot::Back: return "backpack";
            default: return "gear";
            }
        }

        void StartItemDrag(const FItemInstance& item, ImVec2 origin, float cellSize = 0.0f)
        {
            if (InventoryDrag.item == item.instanceId && InventoryDrag.loot == -1) return;
            const auto size = Inventory::InstanceSize(item);
            const ImVec2 current = ImGui::GetMousePos();
            const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
            const ImVec2 mouse(current.x - delta.x, current.y - delta.y);
            InventoryDrag = {item.instanceId, -1, item.placement.rotated,
                cellSize > 0.0f ? std::clamp(static_cast<int>((mouse.x - origin.x) / cellSize), 0, size.width - 1) : 0,
                cellSize > 0.0f ? std::clamp(static_cast<int>((mouse.y - origin.y) / cellSize), 0, size.height - 1) : 0};
        }

        std::string FitText(std::string_view text, float width)
        {
            return NextUI::TruncateWithEllipsis(text, width);
        }

        bool DrawItemIcon(const FHudContext& context, std::string_view id, ImVec2 min, ImVec2 max, bool rotated = false,
                          ImDrawList* drawList = nullptr)
        {
            const auto* def = FindItemDef(id);
            const uint64_t texture = context.itemIcon ? context.itemIcon(id) : 0;
            if (!def || !texture) return false;
            const float width = static_cast<float>(rotated ? def->size.height : def->size.width);
            const float height = static_cast<float>(rotated ? def->size.width : def->size.height);
            const float scale = std::max(0.0f, std::min((max.x - min.x) / width, (max.y - min.y) / height));
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const ImVec2 a(center.x - width * scale * 0.5f, center.y - height * scale * 0.5f);
            const ImVec2 b(center.x + width * scale * 0.5f, center.y + height * scale * 0.5f);
            ImDrawList* target = drawList ? drawList : ImGui::GetWindowDrawList();
            target->AddImageQuad(static_cast<ImTextureID>(texture), a, ImVec2(b.x, a.y), b,
                ImVec2(a.x, b.y), rotated ? ImVec2(0, 1) : ImVec2(0, 0), rotated ? ImVec2(0, 0) : ImVec2(1, 0),
                rotated ? ImVec2(1, 0) : ImVec2(1, 1), rotated ? ImVec2(1, 1) : ImVec2(0, 1));
            return true;
        }

        void ItemIconPreview(const FHudContext& context, const FItemDef& def, bool rotated, float longestSide)
        {
            const auto size = rotated ? def.size.Rotated() : def.size;
            const float cell = longestSide / std::max(size.width, size.height);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 extent(size.width * cell, size.height * cell);
            if (DrawItemIcon(context, def.id, pos, ImVec2(pos.x + extent.x, pos.y + extent.y), rotated))
                ImGui::Dummy(extent);
        }

        void AcceptContainerDrop(const FHudContext& context, FContainerId containerId)
        {
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kItemPayload))
                {
                    if (context.moveItem)
                    {
                        context.moveItem(*static_cast<const FItemInstanceId*>(payload->Data), containerId);
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kLootPayload))
                {
                    if (context.takeLoot)
                    {
                        context.takeLoot(*static_cast<const int*>(payload->Data), containerId, EEquipSlot::None);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        std::string ItemName(std::string_view itemId)
        {
            const FItemDef* definition = FindItemDef(itemId);
            return definition ? std::string(NextDayz::L10n::Tr(definition->displayName)) : std::string(itemId);
        }

        // Bearing in degrees clockwise from north. Engine +Z is south on the map.
        float BearingDegrees(float yawRadians)
        {
            const float degrees = glm::degrees(std::atan2(std::sin(yawRadians), -std::cos(yawRadians)));
            return degrees < 0.0f ? degrees + 360.0f : degrees;
        }

        // ====================================================================
        // CROSSHAIR & DAMAGE FEEDBACK
        // ====================================================================
        // A ring whose colour runs from col0 at r0 to col1 at r1 (radial gradient).
        void RadialBand(ImDrawList* draw, ImVec2 center, float r0, float r1, ImU32 col0, ImU32 col1, int segments = 96)
        {
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            draw->PrimReserve(segments * 6, segments * 4);
            for (int i = 0; i < segments; ++i)
            {
                const float a = 6.2831853f * i / segments;
                const float b = 6.2831853f * (i + 1) / segments;
                const ImDrawIdx base = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
                draw->PrimWriteVtx(ImVec2(center.x + std::cos(a) * r0, center.y + std::sin(a) * r0), uv, col0);
                draw->PrimWriteVtx(ImVec2(center.x + std::cos(a) * r1, center.y + std::sin(a) * r1), uv, col1);
                draw->PrimWriteVtx(ImVec2(center.x + std::cos(b) * r1, center.y + std::sin(b) * r1), uv, col1);
                draw->PrimWriteVtx(ImVec2(center.x + std::cos(b) * r0, center.y + std::sin(b) * r0), uv, col0);
                draw->PrimWriteIdx(base);
                draw->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
                draw->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
                draw->PrimWriteIdx(base);
                draw->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
                draw->PrimWriteIdx(static_cast<ImDrawIdx>(base + 3));
            }
        }

        ImU32 WithAlpha(ImU32 color, float alpha)
        {
            const int a = static_cast<int>(((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f));
            return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
        }

        // An illuminated reticle mark: a soft glow under a crisp core.
        void GlowLine(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color, float width, float alpha)
        {
            draw->AddLine(a, b, WithAlpha(color, 0.18f * alpha), width * 4.0f);
            draw->AddLine(a, b, WithAlpha(color, 0.35f * alpha), width * 2.0f);
            draw->AddLine(a, b, WithAlpha(color, alpha), width);
        }

        void GlowDot(ImDrawList* draw, ImVec2 p, ImU32 color, float radius, float alpha)
        {
            draw->AddCircleFilled(p, radius * 4.0f, WithAlpha(color, 0.07f * alpha), 24);
            draw->AddCircleFilled(p, radius * 2.2f, WithAlpha(color, 0.22f * alpha), 20);
            draw->AddCircleFilled(p, radius, WithAlpha(color, alpha), 16);
            draw->AddCircleFilled(p, radius * 0.45f, WithAlpha(IM_COL32(255, 220, 200, 255), 0.85f * alpha), 12);
        }

        // Magnified optic. As the eye closes on the eyepiece the housing closes
        // in from the screen edges with a soft edge, then the lens settles:
        // a darker rim, a thin glint and the reticle. The eye box drifts with
        // look lag, so turning shades one side of the lens (scope shadow).
        void DrawScope(const FHudContext& context)
        {
            const float blend = context.scopeBlend;
            if (blend <= 0.0f || !context.firstPerson || context.showInventory || context.showMap) return;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImVec2 screenCenter = viewport->GetCenter();
            const float lensRadius = std::min(viewport->Size.x, viewport->Size.y) * 0.455f;
            const float diagonal = 0.5f * std::hypot(viewport->Size.x, viewport->Size.y);
            const float outside = diagonal * 2.2f;
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            const float closing = 1.0f - std::pow(1.0f - std::clamp(blend, 0.0f, 1.0f), 3.0f);
            const float settled = glm::smoothstep(0.65f, 1.0f, blend);
            const glm::vec2 reticle = glm::vec2(screenCenter.x, screenCenter.y) + context.scopeReticlePx * settled;
            const glm::vec2 drift = context.scopeEyeBoxPx * settled;
            const ImVec2 lens(reticle.x + drift.x, reticle.y + drift.y);
            const float radius = glm::mix(diagonal * 1.15f, lensRadius, closing);
            const float softness = glm::mix(diagonal * 0.45f, lensRadius * 0.10f, closing);
            const float alpha = glm::smoothstep(0.0f, 0.35f, blend);
            const ImU32 housing = IM_COL32(5, 6, 7, 255);

            const ImDrawListFlags flags = draw->Flags;
            draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            RadialBand(draw, lens, radius, outside, WithAlpha(housing, alpha), WithAlpha(housing, alpha));
            RadialBand(draw, lens, radius - softness, radius, WithAlpha(housing, 0.0f), WithAlpha(housing, alpha));
            draw->Flags = flags;
            if (settled <= 0.0f) return;

            // Lens: rim darkening, tube wall and a faint glint at the top left.
            RadialBand(draw, lens, lensRadius * 0.80f, lensRadius * 0.985f, IM_COL32(0, 0, 0, 0),
                       WithAlpha(IM_COL32(0, 0, 0, 150), settled));
            draw->AddCircle(lens, lensRadius * 0.992f, WithAlpha(IM_COL32(48, 52, 56, 255), settled), 128, 3.0f);
            draw->PathArcTo(lens, lensRadius * 0.955f, 3.55f, 4.45f, 32);
            draw->PathStroke(WithAlpha(IM_COL32(170, 200, 230, 60), settled), 0, 2.0f);

            // Reticle, sized to the lens so it reads the same at every resolution.
            const float unit = lensRadius;
            const float thin = std::max(1.2f, lensRadius / 330.0f);
            const ImU32 ink = IM_COL32(8, 10, 8, 240);
            const ImU32 red = IM_COL32(255, 52, 36, 255);
            const auto at = [&](float x, float y) { return ImVec2(reticle.x + x * unit, reticle.y + y * unit); };
            const auto line = [&](float x0, float y0, float x1, float y1, float width) {
                draw->AddLine(at(x0, y0), at(x1, y1), WithAlpha(ink, settled), width);
            };
            if (!context.factoryScope)
            {
                // ACOG-style: black posts stop short of an illuminated chevron;
                // shrinking bullet-drop bars hang below it.
                line(-1.0f, 0.0f, -0.16f, 0.0f, thin * 2.2f);
                line(0.16f, 0.0f, 1.0f, 0.0f, thin * 2.2f);
                line(0.0f, 1.0f, 0.0f, 0.45f, thin * 2.2f);
                line(0.0f, 0.42f, 0.0f, 0.06f, thin);
                for (int i = 1; i <= 4; ++i)
                {
                    const float y = 0.06f + 0.085f * i;
                    const float half = 0.055f - 0.009f * i;
                    line(-half, y, half, y, thin);
                }
                GlowLine(draw, at(-0.032f, 0.030f), at(0.0f, 0.0f), red, thin * 1.6f, settled);
                GlowLine(draw, at(0.0f, 0.0f), at(0.032f, 0.030f), red, thin * 1.6f, settled);
                return;
            }
            // PSO-1: chevron (its apex is the aim point), range chevrons below,
            // a split horizontal line with windage ticks and the stadia curve.
            for (int i = 0; i < 4; ++i)
            {
                const float y = i * 0.085f;
                line(-0.027f, y + 0.027f, 0.0f, y, thin * 1.4f);
                line(0.0f, y, 0.027f, y + 0.027f, thin * 1.4f);
            }
            line(-0.75f, 0.0f, -0.12f, 0.0f, thin * 1.4f);
            line(0.12f, 0.0f, 0.75f, 0.0f, thin * 1.4f);
            for (int i = 2; i <= 7; ++i)
            {
                const float x = i * 0.1f;
                line(-x, -0.012f, -x, 0.012f, thin);
                line(x, -0.012f, x, 0.012f, thin);
            }
            for (int i = 0; i < 12; ++i)
            {
                const float x0 = -0.62f + i * 0.06f;
                const float x1 = x0 + 0.06f;
                const auto curve = [](float x) { return 0.30f + 0.10f * std::sqrt(std::max(0.0f, 0.62f + x) / 0.72f); };
                line(x0, curve(x0), x1, curve(x1), thin);
            }
        }

        // Iron sights and 1x optics: the periphery dims a little as the eye
        // settles behind the sight, pulling attention to the centre.
        void DrawAdsVignette(const FHudContext& context)
        {
            const float amount = std::clamp(context.adsVignette, 0.0f, 1.0f);
            if (amount <= 0.0f || context.showInventory || context.showMap) return;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float diagonal = 0.5f * std::hypot(viewport->Size.x, viewport->Size.y);
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            const ImU32 dark = IM_COL32(4, 5, 6, 255);
            RadialBand(draw, viewport->GetCenter(), diagonal * 0.55f, diagonal * 1.02f, WithAlpha(dark, 0.0f),
                       WithAlpha(dark, 0.42f * amount));
        }

        // Red dot / holographic: the dot is projected at infinity, so it sits
        // where the optic points and is only seen through its glass.
        void DrawReflex(const FHudContext& context)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImVec2 center = viewport->GetCenter();
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            const float scale = viewport->Size.y / 1080.0f;
            const ImVec2 reticle(center.x + context.reflexOffsetPx.x, center.y + context.reflexOffsetPx.y);
            const ImU32 red = IM_COL32(255, 46, 32, 255);
            const bool holo = context.optic == EWeaponAttachment::Holographic;
            ImVec2 clipMin = viewport->Pos;
            ImVec2 clipMax(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
            float visible = 1.0f;
            if (context.reflexWindowValid)
            {
                const ImVec2 window(center.x + context.reflexWindowCenterPx.x, center.y + context.reflexWindowCenterPx.y);
                const ImVec2 half(context.reflexWindowHalfPx.x, context.reflexWindowHalfPx.y);
                // Coated glass: a faint tint and a soft reflection across the top.
                if (holo)
                {
                    draw->AddRectFilled(ImVec2(window.x - half.x, window.y - half.y), ImVec2(window.x + half.x, window.y + half.y),
                                        IM_COL32(120, 170, 210, 16));
                    draw->AddRectFilledMultiColor(ImVec2(window.x - half.x, window.y - half.y),
                                                  ImVec2(window.x + half.x, window.y - half.y * 0.2f),
                                                  IM_COL32(220, 235, 255, 26), IM_COL32(220, 235, 255, 8),
                                                  IM_COL32(220, 235, 255, 0), IM_COL32(220, 235, 255, 0));
                    clipMin = ImVec2(window.x - half.x, window.y - half.y);
                    clipMax = ImVec2(window.x + half.x, window.y + half.y);
                }
                else
                {
                    const float radius = std::min(half.x, half.y);
                    draw->AddCircleFilled(window, radius, IM_COL32(150, 200, 255, 14), 48);
                    draw->PathArcTo(window, radius * 0.82f, 3.6f, 4.6f, 24);
                    draw->PathStroke(IM_COL32(220, 235, 255, 40), 0, 1.5f * scale);
                    const float distance = std::hypot(reticle.x - window.x, reticle.y - window.y);
                    visible = 1.0f - glm::smoothstep(radius * 0.80f, radius * 0.95f, distance);
                }
            }
            if (visible <= 0.0f) return;
            draw->PushClipRect(clipMin, clipMax, true);
            if (holo)
            {
                // EOTech-style 65 MOA ring with quadrant ticks around a 1 MOA dot.
                const float ring = 15.0f * scale;
                const float width = 1.6f * scale;
                draw->AddCircle(reticle, ring, WithAlpha(red, 0.18f * visible), 64, width * 3.5f);
                draw->AddCircle(reticle, ring, WithAlpha(red, 0.95f * visible), 64, width);
                for (const ImVec2 axis : {ImVec2(0, -1), ImVec2(0, 1), ImVec2(-1, 0), ImVec2(1, 0)})
                {
                    GlowLine(draw, ImVec2(reticle.x + axis.x * ring, reticle.y + axis.y * ring),
                             ImVec2(reticle.x + axis.x * (ring + 5.0f * scale), reticle.y + axis.y * (ring + 5.0f * scale)),
                             red, width, visible);
                }
                GlowDot(draw, reticle, red, 1.5f * scale, visible);
            }
            else
            {
                GlowDot(draw, reticle, red, 2.8f * scale, visible);
            }
            draw->PopClipRect();
        }

        void DrawCrosshair(const FHudContext& context)
        {
            if (context.freeLooking) return;
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            if (context.firstPerson && context.aiming && context.sightReady && !context.showInventory && !context.showMap &&
                (context.optic == EWeaponAttachment::RedDot || context.optic == EWeaponAttachment::Holographic))
            {
                DrawReflex(context);
            }
            const float crosshair = context.melee ? 1.0f : std::clamp(context.crosshairAlpha, 0.0f, 1.0f);
            const ImU32 color = WithAlpha(IM_COL32(235, 235, 235, 220), crosshair);
            const ImU32 outline = WithAlpha(IM_COL32(20, 20, 20, 210), crosshair);
            const float gap = 6.0f * (context.melee ? 1.0f : context.crosshairGapScale);
            constexpr float len = 10.0f;
            const auto line = [&](const ImVec2& a, const ImVec2& b) {
                draw->AddLine(a, b, outline, 4.0f);
                draw->AddLine(a, b, color, 1.8f);
            };
            if (context.melee)
            {
                // A small guard ring replaces the firearm sight; the arc shows recovery.
                const float radius = context.aiming ? 10.0f : 5.0f;
                draw->AddCircle(center, radius, outline, 24, 3.5f);
                draw->AddCircle(center, radius, color, 24, 1.2f);
                if (context.swinging)
                {
                    draw->PathArcTo(center, 15.0f, -1.5708f, -1.5708f + 6.2832f * context.swingProgress, 32);
                    draw->PathStroke(UITheme::Colors::AccentAmber, 0, 2.0f);
                }
            }
            else if (crosshair > 0.01f)
            {
                line(ImVec2(center.x - gap - len, center.y), ImVec2(center.x - gap, center.y));
                line(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + len, center.y));
                line(ImVec2(center.x, center.y - gap - len), ImVec2(center.x, center.y - gap));
                line(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + len));
            }
            if (context.melee || crosshair > 0.01f)
            {
                draw->AddCircleFilled(center, 2.4f, outline);
                draw->AddCircleFilled(center, 1.2f, color);
            }

            // Hit marker: four ticks that pop out and settle.
            if (context.hitMarker > 0.0f)
            {
                const float t = std::clamp(context.hitMarker, 0.0f, 1.0f);
                const int alpha = static_cast<int>(255.0f * std::min(1.0f, t * 1.6f));
                const ImU32 tick = context.hitMarkerKill ? IM_COL32(235, 45, 40, alpha)
                    : context.hitMarkerHead              ? IM_COL32(255, 196, 60, alpha)
                                                         : IM_COL32(245, 245, 245, alpha);
                const ImU32 shadow = IM_COL32(15, 10, 10, alpha * 3 / 4);
                const float inner = 5.0f + 3.0f * t;
                const float outer = inner + (context.hitMarkerKill ? 9.0f : 6.0f) + 3.0f * t;
                const float width = context.hitMarkerKill || context.hitMarkerHead ? 2.6f : 2.0f;
                constexpr float diagonal = 0.70710678f;
                for (const ImVec2 axis : {ImVec2(diagonal, diagonal), ImVec2(-diagonal, diagonal),
                                          ImVec2(diagonal, -diagonal), ImVec2(-diagonal, -diagonal)})
                {
                    const ImVec2 a(center.x + axis.x * inner, center.y + axis.y * inner);
                    const ImVec2 b(center.x + axis.x * outer, center.y + axis.y * outer);
                    draw->AddLine(a, b, shadow, width + 2.0f);
                    draw->AddLine(a, b, tick, width);
                }
            }
        }

        void DrawDamageFeedback(const FHudContext& context)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            const float lowHealth = std::clamp((35.0f - context.survival.health) / 35.0f, 0.0f, 1.0f);
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 4.5f);
            const float intensity = std::clamp(std::max(context.hurt * 0.75f, lowHealth * (0.25f + 0.2f * pulse)),
                                               0.0f, 1.0f);
            if (intensity > 0.01f && context.survival.lifeState == EPlayerLifeState::Alive)
            {
                const ImVec2 min = viewport->Pos;
                const ImVec2 max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
                const float band = std::min(viewport->Size.x, viewport->Size.y) * 0.22f;
                const ImU32 edge = IM_COL32(150, 10, 8, static_cast<int>(210.0f * intensity));
                const ImU32 clear = IM_COL32(150, 10, 8, 0);
                draw->AddRectFilledMultiColor(min, ImVec2(max.x, min.y + band), edge, edge, clear, clear);
                draw->AddRectFilledMultiColor(ImVec2(min.x, max.y - band), max, clear, clear, edge, edge);
                draw->AddRectFilledMultiColor(min, ImVec2(min.x + band, max.y), edge, clear, clear, edge);
                draw->AddRectFilledMultiColor(ImVec2(max.x - band, min.y), max, clear, edge, edge, clear);
            }
            if (context.hurt > 0.01f)
            {
                const ImVec2 center = viewport->GetCenter();
                const float radius = std::min(viewport->Size.x, viewport->Size.y) * 0.16f;
                const float angle = std::atan2(-std::cos(context.hurtYawRadians), -std::sin(context.hurtYawRadians));
                const int alpha = static_cast<int>(235.0f * std::clamp(context.hurt, 0.0f, 1.0f));
                ImDrawList* front = ImGui::GetForegroundDrawList();
                front->PathArcTo(center, radius, angle - 0.38f, angle + 0.38f, 16);
                front->PathStroke(IM_COL32(20, 0, 0, alpha / 2), 0, 11.0f);
                front->PathArcTo(center, radius, angle - 0.36f, angle + 0.36f, 16);
                front->PathStroke(IM_COL32(225, 30, 22, alpha), 0, 7.0f);
            }
        }

        // ====================================================================
        // CLOCK, COMPASS & OBJECTIVE (Frameless Flat-Stylized Tactical HUD)
        // ====================================================================
        void DrawClock(const FHudContext& context)
        {
            using namespace UITheme;
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const bool clear = !context.overcast;
            const std::string weatherStr = context.weather.empty()
                ? std::string(clear ? NextDayz::L10n::Tr("Clear") : NextDayz::L10n::Tr("Overcast"))
                : context.weather;
            char timeBuf[16];
            std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", context.hour, context.minute);

            ImFont* numFont = NumeralFont();
            ImFont* boldFont = BoldFont();
            const float timeSz = S(30.0f);
            const float weatherSz = S(21.0f);
            const ImVec2 timeSize = numFont->CalcTextSizeA(timeSz, FLT_MAX, 0.0f, timeBuf);
            const ImVec2 weatherSize = boldFont->CalcTextSizeA(weatherSz, FLT_MAX, 0.0f, weatherStr.c_str());
            const float right = vp->Pos.x + vp->Size.x - S(28.0f);
            const float top = vp->Pos.y + S(18.0f);

            UIWidgets::DrawTextShadow(draw, boldFont, weatherSz,
                                      ImVec2(right - weatherSize.x, top + (timeSize.y - weatherSize.y) * 0.55f),
                                      clear ? Colors::TextPrimary : Colors::TextSecondary,
                                      weatherStr.c_str(), 1.8f);
            UIWidgets::DrawTextShadow(draw, numFont, timeSz,
                                      ImVec2(right - weatherSize.x - timeSize.x - S(12.0f), top),
                                      Colors::TextPrimary, timeBuf, 2.0f);

            if (context.pvp)
            {
                const char* pvpLabel = NextDayz::L10n::Tr("Friendly fire");
                const float pSz = S(15.0f);
                const ImVec2 pSize = boldFont->CalcTextSizeA(pSz, FLT_MAX, 0.0f, pvpLabel);
                const ImVec2 pMin(right - pSize.x - S(18.0f), top + timeSize.y + S(8.0f));
                const ImVec2 pMax(right, pMin.y + pSize.y + S(8.0f));
                draw->AddRectFilled(pMin, pMax, IM_COL32(190, 46, 46, 200), S(6.0f));
                draw->AddRect(pMin, pMax, Colors::AccentRose, S(6.0f), 0, 1.4f);
                draw->AddText(boldFont, pSz, ImVec2(pMin.x + S(9.0f), pMin.y + S(4.0f)), Colors::TextPrimary, pvpLabel);
            }
        }

        void DrawObjective(const FHudContext& context)
        {
            if (context.objective.empty()) return;
            using namespace UITheme;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const float top = context.hasCompass ? S(68.0f) : S(22.0f);
            const char* tag = NextDayz::L10n::Tr("GOAL");
            ImFont* boldFont = BoldFont();
            const float tagFontSz = S(16.5f);
            const float textFontSz = S(19.0f);
            const ImVec2 tagSize = boldFont->CalcTextSizeA(tagFontSz, FLT_MAX, 0.0f, tag);
            const ImVec2 textSize = boldFont->CalcTextSizeA(textFontSz, FLT_MAX, 0.0f, context.objective.c_str());
            const float tagW = tagSize.x + S(18.0f);
            const float tagH = tagSize.y + S(8.0f);
            const float contentW = tagW + S(12.0f) + textSize.x;
            const float stripH = std::max(tagH, textSize.y) + S(8.0f);
            const float cx = viewport->GetCenter().x;
            const float y0 = viewport->Pos.y + top;
            const float y1 = y0 + stripH;
            const float halfW = contentW * 0.5f + S(32.0f);

            const ImU32 dark = IM_COL32(14, 18, 16, 130);
            const ImU32 clear = IM_COL32(14, 18, 16, 0);
            draw->AddRectFilledMultiColor(ImVec2(cx - halfW - S(28.0f), y0), ImVec2(cx - halfW + S(20.0f), y1), clear, dark, dark, clear);
            draw->AddRectFilled(ImVec2(cx - halfW + S(20.0f), y0), ImVec2(cx + halfW - S(20.0f), y1), dark);
            draw->AddRectFilledMultiColor(ImVec2(cx + halfW - S(20.0f), y0), ImVec2(cx + halfW + S(28.0f), y1), dark, clear, clear, dark);

            const float startX = cx - contentW * 0.5f;
            const float midY = (y0 + y1) * 0.5f;
            const ImVec2 pillMin(startX, midY - tagH * 0.5f);
            const ImVec2 pillMax(startX + tagW, midY + tagH * 0.5f);
            draw->AddRectFilled(pillMin, pillMax, Colors::AccentAmber, S(5.0f));
            draw->AddText(boldFont, tagFontSz, ImVec2(pillMin.x + S(9.0f), midY - tagSize.y * 0.5f), Colors::TextOnAccent, tag);
            UIWidgets::DrawTextShadow(draw, boldFont, textFontSz,
                                      ImVec2(pillMax.x + S(12.0f), midY - textSize.y * 0.5f),
                                      Colors::TextPrimary, context.objective.c_str(), 1.8f);
        }

        void DrawCompass(const FHudContext& context)
        {
            if (!context.hasCompass) return;
            using namespace UITheme;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const float width = std::min(S(560.0f), viewport->Size.x * 0.42f);
            const float halfH = S(18.0f);
            const ImVec2 center(viewport->GetCenter().x, viewport->Pos.y + S(32.0f));
            const float pxPerDegree = width / 120.0f;

            // Frameless soft gradient tape (Mockup 02)
            const float left = center.x - width * 0.5f;
            const float right = center.x + width * 0.5f;
            const float fadeW = width * 0.24f;
            const ImU32 tapeDark = IM_COL32(16, 20, 18, 165);
            const ImU32 tapeClear = IM_COL32(16, 20, 18, 0);
            draw->AddRectFilledMultiColor(ImVec2(left - S(20.0f), center.y - halfH), ImVec2(left + fadeW, center.y + halfH),
                                          tapeClear, tapeDark, tapeDark, tapeClear);
            draw->AddRectFilled(ImVec2(left + fadeW, center.y - halfH), ImVec2(right - fadeW, center.y + halfH), tapeDark);
            draw->AddRectFilledMultiColor(ImVec2(right - fadeW, center.y - halfH), ImVec2(right + S(20.0f), center.y + halfH),
                                          tapeDark, tapeClear, tapeClear, tapeDark);

            ImFont* numFont = NumeralFont();
            ImFont* boldFont = BoldFont();
            const float heading = BearingDegrees(context.yawRadians);
            for (int tick = 0; tick < 360; tick += 15)
            {
                float delta = static_cast<float>(tick) - heading;
                delta = std::remainder(delta, 360.0f);
                if (std::abs(delta) > 60.0f) continue;

                const float x = center.x + delta * pxPerDegree;
                const float fade = std::pow(1.0f - std::abs(delta) / 60.0f, 0.72f);
                const ImU32 color = IM_COL32(242, 239, 230, static_cast<int>(235.0f * fade));
                const ImU32 subColor = IM_COL32(204, 208, 196, static_cast<int>(200.0f * fade));
                const char* cardinal = tick == 0 ? NextDayz::L10n::Tr("N") : tick == 90 ? NextDayz::L10n::Tr("E")
                    : tick == 180 ? NextDayz::L10n::Tr("S") : tick == 270 ? NextDayz::L10n::Tr("W") : nullptr;
                const char* inter = tick == 45 ? "NE" : tick == 135 ? "SE" : tick == 225 ? "SW" : tick == 315 ? "NW" : nullptr;
                if (cardinal)
                {
                    const float cSz = S(22.0f);
                    const ImVec2 lSize = boldFont->CalcTextSizeA(cSz, FLT_MAX, 0.0f, cardinal);
                    const ImU32 col = tick == 0 ? IM_COL32(224, 76, 76, static_cast<int>(255.0f * fade)) : color;
                    UIWidgets::DrawTextShadow(draw, boldFont, cSz, ImVec2(x - lSize.x * 0.5f, center.y - lSize.y * 0.5f), col, cardinal, 1.5f);
                }
                else if (inter)
                {
                    const float iSz = S(15.0f);
                    const ImVec2 lSize = numFont->CalcTextSizeA(iSz, FLT_MAX, 0.0f, inter);
                    UIWidgets::DrawTextShadow(draw, numFont, iSz, ImVec2(x - lSize.x * 0.5f, center.y - lSize.y * 0.5f), subColor, inter, 1.2f);
                }
                else
                {
                    const float h = S(6.5f);
                    draw->AddLine(ImVec2(x, center.y - h), ImVec2(x, center.y + h * 0.45f), subColor, 1.5f);
                }
            }
            for (const auto& [position, label] : context.objectives)
            {
                const glm::vec3 delta3 = position - context.playerPosition;
                const float bearing = glm::degrees(std::atan2(delta3.x, -delta3.z));
                const float delta = std::clamp(std::remainder(bearing - heading, 360.0f), -60.0f, 60.0f);
                const float x = center.x + delta * pxPerDegree;
                const ImVec2 d(x, center.y - halfH + S(2.0f));
                const float dr = S(5.5f);
                draw->AddQuadFilled(ImVec2(d.x, d.y - dr), ImVec2(d.x + dr, d.y),
                                    ImVec2(d.x, d.y + dr), ImVec2(d.x - dr, d.y), Colors::AccentAmber);
                char text[64];
                std::snprintf(text, sizeof(text), "%.0fm", glm::length(glm::vec2(delta3.x, delta3.z)));
                const float dSz = S(15.0f);
                const ImVec2 tSize = boldFont->CalcTextSizeA(dSz, FLT_MAX, 0.0f, text);
                UIWidgets::DrawTextShadow(draw, boldFont, dSz, ImVec2(x - tSize.x * 0.5f, center.y - halfH - tSize.y - S(2.0f)),
                                          Colors::TextHighlight, text, 1.4f);
            }
            if (context.cacheMarker)
            {
                const glm::vec3 delta3 = *context.cacheMarker - context.playerPosition;
                const float bearing = glm::degrees(std::atan2(delta3.x, -delta3.z));
                const float delta = std::clamp(std::remainder(bearing - heading, 360.0f), -60.0f, 60.0f);
                const float x = center.x + delta * pxPerDegree;
                const ImVec2 d(x, center.y + halfH);
                const float dr = S(5.0f);
                draw->AddQuadFilled(ImVec2(d.x, d.y - dr), ImVec2(d.x + dr, d.y),
                                    ImVec2(d.x, d.y + dr), ImVec2(d.x - dr, d.y), Colors::AccentRose);
                char text[64];
                std::snprintf(text, sizeof(text), "%.0fm", glm::length(glm::vec2(delta3.x, delta3.z)));
                const float dSz = S(14.5f);
                const ImVec2 tSize = boldFont->CalcTextSizeA(dSz, FLT_MAX, 0.0f, text);
                UIWidgets::DrawTextShadow(draw, boldFont, dSz, ImVec2(x - tSize.x * 0.5f, center.y + halfH + S(4.0f)),
                                          Colors::AccentRose, text, 1.3f);
            }
            // Center amber pointer triangle
            draw->AddTriangleFilled(ImVec2(center.x - S(5.5f), center.y - halfH - S(3.0f)),
                                    ImVec2(center.x + S(5.5f), center.y - halfH - S(3.0f)),
                                    ImVec2(center.x, center.y - halfH + S(5.0f)), Colors::AccentAmber);
        }

        // ====================================================================
        // OBJECT INTERACTION PROMPT & ACTION RADIAL PROGRESS
        // ====================================================================
        void DrawInteractionPrompt(const FHudContext& context)
        {
            using namespace UITheme;
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            ImFont* boldFont = BoldFont();

            // 1. Ongoing Action Radial Progress
            if (context.actionActive)
            {
                const ImVec2 center(vp->GetCenter().x, vp->GetCenter().y + S(54.0f));
                const float radius = S(44.0f);
                const float thickness = S(5.0f);

                UIWidgets::DrawRadialProgress(draw, center, radius, thickness, context.actionProgress,
                                              Colors::AccentAmber, IM_COL32(20, 26, 22, 210));

                const std::string& action = context.actionLabel.empty() ? NextDayz::L10n::Tr("INTERACTING") : context.actionLabel;
                const float actSz = S(16.0f);
                const ImVec2 textSize = boldFont->CalcTextSizeA(actSz, FLT_MAX, 0.0f, action.c_str());
                draw->AddText(boldFont, actSz, ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                              Colors::TextPrimary, action.c_str());

                char pctText[16];
                std::snprintf(pctText, sizeof(pctText), "%d%%", static_cast<int>(context.actionProgress * 100.0f));
                const float pctSz = S(18.0f);
                const ImVec2 pctSize = boldFont->CalcTextSizeA(pctSz, FLT_MAX, 0.0f, pctText);
                UIWidgets::DrawTextShadow(draw, boldFont, pctSz, ImVec2(center.x - pctSize.x * 0.5f, center.y + radius + S(8.0f)),
                                          Colors::TextHighlight, pctText, 1.5f);
                return;
            }

            if (context.interactionPrompt.empty()) return;

            const ImVec2 center(vp->GetCenter().x, vp->Pos.y + vp->Size.y * 0.72f);
            const std::string& prompt = context.interactionPrompt;
            if (prompt.find("[E]") != std::string::npos && prompt.find("[F]") != std::string::npos)
            {
                const float pillH = S(44.0f);
                const float gap = S(16.0f);
                const float w1 = S(172.0f);
                const float w2 = S(204.0f);
                const float totalW = w1 + w2 + gap;
                const float startX = center.x - totalW * 0.5f;
                const float r = S(8.0f);

                const ImVec2 p1Min(startX, center.y - pillH * 0.5f);
                const ImVec2 p1Max(p1Min.x + w1, p1Min.y + pillH);
                draw->AddRectFilled(p1Min, p1Max, Colors::SurfaceBg, r);
                draw->AddRect(p1Min, p1Max, Colors::BorderTactical, r, 0, 1.4f);
                UIWidgets::DrawKeyBadge(draw, ImVec2(p1Min.x + S(9.0f), p1Min.y + S(7.0f)), "E", S(30.0f));
                draw->AddText(boldFont, S(17.5f), ImVec2(p1Min.x + S(48.0f), p1Min.y + S(12.0f)), Colors::TextPrimary, NextDayz::L10n::Tr("DRINK"));

                const ImVec2 p2Min(p1Max.x + gap, center.y - pillH * 0.5f);
                const ImVec2 p2Max(p2Min.x + w2, p2Min.y + pillH);
                draw->AddRectFilled(p2Min, p2Max, Colors::SurfaceBg, r);
                draw->AddRect(p2Min, p2Max, Colors::BorderTactical, r, 0, 1.4f);
                UIWidgets::DrawKeyBadge(draw, ImVec2(p2Min.x + S(9.0f), p2Min.y + S(7.0f)), "F", S(30.0f));
                draw->AddText(boldFont, S(17.5f), ImVec2(p2Min.x + S(48.0f), p2Min.y + S(12.0f)), Colors::AccentCyan, NextDayz::L10n::Tr("FILL BOTTLE"));
                return;
            }

            // Stacked 2-line flat-stylized interaction pill (Mockup 02: [E] | 拾取 / AK-74 突击步枪)
            std::string key = "E";
            std::string action = NextDayz::L10n::Tr("INTERACT");
            std::string target = prompt;

            if (prompt.rfind("[E] Take ", 0) == 0)
            {
                key = "E";
                action = NextDayz::L10n::Tr("TAKE");
                target = prompt.substr(9);
            }
            else if (prompt.rfind("[E] ", 0) == 0)
            {
                key = "E";
                action = NextDayz::L10n::Tr("USE");
                target = prompt.substr(4);
            }

            target = NextDayz::L10n::Localize(target, NextDayz::L10n::Language());
            const float actSz = S(14.5f);
            const float tgtSz = S(19.5f);
            const ImVec2 actionSize = boldFont->CalcTextSizeA(actSz, FLT_MAX, 0.0f, action.c_str());
            const ImVec2 targetSize = boldFont->CalcTextSizeA(tgtSz, FLT_MAX, 0.0f, target.c_str());
            const float keyH = S(36.0f);
            const float pillH = S(58.0f);
            const float pillW = S(14.0f) + keyH + S(14.0f) + std::max(actionSize.x, targetSize.x) + S(22.0f);
            const float r = S(8.0f);

            const ImVec2 min(center.x - pillW * 0.5f, center.y - pillH * 0.5f);
            const ImVec2 max(min.x + pillW, min.y + pillH);

            draw->AddRectFilled(ImVec2(min.x, min.y + 3.0f), ImVec2(max.x, max.y + 3.0f), IM_COL32(4, 6, 5, 145), r);
            draw->AddRectFilled(min, max, IM_COL32(22, 26, 23, 238), r);
            draw->AddRect(min, max, Colors::BorderTactical, r, 0, 1.5f);

            const float kw = UIWidgets::DrawKeyBadge(draw, ImVec2(min.x + S(12.0f), min.y + (pillH - keyH) * 0.5f), key.c_str(), keyH);
            const float textX = min.x + S(12.0f) + kw + S(14.0f);
            draw->AddText(boldFont, actSz, ImVec2(textX, min.y + S(8.0f)), Colors::TextSecondary, action.c_str());
            draw->AddText(boldFont, tgtSz, ImVec2(textX, min.y + S(27.0f)), Colors::TextPrimary, target.c_str());
        }

        // ====================================================================
        // SURVIVAL VITALS (Chunky Flat-Stylized Bars + Plump Icons, Mockup 02)
        // ====================================================================
        void DrawSurvival(const FHudContext& context)
        {
            using namespace UITheme;
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            ImFont* boldFont = BoldFont();

            const float baseX = vp->Pos.x + S(28.0f);
            const float iconColW = S(28.0f);
            const float barW = S(202.0f);
            const float barH = S(17.0f);
            const float rowStep = S(28.0f);
            const float topY = vp->Pos.y + vp->Size.y - rowStep * 3.0f - S(22.0f);

            const auto renderRow = [&](UIWidgets::EVitalIcon icon, float value, float y, ImU32 color, ImU32 numCol, bool pulse)
            {
                const float midY = y + barH * 0.5f;
                UIWidgets::DrawVitalIcon(draw, ImVec2(baseX + S(10.0f), midY), S(19.0f), icon, color);

                const ImVec2 bMin(baseX + iconColW, y);
                const ImVec2 bMax(bMin.x + barW, y + barH);
                UIWidgets::DrawTacticalBar(draw, bMin, bMax, value / 100.0f, color, IM_COL32(14, 18, 16, 205), pulse, S(4.5f));

                char pct[16];
                std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(std::ceil(std::clamp(value, 0.0f, 100.0f))));
                const float numSz = S(18.0f);
                const ImVec2 pSz = boldFont->CalcTextSizeA(numSz, FLT_MAX, 0.0f, pct);
                UIWidgets::DrawTextShadow(draw, boldFont, numSz, ImVec2(bMax.x + S(10.0f), midY - pSz.y * 0.5f),
                                          pulse ? Colors::AccentRose : numCol, pct, 1.5f);
            };

            const FSurvivalSnapshot& s = context.survival;
            const bool lowHealth = s.health < 28.0f;
            const ImU32 hpCol = lowHealth ? Colors::AccentRose : Colors::TextPrimary;
            renderRow(UIWidgets::EVitalIcon::Health, s.health, topY, hpCol, Colors::TextPrimary, lowHealth);
            renderRow(UIWidgets::EVitalIcon::Hunger, s.hunger, topY + rowStep, Colors::AccentAmber, Colors::TextHighlight, s.hunger < 22.0f);
            renderRow(UIWidgets::EVitalIcon::Hydration, s.hydration, topY + rowStep * 2.0f, Colors::AccentCyan, Colors::AccentIceBlue, s.hydration < 22.0f);

            // Condition pills only appear when triggered (flat rounded crimson/ice/olive pills)
            float pillY = topY - S(38.0f);
            if (s.bleedingWounds > 0)
            {
                char text[128];
                std::snprintf(text, sizeof(text), NextDayz::L10n::Tr("BLEEDING x%d  [H]"), s.bleedingWounds);
                const ImVec2 sz = UIWidgets::DrawStatusPill(draw, ImVec2(baseX, pillY), UIWidgets::EVitalIcon::Bleeding, text,
                                                            IM_COL32(182, 46, 46, 238), Colors::AccentRose,
                                                            Colors::TextPrimary, true);
                pillY -= sz.y + S(8.0f);
            }
            if (s.cold > 0.25f)
            {
                const char* coldText = s.cold > 0.55f ? NextDayz::L10n::Tr("FREEZING") : NextDayz::L10n::Tr("COLD");
                const ImVec2 sz = UIWidgets::DrawStatusPill(draw, ImVec2(baseX, pillY), UIWidgets::EVitalIcon::Cold, coldText,
                                                            IM_COL32(34, 68, 90, 232), Colors::AccentCyan,
                                                            Colors::AccentIceBlue, s.cold > 0.55f);
                pillY -= sz.y + S(8.0f);
            }
            if (s.healing && s.bleedingWounds == 0)
            {
                const char* healText = s.painkillerSeconds > 0.0f ? NextDayz::L10n::Tr("PAINKILLERS") : NextDayz::L10n::Tr("HEALING");
                const ImVec2 sz = UIWidgets::DrawStatusPill(draw, ImVec2(baseX, pillY), UIWidgets::EVitalIcon::Healing, healText,
                                                            IM_COL32(34, 72, 44, 232), Colors::AccentEmerald,
                                                            Colors::TextPrimary);
                pillY -= sz.y + S(8.0f);
            }
            if (context.weak)
            {
                const ImVec2 sz = UIWidgets::DrawStatusPill(draw, ImVec2(baseX, pillY), UIWidgets::EVitalIcon::Weak,
                                                            NextDayz::L10n::Tr("WEAK - NO SPRINT"),
                                                            IM_COL32(94, 64, 22, 232), Colors::AccentAmber,
                                                            Colors::TextHighlight);
                pillY -= sz.y + S(8.0f);
            }
            if (context.lanternLit)
            {
                char lanternText[128];
                std::snprintf(lanternText, sizeof(lanternText), NextDayz::L10n::Tr("LANTERN %.0fm"), context.lanternMinutes);
                UIWidgets::DrawStatusPill(draw, ImVec2(baseX, pillY), UIWidgets::EVitalIcon::Lantern, lanternText,
                                          IM_COL32(70, 54, 22, 226), Colors::AccentAmber,
                                          Colors::TextPrimary);
            }
        }

        // ====================================================================
        // WEAPON READOUT & BOTTOM QUICKBAR (Bold Condensed Typography, Mockup 02)
        // ====================================================================
        void DrawWeaponReadout(const FHudContext& context)
        {
            using namespace UITheme;
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            ImFont* boldFont = BoldFont();
            ImFont* numFont = NumeralFont();
            const float right = vp->Pos.x + vp->Size.x - S(30.0f);
            const float bottom = vp->Pos.y + vp->Size.y - S(24.0f);

            // 3 chunky rounded weapon slot indicator bars along bottom-right
            const float dashH = S(5.5f);
            const float dashY = bottom - dashH;
            const float dashGap = S(7.0f);
            const float dashW[3] = {S(42.0f), S(32.0f), S(24.0f)};
            float dx = right;
            for (int s = 2; s >= 0; --s)
            {
                dx -= dashW[s];
                const bool hasSlot = context.weapons && context.weapons->SlotInstance(s) != 0;
                const bool active = context.hasWeapon && context.activeSlot == s;
                const ImU32 col = active  ? Colors::AccentAmber
                                : hasSlot ? IM_COL32(242, 239, 230, 120)
                                          : IM_COL32(242, 239, 230, 44);
                draw->AddRectFilled(ImVec2(dx, dashY), ImVec2(dx + dashW[s], dashY + dashH), col, S(2.5f));
                dx -= dashGap;
            }

            if (!context.hasWeapon)
            {
                const char* unarmed = NextDayz::L10n::Tr("UNARMED");
                const float uSz = S(18.0f);
                const ImVec2 uSize = boldFont->CalcTextSizeA(uSz, FLT_MAX, 0.0f, unarmed);
                UIWidgets::DrawTextShadow(draw, boldFont, uSz, ImVec2(right - uSize.x, dashY - uSize.y - S(10.0f)),
                                          Colors::TextSecondary, unarmed, 1.5f);
                return;
            }

            const float readoutBottom = dashY - S(12.0f);
            if (context.reloading)
            {
                const char* rel = NextDayz::L10n::Tr("RELOADING...");
                const float rFontSz = S(18.0f);
                const ImVec2 rSize = boldFont->CalcTextSizeA(rFontSz, FLT_MAX, 0.0f, rel);
                const float barW = S(156.0f);
                const float t = static_cast<float>(ImGui::GetTime());
                const float wave = 0.5f + 0.5f * std::sin(t * 7.5f);
                UIWidgets::DrawTacticalBar(draw, ImVec2(right - barW, readoutBottom - S(8.0f)),
                                           ImVec2(right, readoutBottom), wave, Colors::AccentAmber);
                UIWidgets::DrawTextShadow(draw, boldFont, rFontSz, ImVec2(right - rSize.x, readoutBottom - rSize.y - S(12.0f)),
                                          Colors::AccentAmber, rel, 1.5f);
                const float wFontSz = S(17.5f);
                const ImVec2 wSize = boldFont->CalcTextSizeA(wFontSz, FLT_MAX, 0.0f, context.weaponName.c_str());
                UIWidgets::DrawTextShadow(draw, boldFont, wFontSz, ImVec2(right - wSize.x, readoutBottom - rSize.y - wSize.y - S(18.0f)),
                                          Colors::TextSecondary, context.weaponName.c_str(), 1.5f);
                return;
            }
            if (context.melee)
            {
                const char* state = context.swinging ? NextDayz::L10n::Tr("STRIKING...")
                    : context.aiming ? NextDayz::L10n::Tr("GUARD READY") : NextDayz::L10n::Tr("LMB  STRIKE");
                const float sFontSz = S(17.0f);
                const ImVec2 sSize = boldFont->CalcTextSizeA(sFontSz, FLT_MAX, 0.0f, state);
                UIWidgets::DrawTextShadow(draw, boldFont, sFontSz, ImVec2(right - sSize.x, readoutBottom - sSize.y),
                                          Colors::TextSecondary, state, 1.5f);
                const float wFontSz = S(22.0f);
                const ImVec2 wSize = boldFont->CalcTextSizeA(wFontSz, FLT_MAX, 0.0f, context.weaponName.c_str());
                UIWidgets::DrawTextShadow(draw, boldFont, wFontSz, ImVec2(right - wSize.x, readoutBottom - sSize.y - wSize.y - S(6.0f)),
                                          Colors::TextPrimary, context.weaponName.c_str(), 1.6f);
                return;
            }

            // Huge bold condensed magazine numeral + crisp reserve (Mockup 02)
            char magText[16];
            std::snprintf(magText, sizeof(magText), "%d", context.ammoInMag);
            char reserveText[16];
            std::snprintf(reserveText, sizeof(reserveText), "/ %d", context.ammoReserve);

            const float bigSize = S(58.0f);
            const float resFontSize = S(28.0f);
            const ImVec2 magSize = numFont->CalcTextSizeA(bigSize, FLT_MAX, 0.0f, magText);
            const ImVec2 resSize = numFont->CalcTextSizeA(resFontSize, FLT_MAX, 0.0f, reserveText);

            const ImU32 magColor = context.ammoInMag == 0 ? Colors::AccentRose : Colors::TextPrimary;
            UIWidgets::DrawTextShadow(draw, numFont, resFontSize,
                                      ImVec2(right - resSize.x, readoutBottom - resSize.y - S(5.0f)),
                                      Colors::TextSecondary, reserveText, 1.8f);
            UIWidgets::DrawTextShadow(draw, numFont, bigSize,
                                      ImVec2(right - resSize.x - magSize.x - S(8.0f), readoutBottom - magSize.y + S(4.0f)),
                                      magColor, magText, 2.4f);

            const float wFontSz = S(18.5f);
            const ImVec2 wSize = boldFont->CalcTextSizeA(wFontSz, FLT_MAX, 0.0f, context.weaponName.c_str());
            UIWidgets::DrawTextShadow(draw, boldFont, wFontSz, ImVec2(right - wSize.x, readoutBottom - magSize.y - wSize.y - S(4.0f)),
                                      Colors::TextPrimary, context.weaponName.c_str(), 1.6f);
        }

        void DrawQuickbar(const FHudContext& context)
        {
            if (context.inventory == nullptr) return;
            using namespace UITheme;
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            ImFont* boldFont = BoldFont();

            const float size = S(66.0f);
            const float gap = S(10.0f);
            const float r = S(8.0f);
            const float total = 4.0f * size + 3.0f * gap;
            const ImVec2 origin(vp->GetCenter().x - total * 0.5f, vp->Pos.y + vp->Size.y - size - S(22.0f));

            for (int slot = 0; slot < 4; ++slot)
            {
                const ImVec2 a(origin.x + slot * (size + gap), origin.y);
                const ImVec2 b(a.x + size, a.y + size);
                const std::string& itemId = context.quickSlots[slot];
                const int count = itemId.empty() ? 0 : context.inventory->CountOf(itemId);
                const bool occupied = !itemId.empty();

                draw->AddRectFilled(ImVec2(a.x, a.y + 2.5f), ImVec2(b.x, b.y + 2.5f), IM_COL32(4, 6, 5, 130), r);
                draw->AddRectFilled(a, b, occupied ? IM_COL32(24, 29, 26, 218) : IM_COL32(18, 22, 20, 150), r);
                draw->AddRect(a, b, occupied ? Colors::BorderTactical : Colors::BorderMuted, r, 0, 1.4f);

                char key[4];
                std::snprintf(key, sizeof(key), "%d", slot + 4);
                draw->AddText(boldFont, S(14.0f), ImVec2(a.x + S(7.0f), a.y + S(4.0f)), Colors::TextSecondary, key);

                if (occupied)
                {
                    if (!DrawItemIcon(context, itemId, ImVec2(a.x + S(8.0f), a.y + S(12.0f)), ImVec2(b.x - S(8.0f), b.y - S(12.0f)), false, draw))
                    {
                        const std::string name = FitText(ItemName(itemId), size - S(10.0f));
                        draw->AddText(boldFont, S(13.5f), ImVec2(a.x + S(6.0f), a.y + S(24.0f)),
                                      count > 0 ? Colors::TextPrimary : Colors::TextMuted, name.c_str());
                    }
                    char amount[16];
                    std::snprintf(amount, sizeof(amount), "x%d", count);
                    const float amtSz = S(15.5f);
                    const ImVec2 amtSize = boldFont->CalcTextSizeA(amtSz, FLT_MAX, 0.0f, amount);
                    UIWidgets::DrawTextShadow(draw, boldFont, amtSz, ImVec2(b.x - amtSize.x - S(6.0f), b.y - amtSize.y - S(4.0f)),
                                              count > 0 ? Colors::TextPrimary : Colors::TextMuted, amount, 1.3f);
                }
            }
        }

        // ====================================================================
        // NOTIFICATIONS & MAP
        // ====================================================================
        void DrawNotifications(const FHudContext& context)
        {
            if (context.notifications.empty())
            {
                return;
            }
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            float y = vp->Pos.y + vp->Size.y - 100.0f;
            const float right = vp->Pos.x + vp->Size.x - 20.0f;

            for (auto it = context.notifications.rbegin(); it != context.notifications.rend(); ++it)
            {
                const float alpha = std::clamp(1.0f - (it->age - 5.0f) / 1.5f, 0.0f, 1.0f);
                if (alpha <= 0.0f)
                {
                    continue;
                }
                const ImVec2 size = ImGui::CalcTextSize(it->text.c_str());
                const ImVec2 a(right - size.x - 18.0f, y - size.y - 8.0f);
                const ImVec2 b(right, y);

                const int aVal = static_cast<int>(240.0f * alpha);
                ImU32 textCol = UITheme::Colors::TextPrimary;
                ImU32 borderCol = UITheme::Colors::BorderMuted;
                if (it->severity == 1) { textCol = UITheme::Colors::AccentAmber; borderCol = UITheme::Colors::AccentAmber; }
                else if (it->severity == 2) { textCol = UITheme::Colors::AccentRose; borderCol = UITheme::Colors::AccentRose; }
                else if (it->severity == 3) { textCol = UITheme::Colors::AccentEmerald; borderCol = UITheme::Colors::AccentEmerald; }

                textCol = (textCol & 0x00FFFFFF) | (static_cast<ImU32>(aVal) << 24);
                draw->AddRectFilled(a, b, UITheme::Colors::SurfaceBg, 3.0f);
                draw->AddRect(a, b, borderCol, 3.0f, 0, 1.0f);
                draw->AddText(ImVec2(a.x + 9.0f, a.y + 4.0f), textCol, it->text.c_str());
                y -= size.y + 12.0f;
            }
        }

        void DrawMap(const FHudContext& context)
        {
            if (!context.showMap)
            {
                return;
            }
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                          UITheme::Colors::BgScrim);
            const float side = std::min(vp->Size.x, vp->Size.y) * 0.80f;
            ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(side + 24.0f, side + 82.0f), ImGuiCond_Always);
            UITheme::FFieldStyleScope style;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));

            ImGui::Begin("##nd_map", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            UIWidgets::DrawSectionHeader(NextDayz::L10n::Tr("Riverland Tactical Map"), "[M]", UITheme::Colors::AccentAmber);
            if (!context.hasMap)
            {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", NextDayz::L10n::Tr("You have no map. Look for one in shops, police stations and military sites."));
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float mapSide = side - 4.0f;
            const ImVec2 end(origin.x + mapSide, origin.y + mapSide);
            draw->AddRectFilled(origin, end, IM_COL32(196, 188, 158, 255), 2.0f);

            const auto toScreen = [&](float x, float z)
            {
                return ImVec2(origin.x + (x + 500.0f) / 1000.0f * mapSide, origin.y + (z + 500.0f) / 1000.0f * mapSide);
            };
            for (int line = -400; line <= 400; line += 100)
            {
                draw->AddLine(toScreen(static_cast<float>(line), -500.0f), toScreen(static_cast<float>(line), 500.0f),
                              IM_COL32(150, 142, 116, 120));
                draw->AddLine(toScreen(-500.0f, static_cast<float>(line)), toScreen(500.0f, static_cast<float>(line)),
                              IM_COL32(150, 142, 116, 120));
            }
            const std::array<glm::vec2, 7> river = {{{-60, 470}, {-30, 300}, {-110, 160}, {-60, 20}, {20, -160}, {-20, -330}, {30, -500}}};
            for (size_t i = 0; i + 1 < river.size(); ++i)
            {
                draw->AddLine(toScreen(river[i].x, -river[i].y), toScreen(river[i + 1].x, -river[i + 1].y),
                              IM_COL32(70, 110, 160, 255), 4.0f);
            }
            const std::array<glm::vec2, 12> road = {{{-500, -140}, {-390, -110}, {-300, -150}, {-210, -110}, {-120, -150}, {-30, -158},
                                                     {60, -150}, {150, -110}, {240, -140}, {330, -100}, {420, -130}, {500, -115}}};
            for (size_t i = 0; i + 1 < road.size(); ++i)
            {
                draw->AddLine(toScreen(road[i].x, -road[i].y), toScreen(road[i + 1].x, -road[i + 1].y),
                              IM_COL32(90, 80, 70, 255), 3.0f);
            }
            draw->AddCircleFilled(toScreen(-330.0f, 330.0f), 55.0f / 1000.0f * mapSide, IM_COL32(70, 110, 160, 255));
            for (const FHudPoi& poi : context.pois)
            {
                const ImVec2 p = toScreen(poi.xz.x, poi.xz.y);
                const bool danger = poi.kind == "military" || poi.kind == "fortress";
                draw->AddRectFilled(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4),
                                    danger ? IM_COL32(150, 40, 30, 255) : IM_COL32(50, 46, 40, 255));
                draw->AddText(ImVec2(p.x + 7, p.y - 7), IM_COL32(30, 28, 24, 255), NextDayz::L10n::Tr(poi.name));
            }
            for (const auto& [position, label] : context.objectives)
            {
                const ImVec2 p = toScreen(position.x, position.z);
                draw->AddCircle(p, 12.0f, IM_COL32(200, 130, 20, 255), 20, 3.0f);
                draw->AddText(ImVec2(p.x + 14, p.y + 2), IM_COL32(150, 90, 10, 255), label.c_str());
            }
            if (context.cacheMarker)
            {
                const ImVec2 p = toScreen(context.cacheMarker->x, context.cacheMarker->z);
                draw->AddCircle(p, 10.0f, IM_COL32(220, 40, 30, 255), 16, 3.0f);
                draw->AddText(ImVec2(p.x + 12, p.y + 2), IM_COL32(200, 30, 20, 255), NextDayz::L10n::Tr("Supply cache"));
            }
            const ImVec2 me = toScreen(context.playerPosition.x, context.playerPosition.z);
            const glm::vec2 forward(std::sin(context.yawRadians), std::cos(context.yawRadians));
            const glm::vec2 right(forward.y, -forward.x);
            const auto offset = [&](float f, float r)
            {
                return ImVec2(me.x + (forward.x * f + right.x * r), me.y + (forward.y * f + right.y * r));
            };
            draw->AddTriangleFilled(offset(12.0f, 0.0f), offset(-7.0f, 6.0f), offset(-7.0f, -6.0f), IM_COL32(20, 90, 220, 255));
            ImGui::Dummy(ImVec2(mapSide, mapSide));
            ImGui::TextDisabled("%s", NextDayz::L10n::Tr("M close  |  grid 100 m  |  north up"));
            ImGui::End();
            ImGui::PopStyleVar();
        }

        // ====================================================================
        // MODERN GRID-BASED INVENTORY & EXPLICIT DRAG-DROP
        // ====================================================================

        void ItemTooltip(const FHudContext& context, const FItemDef& definition, const FItemInstance& instance)
        {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, UITheme::Colors::SurfaceBg);
            ImGui::PushStyleColor(ImGuiCol_Border, UITheme::Colors::BorderMuted);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
            ImGui::BeginTooltip();

            ItemIconPreview(context, definition, false, 160.0f);

            // Category badge & Title
            ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), "[%s]", UITheme::ItemKindBadge(definition.kind));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", std::string(NextDayz::L10n::Tr(definition.displayName)).c_str());

            const auto size = Inventory::InstanceSize(instance);
            ImGui::Text(L10n::Tr("Footprint: %d x %d cells"), size.width, size.height);
            if (definition.containerSize.IsValid())
                ImGui::Text(L10n::Tr("Container: %d x %d cells"), definition.containerSize.width, definition.containerSize.height);
            if (definition.maxStack > 1)
                ImGui::Text(L10n::Tr("Stack: %d / %d"), instance.count, definition.maxStack);

            if (!definition.description.empty())
            {
                ImGui::Separator();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20.0f);
                ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "%s", std::string(NextDayz::L10n::Tr(definition.description)).c_str());
                ImGui::PopTextWrapPos();
            }
            if (!definition.lore.empty())
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.72f, 0.66f, 0.50f, 1.0f), "%s", NextDayz::L10n::Tr("FIELD NOTES"));
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20.0f);
                ImGui::TextColored(ImVec4(0.66f, 0.64f, 0.58f, 1.0f), "%s", std::string(NextDayz::L10n::Tr(definition.lore)).c_str());
                ImGui::PopTextWrapPos();
            }
            if (definition.kind == EItemKind::Weapon)
            {
                ImGui::TextColored(ImVec4(0.9f, 0.95f, 1.0f, 1.0f), NextDayz::L10n::Tr("Loaded Ammo: %d"), instance.loadedAmmo);
                ImGui::Text(NextDayz::L10n::Tr("Magazine capacity: %d"), WeaponMagazineCapacity(definition.weaponDefId, instance.attachments));
                for (size_t slot = 0; slot < instance.attachments.size(); ++slot)
                    if (const auto* part = FindAttachmentDef(instance.attachments[slot]))
                        ImGui::BulletText("%s: %s", L10n::Tr(AttachmentSlotName(static_cast<EAttachmentSlot>(slot))),
                                          std::string(L10n::Tr(part->displayName)).c_str());
            }
            if (definition.armor > 0.0f)
            {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), NextDayz::L10n::Tr("Armor Rating: +%.0f%%"), definition.armor * 100.0f);
            }
            if (definition.warmth > 0.0f)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), NextDayz::L10n::Tr("Insulation: +%.0f%%"), definition.warmth * 100.0f);
            }
            if (definition.hungerDelta > 0.0f)
            {
                ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), NextDayz::L10n::Tr("Food Value: +%.0f"), definition.hungerDelta);
            }
            if (definition.hydrationDelta > 0.0f)
            {
                ImGui::TextColored(ImVec4(0.2f, 0.7f, 1.0f, 1.0f), NextDayz::L10n::Tr("Hydration: +%.0f"), definition.hydrationDelta);
            }
            ImGui::EndTooltip();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }

        void DrawAttachmentOptions(const FHudContext& context, const FItemInstance& gun, EAttachmentSlot slot)
        {
            const auto installed = AttachmentAt(gun.attachments, slot);
            if (installed != EWeaponAttachment::None && context.removeAttachment && TargetMenuItem("Remove attachment"))
                context.removeAttachment(gun.instanceId, slot);
            bool any = false;
            for (const auto& item : context.inventory->Instances())
            {
                const auto* part = FindAttachmentDef(item.defId);
                if (!part || part->slot != slot || !SupportsAttachment(gun.defId, part->type)) continue;
                any = true;
                ImGui::PushID(static_cast<int>(item.instanceId));
                if (ImGui::MenuItem(std::string(L10n::Tr(part->displayName)).c_str()) && context.installAttachment)
                    context.installAttachment(gun.instanceId, item.instanceId);
                ImGui::PopID();
            }
            if (!any) ImGui::TextDisabled("%s", L10n::Tr("No compatible attachments in backpack"));
        }

        void DrawAttachmentMenu(const FHudContext& context, const FItemInstance& gun)
        {
            for (size_t slot = 0; slot < kAttachmentSlotCount; ++slot)
            {
                const auto kind = static_cast<EAttachmentSlot>(slot);
                if (ImGui::BeginMenu(L10n::Label(AttachmentSlotName(kind))))
                {
                    DrawAttachmentOptions(context, gun, kind);
                    ImGui::EndMenu();
                }
            }
        }

        // Renders an item card slot in a grid. Returns true if clicked.
        bool DrawGridItemSlot(const FHudContext& context, const FItemInstance& instance,
                              const ImVec2& slotSize, bool inEquipmentSlot = false, float cellSize = 0.0f)
        {
            const FItemDef* def = FindItemDef(instance.defId);
            if (!def)
            {
                return false;
            }

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 max(pos.x + slotSize.x, pos.y + slotSize.y);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            ImGui::PushID(static_cast<int>(instance.instanceId));
            const bool clicked = ImGui::InvisibleButton("##slot_btn", slotSize);
            const bool hovered = ImGui::IsItemHovered();

            // Slot Background & Frame
            UIWidgets::DrawSlotFrame(draw, pos, max, true, hovered, false, false, 3.0f);
            UIWidgets::DrawCornerTicks(draw, pos, max, 4.0f, hovered ? UITheme::Colors::AccentAmber : UITheme::Colors::BorderMuted);

            // Left Category Color Accent Stripe
            const ImU32 kindColor = UITheme::ItemKindColor(def->kind);
            draw->AddRectFilled(pos, ImVec2(pos.x + 3.0f, max.y), kindColor, 4.0f);

            // Pending icons retain a readable label; completed icons use the whole rectangle.
            if (!DrawItemIcon(context, def->id, ImVec2(pos.x + 5.0f, pos.y + 3.0f),
                              ImVec2(max.x - 3.0f, max.y - 3.0f), instance.placement.rotated))
            {
                const auto name = FitText(std::string(L10n::Tr(def->displayName)), slotSize.x - 14.0f);
                draw->AddText(ImVec2(pos.x + 7.0f, pos.y + 6.0f), UITheme::Colors::TextPrimary, name.c_str());
            }
            const auto footprint = Inventory::InstanceSize(instance);
            const auto dimensions = fmt::format("{}x{}", footprint.width, footprint.height);
            const float indicatorWidth = def->maxStack > 1 || def->kind == EItemKind::Weapon ? 36.0f : 0.0f;
            if (slotSize.x > ImGui::CalcTextSize(dimensions.c_str()).x + indicatorWidth + 14.0f)
            {
                draw->AddRectFilled(ImVec2(pos.x + 5.0f, max.y - ImGui::GetTextLineHeight() - 5.0f),
                    ImVec2(pos.x + 9.0f + ImGui::CalcTextSize(dimensions.c_str()).x, max.y - 2.0f), IM_COL32(10, 12, 16, 200), 2.0f);
                draw->AddText(ImVec2(pos.x + 7.0f, max.y - ImGui::GetTextLineHeight() - 4.0f),
                              UITheme::Colors::TextMuted, dimensions.c_str());
            }

            // Bottom-right indicator: Ammo count or stack count
            if (def->maxStack > 1)
            {
                char countText[16];
                std::snprintf(countText, sizeof(countText), "%d/%d", instance.count, def->maxStack);
                const ImVec2 cSize = ImGui::CalcTextSize(countText);
                draw->AddRectFilled(ImVec2(max.x - cSize.x - 7.0f, max.y - cSize.y - 5.0f),
                    ImVec2(max.x - 3.0f, max.y - 2.0f), IM_COL32(10, 12, 16, 220), 2.0f);
                draw->AddText(ImVec2(max.x - cSize.x - 5.0f, max.y - cSize.y - 4.0f),
                              UITheme::Colors::AccentAmber, countText);
            }
            else if (def->kind == EItemKind::Weapon)
            {
                char ammoText[16];
                std::snprintf(ammoText, sizeof(ammoText), "[%d]", instance.loadedAmmo);
                const ImVec2 aSize = ImGui::CalcTextSize(ammoText);
                draw->AddRectFilled(ImVec2(max.x - aSize.x - 7.0f, max.y - aSize.y - 5.0f),
                    ImVec2(max.x - 3.0f, max.y - 2.0f), IM_COL32(10, 12, 16, 220), 2.0f);
                draw->AddText(ImVec2(max.x - aSize.x - 5.0f, max.y - aSize.y - 4.0f),
                              UITheme::Colors::AccentCyan, ammoText);
            }

            // Hover tooltip
            if (hovered)
            {
                ItemTooltip(context, *def, instance);
            }

            // Drag Source
            if (ImGui::BeginDragDropSource())
            {
                StartItemDrag(instance, pos, cellSize);
                const FItemInstanceId id = instance.instanceId;
                ImGui::SetDragDropPayload(kItemPayload, &id, sizeof(id));

                // Floating Ghost Item Card preview
                ItemIconPreview(context, *def, InventoryDrag.rotated, 96.0f);
                ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), "%s", std::string(NextDayz::L10n::Tr(def->displayName)).c_str());
                if (def->maxStack > 1)
                {
                    ImGui::SameLine();
                    ImGui::Text("%d / %d", instance.count, def->maxStack);
                }
                const auto size = InventoryDrag.rotated ? def->size.Rotated() : def->size;
                ImGui::Text(L10n::Tr("%d x %d  [R] Rotate"), size.width, size.height);
                ImGui::EndDragDropSource();
            }

            // Double Click Action
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (def->kind == EItemKind::Clothing && context.wear && context.takeOff)
                {
                    inEquipmentSlot ? context.takeOff(instance.instanceId) : context.wear(instance.instanceId);
                }
                else if (IsWieldable(*def) && context.equipWeapon)
                {
                    context.equipWeapon(instance.instanceId, PreferredWeaponSlot(*def));
                }
                else if (context.useItem)
                {
                    context.useItem(instance.instanceId);
                }
            }

            // Context Menu
            if (ImGui::BeginPopupContextItem("##slot_menu"))
            {
                if (def->maxStack > 1 && context.mergeItems)
                {
                    const bool hasMatching = std::any_of(context.inventory->Instances().begin(),
                        context.inventory->Instances().end(), [&](const auto& other)
                        { return other.instanceId != instance.instanceId && other.defId == instance.defId; });
                    if (TargetMenuItem("Merge matching stacks", hasMatching && instance.count < def->maxStack))
                    {
                        int room = def->maxStack - instance.count;
                        for (const auto& other : context.inventory->Instances())
                            if (room > 0 && other.instanceId != instance.instanceId && other.defId == instance.defId)
                            {
                                context.mergeItems(other.instanceId, instance.instanceId);
                                room -= std::min(room, other.count);
                            }
                    }
                }
                FGridPlacement rotated = instance.placement;
                rotated.rotated = !rotated.rotated;
                if (TargetMenuItem("Rotate", context.inventory->CanPlace(instance.instanceId, instance.containerId, rotated)) &&
                    context.placeItem)
                    context.placeItem(instance.instanceId, instance.containerId, rotated);
                if ((def->kind == EItemKind::Consumable || def->use != EItemUse::None) && context.useItem &&
                    TargetMenuItem("Use"))
                {
                    context.useItem(instance.instanceId);
                }
                if (def->kind == EItemKind::Clothing && !inEquipmentSlot && context.wear && TargetMenuItem("Wear"))
                {
                    context.wear(instance.instanceId);
                }
                if (inEquipmentSlot && context.takeOff && TargetMenuItem("Take off"))
                {
                    context.takeOff(instance.instanceId);
                }
                if (IsWieldable(*def) && context.equipWeapon)
                {
                    if (ImGui::MenuItem(NextDayz::L10n::Label("Equip: Primary (1)"))) context.equipWeapon(instance.instanceId, 0);
                    if (ImGui::MenuItem(NextDayz::L10n::Label("Equip: Secondary (2)"))) context.equipWeapon(instance.instanceId, 1);
                    if (WeaponSystem::CanEquipInSlot(kSidearmSlot, def->weaponDefId) &&
                        ImGui::MenuItem(NextDayz::L10n::Label("Equip: Waist Sidearm (3)"))) context.equipWeapon(instance.instanceId, 2);
                }
                if (def->kind == EItemKind::Weapon && ImGui::BeginMenu(L10n::Label("Attachments")))
                {
                    DrawAttachmentMenu(context, instance);
                    ImGui::EndMenu();
                }
                if (const auto* part = FindAttachmentDef(instance.defId); part && context.weapons &&
                    ImGui::BeginMenu(L10n::Label("Install on weapon")))
                {
                    for (int slot = 0; slot < kWeaponSlotCount; ++slot)
                    {
                        const auto* gun = context.inventory->FindInstance(context.weapons->SlotInstance(slot));
                        if (!gun || !SupportsAttachment(gun->defId, part->type)) continue;
                        const auto name = fmt::format("{} [{}]", L10n::Tr(FindItemDef(gun->defId)->displayName), slot + 1);
                        if (ImGui::MenuItem(name.c_str()) && context.installAttachment)
                            context.installAttachment(gun->instanceId, instance.instanceId);
                    }
                    ImGui::EndMenu();
                }
                if (instance.count > 1 && context.splitItem && TargetMenuItem("Split stack"))
                {
                    context.splitItem(instance.instanceId);
                }
                if (context.bindQuick && ImGui::BeginMenu(NextDayz::L10n::Label("Bind to Quickbar")))
                {
                    for (int s = 0; s < 4; ++s)
                    {
                        char name[128];
                        std::snprintf(name, sizeof(name), NextDayz::L10n::Tr("Key %d"), s + 4);
                        if (ImGui::MenuItem(name))
                        {
                            context.bindQuick(s, instance.defId);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (context.dropItem)
                {
                    if (instance.count > 1 && TargetMenuItem("Drop One"))
                    {
                        context.dropItem(instance.instanceId, 1);
                    }
                    if (TargetMenuItem(instance.count > 1 ? "Drop All" : "Drop"))
                    {
                        context.dropItem(instance.instanceId, -1);
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
            return clicked;
        }

        // Equipment Slot Card (Mockup 03: 5 horizontal square flat-stylized cards)
        void DrawEquipmentSlotTile(const FHudContext& context, const char* label, EEquipSlot slot, float tileW, float tileH)
        {
            using namespace UITheme;
            const Inventory& inventory = *context.inventory;
            const FItemInstanceId worn = inventory.WornInSlot(slot);
            const FItemInstance* instance = worn ? inventory.FindInstance(worn) : nullptr;
            const FItemDef* def = instance ? FindItemDef(instance->defId) : nullptr;

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 max(pos.x + tileW, pos.y + tileH);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImFont* boldFont = BoldFont();

            ImGui::PushID(label);
            ImGui::InvisibleButton("##eq_btn", ImVec2(tileW, tileH));
            MarkLastItem(std::string("wear:") + EquipTargetName(slot));
            const bool hovered = ImGui::IsItemHovered();

            const float r = S(7.0f);
            UIWidgets::DrawSlotFrame(draw, pos, max, instance != nullptr, hovered, false, false, r);

            const char* slotType = (slot == EEquipSlot::Head) ? "HEAD" :
                                   (slot == EEquipSlot::Torso) ? "TORSO" :
                                   (slot == EEquipSlot::Vest) ? "VEST" :
                                   (slot == EEquipSlot::Legs) ? "LEGS" :
                                   (slot == EEquipSlot::Back) ? "BACKPACK" : "GEAR";

            const float iconBottom = max.y - S(36.0f);
            if (def && instance)
            {
                draw->AddRectFilled(pos, ImVec2(pos.x + S(3.5f), max.y), Colors::AccentViolet, r, ImDrawFlags_RoundCornersLeft);
                DrawItemIcon(context, def->id, ImVec2(pos.x + S(8.0f), pos.y + S(6.0f)), ImVec2(max.x - S(8.0f), iconBottom));

                if (hovered) ItemTooltip(context, *def, *instance);
                if (ImGui::BeginDragDropSource())
                {
                    StartItemDrag(*instance, pos);
                    const FItemInstanceId id = instance->instanceId;
                    ImGui::SetDragDropPayload(kItemPayload, &id, sizeof(id));
                    ItemIconPreview(context, *def, InventoryDrag.rotated, S(96.0f));
                    ImGui::TextUnformatted(std::string(NextDayz::L10n::Tr(def->displayName)).c_str());
                    ImGui::EndDragDropSource();
                }
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && context.takeOff)
                    context.takeOff(instance->instanceId);
            }
            else
            {
                UIWidgets::DrawSlotSilhouette(draw, ImVec2(pos.x + S(10.0f), pos.y + S(8.0f)),
                                              ImVec2(max.x - S(10.0f), iconBottom), slotType);
            }

            // Centered slot title & equipped item name / empty dash at bottom of card
            const float lblSz = S(14.5f);
            const ImVec2 lSz = boldFont->CalcTextSizeA(lblSz, FLT_MAX, 0.0f, label);
            draw->AddText(boldFont, lblSz, ImVec2(pos.x + (tileW - lSz.x) * 0.5f, iconBottom + S(2.0f)),
                          Colors::TextPrimary, label);
            const std::string sub = def ? FitText(std::string(L10n::Tr(def->displayName)), tileW - S(10.0f)) : std::string("-");
            const float subSz = S(12.5f);
            const ImVec2 sSz = BodyFont()->CalcTextSizeA(subSz, FLT_MAX, 0.0f, sub.c_str());
            draw->AddText(BodyFont(), subSz, ImVec2(pos.x + (tileW - sSz.x) * 0.5f, max.y - sSz.y - S(4.0f)),
                          def ? Colors::TextHighlight : Colors::TextMuted, sub.c_str());

            // Drag Drop Target: Dropping clothing on this slot wears it
            if (ImGui::BeginDragDropTarget())
            {
                const ImGuiPayload* draggedPayload = ImGui::GetDragDropPayload();
                if (draggedPayload && draggedPayload->IsDataType(kItemPayload))
                {
                    const FItemInstanceId id = *static_cast<const FItemInstanceId*>(draggedPayload->Data);
                    const FItemInstance* dragged = inventory.FindInstance(id);
                    const FItemDef* draggedDef = dragged ? FindItemDef(dragged->defId) : nullptr;
                    if (draggedDef && draggedDef->kind == EItemKind::Clothing && draggedDef->equipSlot == slot &&
                        context.wear && ImGui::AcceptDragDropPayload(kItemPayload))
                    {
                        context.wear(id);
                    }
                }
                if (draggedPayload && draggedPayload->IsDataType(kLootPayload))
                {
                    const int index = *static_cast<const int*>(draggedPayload->Data);
                    const auto loot = std::find_if(context.nearby.begin(), context.nearby.end(),
                        [index](const FHudNearbyLoot& nearby) { return nearby.index == index; });
                    if (loot != context.nearby.end() && loot->equipSlot == slot && context.takeLoot &&
                        ImGui::AcceptDragDropPayload(kLootPayload))
                    {
                        context.takeLoot(index, kWornContainer, slot);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::PopID();
        }

        void DrawWeaponSlot(const FHudContext& context, const char* label, int slot)
        {
            using namespace UITheme;
            const Inventory& inventory = *context.inventory;
            const FItemInstanceId id = context.weapons ? context.weapons->SlotInstance(slot) : 0;
            const FItemInstance* instance = id ? inventory.FindInstance(id) : nullptr;
            const FItemDef* def = instance ? FindItemDef(instance->defId) : nullptr;
            ImFont* boldFont = BoldFont();

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float slotW = ImGui::GetContentRegionAvail().x;
            const float slotH = instance ? S(68.0f) : S(58.0f);
            const ImVec2 max(pos.x + slotW, pos.y + slotH);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float r = S(7.0f);

            ImGui::PushID(slot + 2000);
            ImGui::InvisibleButton("##wp_slot_btn", ImVec2(instance ? slotW - S(84.0f) : slotW, slotH));
            MarkLastItem(fmt::format("weapon:{}", slot));
            const bool hovered = ImGui::IsItemHovered();

            UIWidgets::DrawSlotFrame(draw, pos, max, instance != nullptr, hovered, false, false, r);

            if (def && instance)
            {
                draw->AddRectFilled(pos, ImVec2(pos.x + S(4.0f), max.y), Colors::AccentOrange, r, ImDrawFlags_RoundCornersLeft);
                DrawItemIcon(context, def->id, ImVec2(pos.x + S(8.0f), pos.y + S(5.0f)), ImVec2(pos.x + S(128.0f), max.y - S(5.0f)));

                const float textOffset = S(138.0f);
                draw->AddText(boldFont, S(14.0f), ImVec2(pos.x + textOffset, pos.y + S(8.0f)), Colors::TextSecondary, label);
                const auto name = FitText(std::string(L10n::Tr(def->displayName)), std::max(S(60.0f), slotW - textOffset - S(175.0f)));
                draw->AddText(boldFont, S(18.5f), ImVec2(pos.x + textOffset, pos.y + S(30.0f)), Colors::TextPrimary, name.c_str());

                char ammoText[32];
                std::snprintf(ammoText, sizeof(ammoText), "[%d]", instance->loadedAmmo);
                const float aFontSz = S(16.0f);
                const ImVec2 aSize = boldFont->CalcTextSizeA(aFontSz, FLT_MAX, 0.0f, ammoText);
                const ImVec2 aMin(max.x - aSize.x - S(96.0f), pos.y + (slotH - aSize.y - S(10.0f)) * 0.5f);
                const ImVec2 aMax(max.x - S(82.0f), aMin.y + aSize.y + S(10.0f));
                draw->AddRectFilled(aMin, aMax, IM_COL32(22, 32, 38, 236), S(5.0f));
                draw->AddRect(aMin, aMax, Colors::BorderTactical, S(5.0f), 0, 1.2f);
                draw->AddText(boldFont, aFontSz, ImVec2(aMin.x + S(7.0f), aMin.y + S(5.0f)), Colors::AccentCyan, ammoText);

                if (hovered) ItemTooltip(context, *def, *instance);
            }
            else
            {
                UIWidgets::DrawSlotSilhouette(draw, ImVec2(pos.x + S(12.0f), pos.y + S(6.0f)),
                                              ImVec2(pos.x + S(108.0f), max.y - S(6.0f)),
                                              slot == 2 ? "SIDEARM" : "WEAPON");
                draw->AddText(boldFont, S(14.5f), ImVec2(pos.x + S(118.0f), pos.y + S(10.0f)), Colors::TextSecondary, label);
                draw->AddText(BodyFont(), S(15.0f), ImVec2(pos.x + S(118.0f), pos.y + S(31.0f)), Colors::TextMuted, NextDayz::L10n::Tr("[ NO WEAPON EQUIPPED ]"));
            }

            // Drag drop to equip weapon
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kItemPayload))
                {
                    const auto* dragged = inventory.FindInstance(*static_cast<const FItemInstanceId*>(payload->Data));
                    const auto* definition = dragged ? FindItemDef(dragged->defId) : nullptr;
                    if (instance && dragged && FindAttachmentDef(dragged->defId) && context.installAttachment)
                        context.installAttachment(instance->instanceId, dragged->instanceId);
                    else if (context.equipWeapon && definition && WeaponSystem::CanEquipInSlot(slot, definition->weaponDefId))
                        context.equipWeapon(*static_cast<const FItemInstanceId*>(payload->Data), slot);
                }
                ImGui::EndDragDropTarget();
            }

            // Holster button & Tactical Rails
            if (instance)
            {
                ImGui::SetCursorScreenPos(ImVec2(max.x - S(78.0f), pos.y + (slotH - S(30.0f)) * 0.5f));
                if (UIWidgets::GhostButton(NextDayz::L10n::Label("Holster"), ImVec2(S(70.0f), S(30.0f))) && context.unequipWeapon)
                    context.unequipWeapon(slot);
                ImGui::SetCursorScreenPos(ImVec2(pos.x, max.y + S(5.0f)));

                if (def && def->kind == EItemKind::Weapon)
                {
                    constexpr size_t kSlotCount = kAttachmentSlotCount;
                    const float railGap = S(6.0f);
                    const float railW = std::floor((slotW - railGap * static_cast<float>(kSlotCount - 1)) / static_cast<float>(kSlotCount));
                    const float railH = S(30.0f);

                    for (size_t index = 0; index < kSlotCount; ++index)
                    {
                        const auto kind = static_cast<EAttachmentSlot>(index);
                        const auto* part = FindAttachmentDef(instance->attachments[index]);
                        const ImVec2 rMin = ImGui::GetCursorScreenPos();
                        const ImVec2 rMax(rMin.x + railW, rMin.y + railH);

                        ImGui::PushID(static_cast<int>(index * 100 + slot));
                        ImGui::InvisibleButton("##rail_btn", ImVec2(railW, railH));
                        MarkLastItem(fmt::format("rail:{}:{}", slot, index));
                        const bool rHovered = ImGui::IsItemHovered();

                        const float rr = S(5.0f);
                        draw->AddRectFilled(rMin, rMax, part ? IM_COL32(64, 108, 134, 232) : IM_COL32(20, 25, 22, 205), rr);
                        draw->AddRect(rMin, rMax, part ? Colors::AccentCyan :
                                      rHovered ? Colors::AccentAmber : Colors::BorderMuted, rr, 0, 1.3f);

                        const char* slotTag = (kind == EAttachmentSlot::Optic) ? "OPT" :
                                              (kind == EAttachmentSlot::Muzzle) ? "MUZ" :
                                              (kind == EAttachmentSlot::Magazine) ? "MAG" :
                                              (kind == EAttachmentSlot::Grip) ? "GRP" : "TAC";

                        if (part)
                        {
                            const auto pName = FitText(fmt::format("{}: {}", slotTag, L10n::Tr(part->displayName)), railW - S(12.0f));
                            const float pSz = S(14.5f);
                            const ImVec2 tSz = boldFont->CalcTextSizeA(pSz, FLT_MAX, 0.0f, pName.c_str());
                            draw->AddText(boldFont, pSz, ImVec2(rMin.x + S(7.0f), rMin.y + (railH - tSz.y) * 0.5f), Colors::TextPrimary, pName.c_str());

                            if (rHovered)
                            {
                                ImGui::SetTooltip("%s: %s\n%s", std::string(L10n::Tr(AttachmentSlotName(kind))).c_str(),
                                                  std::string(L10n::Tr(part->displayName)).c_str(),
                                                  L10n::Tr("[RMB] Detach attachment"));
                            }
                            if (ImGui::BeginPopupContextItem("##part_menu"))
                            {
                                if (TargetMenuItem("Detach Attachment") && context.removeAttachment)
                                    context.removeAttachment(instance->instanceId, kind);
                                ImGui::EndPopup();
                            }
                        }
                        else
                        {
                            char emptyText[32];
                            std::snprintf(emptyText, sizeof(emptyText), "+ %s", slotTag);
                            const float eFontSz = S(14.0f);
                            const ImVec2 eSize = boldFont->CalcTextSizeA(eFontSz, FLT_MAX, 0.0f, emptyText);
                            draw->AddText(boldFont, eFontSz,
                                          ImVec2(rMin.x + (railW - eSize.x) * 0.5f, rMin.y + (railH - eSize.y) * 0.5f),
                                          rHovered ? Colors::AccentAmber : Colors::TextSecondary, emptyText);

                            if (rHovered)
                                ImGui::SetTooltip("%s: %s", L10n::Tr(AttachmentSlotName(kind)), L10n::Tr("Drag compatible attachment here"));
                            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                                ImGui::OpenPopup("parts_picker");
                        }

                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const auto* payload = ImGui::AcceptDragDropPayload(kItemPayload))
                            {
                                const auto* dragged = inventory.FindInstance(*static_cast<const FItemInstanceId*>(payload->Data));
                                const auto* accessory = dragged ? FindAttachmentDef(dragged->defId) : nullptr;
                                if (accessory && accessory->slot == kind && context.installAttachment)
                                    context.installAttachment(instance->instanceId, dragged->instanceId);
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (ImGui::BeginPopup("parts_picker"))
                        {
                            DrawAttachmentOptions(context, *instance, kind);
                            ImGui::EndPopup();
                        }

                        ImGui::PopID();
                        if (index + 1 < kSlotCount) ImGui::SameLine(0.0f, railGap);
                    }
                    ImGui::Spacing();
                }
            }

            ImGui::PopID();
            ImGui::Spacing();
        }

        void DrawCargoGrid(const FHudContext& context, const FInventoryContainer& container)
        {
            using namespace UITheme;
            const auto& inventory = *context.inventory;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float maxCellSize = S(62.0f);
            const float cell = std::max(1.0f, std::floor(std::min(maxCellSize,
                ImGui::GetContentRegionAvail().x / static_cast<float>(container.size.width))));
            const ImVec2 gridSize(cell * container.size.width, cell * container.size.height);
            auto* draw = ImGui::GetWindowDrawList();
            ImGui::BeginGroup();
            for (int y = 0; y < container.size.height; ++y)
                for (int x = 0; x < container.size.width; ++x)
                {
                    const ImVec2 a(origin.x + x * cell + 2.0f, origin.y + y * cell + 2.0f);
                    const ImVec2 b(a.x + cell - 4.0f, a.y + cell - 4.0f);
                    UIWidgets::DrawSlotFrame(draw, a, b, false, false, false, false, S(5.0f));
                    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
                    MarkTarget(fmt::format("cell:{}:{}:{}", container.containerId, x, y), a, b);
                    draw->AddCircleFilled(c, 1.8f, Colors::GridCrossDot, 6);
                }
            for (const auto& item : inventory.Instances())
            {
                if (item.containerId != container.containerId) continue;
                const auto size = Inventory::InstanceSize(item);
                ImGui::SetCursorScreenPos(ImVec2(origin.x + item.placement.x * cell + 2.0f,
                                                origin.y + item.placement.y * cell + 2.0f));
                DrawGridItemSlot(context, item, ImVec2(size.width * cell - 4.0f, size.height * cell - 4.0f), false, cell);
            }
            // Dummy reserves the full matrix without intercepting presses on the item cards.
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(gridSize);
            ImGui::EndGroup();
            // EndGroup can inherit the dragged item's ID. A separate target ID also
            // permits rearranging items inside their own container.
            if (!ImGui::BeginDragDropTargetCustom(ImRect(origin, ImVec2(origin.x + gridSize.x, origin.y + gridSize.y)),
                                                ImGui::GetID("##grid_drop"))) return;
            const auto* payload = ImGui::GetDragDropPayload();
            const bool fromItem = payload && payload->IsDataType(kItemPayload);
            const bool fromLoot = payload && payload->IsDataType(kLootPayload);
            const FItemInstance* item = fromItem ? inventory.FindInstance(*static_cast<const FItemInstanceId*>(payload->Data)) : nullptr;
            const FHudNearbyLoot* loot = nullptr;
            if (fromLoot)
                for (const auto& entry : context.nearby)
                    if (entry.index == *static_cast<const int*>(payload->Data)) { loot = &entry; break; }
            const auto* def = item ? FindItemDef(item->defId) : loot ? FindItemDef(loot->itemId) : nullptr;
            if (def)
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const int mouseX = static_cast<int>(std::floor((mouse.x - origin.x) / cell));
                const int mouseY = static_cast<int>(std::floor((mouse.y - origin.y) / cell));
                const FGridPlacement placement{mouseX - InventoryDrag.anchorX, mouseY - InventoryDrag.anchorY, InventoryDrag.rotated};
                const auto size = placement.rotated ? def->size.Rotated() : def->size;
                const auto* merge = inventory.FindStackAt(def->id, container.containerId, mouseX, mouseY,
                                                           item ? item->instanceId : 0);
                const int mergedCount = merge ? std::min(item ? item->count : loot->count, def->maxStack - merge->count) : 0;
                const bool fits = merge ? mergedCount > 0 : (item
                    ? inventory.CanPlace(item->instanceId, container.containerId, placement)
                    : inventory.FitCountAt(def->id, loot->count, container.containerId, placement) > 0);
                const auto dropPlacement = merge ? merge->placement : placement;
                const auto dropSize = merge ? Inventory::InstanceSize(*merge) : size;
                const ImVec2 a(origin.x + dropPlacement.x * cell, origin.y + dropPlacement.y * cell);
                const ImVec2 b(a.x + dropSize.width * cell, a.y + dropSize.height * cell);
                draw->PushClipRect(origin, ImVec2(origin.x + gridSize.x, origin.y + gridSize.y), true);
                draw->AddRectFilled(a, b, fits ? UITheme::Colors::SlotValidDropBg : UITheme::Colors::SlotInvalidDropBg, 3.0f);
                draw->AddRect(a, b, fits ? UITheme::Colors::AccentEmerald : UITheme::Colors::AccentRose, 3.0f, 0, 2.0f);
                draw->PopClipRect();
                if (merge)
                    ImGui::SetTooltip(L10n::Tr("Merge +%d  (%d / %d)"), mergedCount,
                                      merge->count + mergedCount, def->maxStack);
                const auto* accepted = ImGui::AcceptDragDropPayload(fromItem ? kItemPayload : kLootPayload,
                    ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                if (accepted && accepted->IsDelivery() && fits)
                {
                    if (merge && item && context.mergeItems) context.mergeItems(item->instanceId, merge->instanceId);
                    else if (item && context.placeItem) context.placeItem(item->instanceId, container.containerId, placement);
                    else if (loot && context.takeLootAt) context.takeLootAt(loot->index, container.containerId, dropPlacement);
                }
            }
            ImGui::EndDragDropTarget();
        }

        void DrawCargoContainer(const FHudContext& context, const FInventoryContainer& container)
        {
            if (!container.active) return;
            const int used = context.inventory->ContainerUsed(container.containerId);
            const int capacity = container.Capacity();
            ImGui::PushID(static_cast<int>(container.containerId));
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            auto* draw = ImGui::GetWindowDrawList();

            // Tactical elevated container header
            draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 30.0f), UITheme::Colors::SurfaceHeader, 3.0f);
            draw->AddRectFilled(pos, ImVec2(pos.x + 3.0f, pos.y + 30.0f), UITheme::Colors::AccentAmber, 3.0f);
            draw->AddRect(pos, ImVec2(pos.x + width, pos.y + 30.0f), UITheme::Colors::BorderMuted, 3.0f, 0, 1.0f);

            const auto count = fmt::format("{} / {}", used, capacity);
            const auto name = FitText(fmt::format("{}  [{}x{}]", L10n::Tr(container.displayName),
                container.size.width, container.size.height), width - ImGui::CalcTextSize(count.c_str()).x - 30.0f);
            draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 7.0f), UITheme::Colors::TextPrimary, name.c_str());
            draw->AddText(ImVec2(pos.x + width - ImGui::CalcTextSize(count.c_str()).x - 10.0f, pos.y + 7.0f),
                          used >= capacity ? UITheme::Colors::AccentRose : UITheme::Colors::AccentAmber, count.c_str());

            // Bottom micro progress bar on header
            const float frac = capacity > 0 ? std::clamp(static_cast<float>(used) / static_cast<float>(capacity), 0.0f, 1.0f) : 0.0f;
            const ImU32 barCol = frac >= 1.0f ? UITheme::Colors::AccentRose :
                                 frac > 0.75f ? UITheme::Colors::AccentAmber : UITheme::Colors::AccentCyan;
            draw->AddRectFilled(ImVec2(pos.x + 3.0f, pos.y + 28.0f), ImVec2(pos.x + width * frac, pos.y + 30.0f), barCol);

            ImGui::Dummy(ImVec2(width, 30.0f));
            AcceptContainerDrop(context, container.containerId);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L10n::Tr("Drop on the header to auto-place"));
            ImGui::Spacing();
            DrawCargoGrid(context, container);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PopID();
        }

        // ====================================================================
        // CHARACTER PREVIEW, INLINE VITALS & UTILITY ICON TILES (Mockup 03)
        // ====================================================================
        void DrawVitalsMonitor(const FHudContext& context, const ImVec2& pos, const ImVec2& size)
        {
            using namespace UITheme;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImU32 healthCol = context.survival.health > 50.0f ? Colors::AccentEmerald :
                                    context.survival.health > 25.0f ? Colors::AccentAmber : Colors::AccentRose;

            const float barH = S(30.0f);
            const float gap = S(8.0f);
            float y = pos.y;

            char hpText[32];
            std::snprintf(hpText, sizeof(hpText), "%.0f%%", context.survival.health);
            UIWidgets::DrawInlineVitalBar(draw, ImVec2(pos.x, y), ImVec2(pos.x + size.x, y + barH),
                                          UIWidgets::EVitalIcon::Health, L10n::Tr("HEALTH"),
                                          context.survival.health / 100.0f, healthCol, hpText, context.survival.health < 25.0f);
            y += barH + gap;

            char energyText[32];
            std::snprintf(energyText, sizeof(energyText), "%.0f%%", context.survival.hunger);
            UIWidgets::DrawInlineVitalBar(draw, ImVec2(pos.x, y), ImVec2(pos.x + size.x, y + barH),
                                          UIWidgets::EVitalIcon::Hunger, L10n::Tr("HUNGER"),
                                          context.survival.hunger / 100.0f, Colors::AccentAmber, energyText);
            y += barH + gap;

            char hydroText[32];
            std::snprintf(hydroText, sizeof(hydroText), "%.0f%%", context.survival.hydration);
            UIWidgets::DrawInlineVitalBar(draw, ImVec2(pos.x, y), ImVec2(pos.x + size.x, y + barH),
                                          UIWidgets::EVitalIcon::Hydration, L10n::Tr("THIRST"),
                                          context.survival.hydration / 100.0f, Colors::AccentCyan, hydroText);
            y += barH + gap;

            char armorText[32];
            std::snprintf(armorText, sizeof(armorText), "%.0f%%", context.armor * 100.0f);
            UIWidgets::DrawInlineVitalBar(draw, ImVec2(pos.x, y), ImVec2(pos.x + size.x, y + barH),
                                          UIWidgets::EVitalIcon::Armor, L10n::Tr("ARMOR"),
                                          context.armor, IM_COL32(118, 128, 124, 255), armorText);
            y += barH + S(14.0f);

            // 4 Square Utility Icon Tiles underneath vitals (Mockup 03: Compass, Map, Opener, Lantern)
            const float tileGap = S(8.0f);
            const float tileW = std::floor((size.x - tileGap * 3.0f) / 4.0f);
            const float tileH = std::min(S(74.0f), std::max(S(56.0f), pos.y + size.y - y));
            ImFont* boldFont = BoldFont();
            const auto drawToolTile = [&](int idx, UIWidgets::EVitalIcon icon, const char* name, bool owned)
            {
                const ImVec2 tMin(pos.x + idx * (tileW + tileGap), y);
                const ImVec2 tMax(tMin.x + tileW, tMin.y + tileH);
                const float r = S(7.0f);
                draw->AddRectFilled(tMin, tMax, owned ? IM_COL32(34, 42, 37, 238) : IM_COL32(18, 22, 20, 195), r);
                draw->AddRect(tMin, tMax, owned ? Colors::AccentAmber : Colors::BorderMuted, r, 0, owned ? 1.5f : 1.1f);
                const ImU32 iconCol = owned ? Colors::TextPrimary : Colors::TextMuted;
                UIWidgets::DrawVitalIcon(draw, ImVec2((tMin.x + tMax.x) * 0.5f, tMin.y + tileH * 0.38f),
                                         std::min(tileW * 0.52f, S(28.0f)), icon, iconCol);
                const float lblSz = S(13.0f);
                const std::string fit = FitText(name, tileW - S(6.0f));
                const ImVec2 txtSz = boldFont->CalcTextSizeA(lblSz, FLT_MAX, 0.0f, fit.c_str());
                draw->AddText(boldFont, lblSz,
                              ImVec2(tMin.x + (tileW - txtSz.x) * 0.5f, tMax.y - txtSz.y - S(5.0f)),
                              owned ? Colors::TextPrimary : Colors::TextMuted, fit.c_str());
            };

            drawToolTile(0, UIWidgets::EVitalIcon::Compass, L10n::Tr("COMPASS"), context.inventory->HasTool(EItemTool::Compass));
            drawToolTile(1, UIWidgets::EVitalIcon::Map, L10n::Tr("MAP"), context.inventory->HasTool(EItemTool::Map));
            drawToolTile(2, UIWidgets::EVitalIcon::Opener, L10n::Tr("OPENER"), context.inventory->CanOpenCans());
            drawToolTile(3, UIWidgets::EVitalIcon::Lantern, L10n::Tr("LANTERN"), context.lanternLit || context.lanternMinutes > 0.0f);

            float statusY = y + tileH + S(8.0f);
            if (context.survival.bleedingWounds > 0)
            {
                const auto label = L10n::Format("BLEEDING x{}", context.survival.bleedingWounds);
                const ImVec2 badge = UIWidgets::DrawStatusPill(draw, ImVec2(pos.x, statusY), UIWidgets::EVitalIcon::Bleeding,
                    label.c_str(), IM_COL32(160, 42, 42, 232), Colors::AccentRose, Colors::TextPrimary);
                statusY += badge.y + S(4.0f);
            }
            if (context.survival.healing)
            {
                UIWidgets::DrawStatusPill(draw, ImVec2(pos.x, statusY), UIWidgets::EVitalIcon::Healing, L10n::Tr("RECOVERING"),
                    IM_COL32(30, 68, 42, 232), Colors::AccentEmerald, Colors::TextPrimary);
            }
        }

        void DrawCharacterPreview(const FHudContext& context, float height)
        {
            using namespace UITheme;
            static float yaw = 0.35f;
            static float distance = 3.4f;

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
            const ImVec2 max(pos.x + width, pos.y + height);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float r = S(8.0f);
            draw->AddRectFilled(pos, max, Colors::SurfaceCard, r);
            draw->AddRect(pos, max, Colors::BorderTactical, r, 0, 1.4f);

            const float vitalsWidth = std::clamp(width * 0.48f, S(210.0f), S(320.0f));
            const float dividerX = max.x - vitalsWidth - S(26.0f);
            const ImVec2 previewPos(pos.x + S(10.0f), pos.y + S(12.0f));
            const ImVec2 previewSize(std::max(1.0f, dividerX - previewPos.x - S(10.0f)), std::max(S(140.0f), height - S(52.0f)));
            const ImVec2 previewMax(previewPos.x + previewSize.x, previewPos.y + previewSize.y);
            draw->AddLine(ImVec2(dividerX, pos.y + S(14.0f)), ImVec2(dividerX, max.y - S(14.0f)), Colors::BorderMuted);

            ImGui::SetCursorScreenPos(previewPos);
            ImGui::InvisibleButton("##character_orbit", previewSize);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                yaw = std::remainder(yaw - ImGui::GetIO().MouseDelta.x * 0.012f, glm::two_pi<float>());
            if (ImGui::IsItemHovered())
            {
                distance = std::clamp(distance - ImGui::GetIO().MouseWheel * 0.2f, 2.4f, 5.0f);
                ImGui::SetTooltip("%s", L10n::Tr("Drag to rotate / Scroll to zoom"));
            }

            const uint64_t texture = context.characterPreview
                ? context.characterPreview(glm::vec2(previewSize.x, previewSize.y), yaw, distance) : 0;
            if (texture)
                draw->AddImage(static_cast<ImTextureID>(texture), previewPos, previewMax);
            else
                draw->AddText(ImVec2(previewPos.x + S(10.0f), previewPos.y + previewSize.y * 0.45f),
                              Colors::TextMuted, L10n::Tr("Preparing character preview..."));

            ImGui::SetCursorScreenPos(ImVec2(previewPos.x + S(4.0f), previewMax.y + S(6.0f)));
            if (ImGui::SmallButton(L10n::Label("Front"))) yaw = 0.0f;
            ImGui::SameLine();
            if (ImGui::SmallButton(L10n::Label("Back"))) yaw = glm::pi<float>();
            ImGui::SameLine();
            if (ImGui::SmallButton(L10n::Label("Reset")))
            {
                yaw = 0.35f;
                distance = 3.4f;
            }

            DrawVitalsMonitor(context, ImVec2(dividerX + S(14.0f), pos.y + S(16.0f)), ImVec2(vitalsWidth, height - S(30.0f)));

            ImGui::SetCursorScreenPos(ImVec2(pos.x, max.y + S(8.0f)));
            ImGui::Dummy(ImVec2(width, 0.0f));
        }

        void DrawTacticalQuickbar(const FHudContext& context)
        {
            if (context.inventory == nullptr) return;
            using namespace UITheme;
            ImFont* boldFont = BoldFont();

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const float slotH = S(62.0f);
            constexpr int kSlots = 4;
            const float gap = S(8.0f);
            const float slotW = std::floor((width - gap * (kSlots - 1)) / static_cast<float>(kSlots));
            ImDrawList* draw = ImGui::GetWindowDrawList();

            draw->AddText(boldFont, S(14.5f), ImVec2(pos.x + S(2.0f), pos.y), Colors::TextSecondary, L10n::Tr("TACTICAL QUICKBAR [4 - 7]"));
            const float startY = pos.y + S(22.0f);
            const float r = S(7.0f);

            for (int slot = 0; slot < kSlots; ++slot)
            {
                const ImVec2 a(pos.x + slot * (slotW + gap), startY);
                const ImVec2 b(a.x + slotW, a.y + slotH);
                const std::string& itemId = context.quickSlots[slot];
                const int count = itemId.empty() ? 0 : context.inventory->CountOf(itemId);
                const bool occupied = !itemId.empty();
                const FItemDef* def = occupied ? FindItemDef(itemId) : nullptr;

                ImGui::PushID(slot + 5000);
                ImGui::SetCursorScreenPos(a);
                ImGui::InvisibleButton("##qb_slot", ImVec2(slotW, slotH));
                const bool hovered = ImGui::IsItemHovered();

                UIWidgets::DrawSlotFrame(draw, a, b, occupied, hovered, false, false, r);

                char key[8];
                std::snprintf(key, sizeof(key), "%d", slot + 4);
                UIWidgets::DrawKeyBadge(draw, ImVec2(a.x + S(5.0f), a.y + S(5.0f)), key, S(20.0f));

                if (occupied && def)
                {
                    DrawItemIcon(context, def->id, ImVec2(a.x + S(28.0f), a.y + S(4.0f)), ImVec2(b.x - S(6.0f), b.y - S(20.0f)));

                    if (count > 0)
                    {
                        char cStr[16];
                        std::snprintf(cStr, sizeof(cStr), "x%d", count);
                        const float cSz = S(14.5f);
                        const ImVec2 cSize = boldFont->CalcTextSizeA(cSz, FLT_MAX, 0.0f, cStr);
                        draw->AddText(boldFont, cSz, ImVec2(b.x - cSize.x - S(6.0f), b.y - cSize.y - S(4.0f)),
                                      Colors::AccentAmber, cStr);
                    }

                    const auto sName = FitText(std::string(L10n::Tr(def->displayName)), slotW - S(34.0f));
                    const float nSz = S(13.5f);
                    const ImVec2 nSize = BodyFont()->CalcTextSizeA(nSz, FLT_MAX, 0.0f, sName.c_str());
                    draw->AddText(BodyFont(), nSz, ImVec2(a.x + S(6.0f), b.y - nSize.y - S(4.0f)), Colors::TextSecondary, sName.c_str());

                    if (hovered)
                    {
                        ImGui::SetTooltip("%s [%d]\n%s\n%s", L10n::Tr("Quickbar Slot"), slot + 4,
                                          std::string(L10n::Tr(def->displayName)).c_str(),
                                          L10n::Tr("[RMB] Unbind slot"));
                    }

                    if (ImGui::BeginPopupContextItem("##qb_ctx"))
                    {
                        if (ImGui::MenuItem(L10n::Label("Unbind Slot")) && context.bindQuick)
                            context.bindQuick(slot, "");
                        ImGui::EndPopup();
                    }
                }
                else
                {
                    const char* emptyLabel = L10n::Tr("[ EMPTY ]");
                    const ImVec2 eSz = BodyFont()->CalcTextSizeA(S(14.0f), FLT_MAX, 0.0f, emptyLabel);
                    draw->AddText(BodyFont(), S(14.0f),
                                  ImVec2(a.x + (slotW - eSz.x) * 0.5f, a.y + (slotH - eSz.y) * 0.5f),
                                  Colors::TextMuted, emptyLabel);
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const auto* payload = ImGui::AcceptDragDropPayload(kItemPayload))
                    {
                        const auto* dragged = context.inventory->FindInstance(*static_cast<const FItemInstanceId*>(payload->Data));
                        if (dragged && context.bindQuick)
                            context.bindQuick(slot, dragged->defId);
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::PopID();
            }

            ImGui::SetCursorScreenPos(ImVec2(pos.x, startY + slotH + S(6.0f)));
            ImGui::Dummy(ImVec2(width, 0.0f));
        }

        void DrawVicinityColumn(const FHudContext& context, float columnHeight)
        {
            const float dropH = 100.0f;
            const float lootListH = std::max(80.0f, columnHeight - dropH - 24.0f);

            // 1. NEARBY LOOT SECTION
            ImGui::BeginChild("##vicinity_loot", ImVec2(0.0f, lootListH), false);

            const ImVec2 hPos = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const ImVec2 hMax(hPos.x + w, hPos.y + 26.0f);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            // Tactical Header for loot
            draw->AddRectFilled(hPos, hMax, UITheme::Colors::SurfaceHeader, 3.0f);
            draw->AddLine(ImVec2(hPos.x, hMax.y), hMax, UITheme::Colors::BorderMuted, 1.0f);
            draw->AddRectFilled(ImVec2(hPos.x + 3.0f, hPos.y + 5.0f), ImVec2(hPos.x + 6.0f, hMax.y - 5.0f), UITheme::Colors::AccentAmber, 1.0f);
            draw->AddText(ImVec2(hPos.x + 12.0f, hPos.y + 5.0f), UITheme::Colors::TextPrimary, L10n::Tr("NEARBY GROUND LOOT"));

            char countStr[24];
            std::snprintf(countStr, sizeof(countStr), "[ %d ]", static_cast<int>(context.nearby.size()));
            const ImVec2 cSize = ImGui::CalcTextSize(countStr);
            draw->AddText(ImVec2(hMax.x - cSize.x - 8.0f, hPos.y + 5.0f),
                          context.nearby.empty() ? UITheme::Colors::TextMuted : UITheme::Colors::AccentAmber, countStr);

            ImGui::Dummy(ImVec2(w, 28.0f));
            ImGui::Spacing();

            if (context.nearby.empty())
            {
                // Radar sweep frame when empty
                const ImVec2 ePos = ImGui::GetCursorScreenPos();
                const float eH = 120.0f;
                const ImVec2 eMax(ePos.x + w, ePos.y + eH);
                draw->AddRectFilled(ePos, eMax, UITheme::Colors::SurfaceCard, 3.0f);
                draw->AddRect(ePos, eMax, UITheme::Colors::BorderMuted, 3.0f, 0, 1.0f);
                UIWidgets::DrawCornerTicks(draw, ePos, eMax, 6.0f, UITheme::Colors::BorderTactical);

                // Subtle reticle circle in center
                const ImVec2 eCenter((ePos.x + eMax.x) * 0.5f, ePos.y + 45.0f);
                draw->AddCircle(eCenter, 20.0f, IM_COL32(50, 65, 85, 100), 24, 1.0f);
                draw->AddLine(ImVec2(eCenter.x - 26.0f, eCenter.y), ImVec2(eCenter.x + 26.0f, eCenter.y), IM_COL32(50, 65, 85, 100), 1.0f);
                draw->AddLine(ImVec2(eCenter.x, eCenter.y - 26.0f), ImVec2(eCenter.x, eCenter.y + 26.0f), IM_COL32(50, 65, 85, 100), 1.0f);

                const char* msg = L10n::Tr("NO ITEMS IN IMMEDIATE VICINITY");
                const ImVec2 mSize = ImGui::CalcTextSize(msg);
                draw->AddText(ImVec2(ePos.x + (w - mSize.x) * 0.5f, ePos.y + 82.0f), UITheme::Colors::TextMuted, msg);

                ImGui::Dummy(ImVec2(w, eH));
            }
            else
            {
                int lootRow = 0;
                for (const FHudNearbyLoot& loot : context.nearby)
                {
                    const int row = lootRow++;
                    ImGui::PushID(loot.index);
                    const ImVec2 lootPos = ImGui::GetCursorScreenPos();
                    const float cardW = ImGui::GetContentRegionAvail().x;
                    const float cardH = 44.0f;
                    const ImVec2 cardMax(lootPos.x + cardW, lootPos.y + cardH);

                    // The drag area stops short of the Take button: ImGui gives an overlap to
                    // the item submitted first, which would swallow every click on Take.
                    ImGui::SetCursorScreenPos(ImVec2(lootPos.x + 58.0f, lootPos.y));
                    ImGui::InvisibleButton("##loot_card", ImVec2(std::max(1.0f, cardW - 58.0f - 62.0f), cardH));
                    MarkLastItem(fmt::format("loot:{}", row));
                    const bool hovered = ImGui::IsItemHovered();

                    if (ImGui::BeginDragDropSource())
                    {
                        if (InventoryDrag.loot != loot.index || InventoryDrag.item)
                            InventoryDrag = {0, loot.index, false, 0, 0};
                        ImGui::SetDragDropPayload(kLootPayload, &loot.index, sizeof(loot.index));
                        ImGui::TextUnformatted(NextDayz::L10n::Localize(loot.name, NextDayz::L10n::Language()).c_str());
                        ImGui::EndDragDropSource();
                    }

                    UIWidgets::DrawSlotFrame(draw, lootPos, cardMax, true, hovered, false, false, 3.0f);
                    UIWidgets::DrawCornerTicks(draw, lootPos, cardMax, 5.0f, hovered ? UITheme::Colors::AccentAmber : UITheme::Colors::BorderMuted);

                    // Category / Accent stripe on left
                    draw->AddRectFilled(lootPos, ImVec2(lootPos.x + 3.0f, cardMax.y), UITheme::Colors::AccentAmber, 3.0f);

                    // Icon if available
                    DrawItemIcon(context, loot.itemId, ImVec2(lootPos.x + 6.0f, lootPos.y + 4.0f), ImVec2(lootPos.x + 52.0f, cardMax.y - 4.0f));

                    // Quick Take Button on left/mid
                    ImGui::SetCursorScreenPos(ImVec2(cardMax.x - 56.0f, lootPos.y + 8.0f));
                    const bool take = ImGui::Button(NextDayz::L10n::Label("Take"), ImVec2(50.0f, 26.0f));
                    MarkLastItem(fmt::format("take:{}", row));
                    if (take && context.takeLoot)
                    {
                        context.takeLoot(loot.index, 0, EEquipSlot::None);
                    }

                    // Loot Name & Distance
                    char distText[16];
                    std::snprintf(distText, sizeof(distText), "%.1fm", loot.distance);
                    const auto name = FitText(L10n::Localize(loot.name, L10n::Language()), std::max(1.0f, cardW - 130.0f));
                    draw->AddText(ImVec2(lootPos.x + 58.0f, lootPos.y + 6.0f), UITheme::Colors::TextPrimary, name.c_str());
                    draw->AddText(ImVec2(lootPos.x + 58.0f, lootPos.y + 25.0f), UITheme::Colors::AccentCyan, distText);

                    ImGui::SetCursorScreenPos(ImVec2(lootPos.x, cardMax.y + 4.0f));
                    ImGui::PopID();
                    ImGui::Spacing();
                }
            }

            ImGui::EndChild();

            // 2. RAPID JETTISON / DROP ZONE SECTION
            ImGui::Spacing();
            const ImVec2 dropPos = ImGui::GetCursorScreenPos();
            const float dropW = ImGui::GetContentRegionAvail().x;
            const ImVec2 dropMax(dropPos.x + dropW, dropPos.y + dropH);
            ImDrawList* dDraw = ImGui::GetWindowDrawList();

            ImGui::InvisibleButton("##drop_zone_btn", ImVec2(dropW, dropH));
            MarkLastItem("drop");
            const bool dHovered = ImGui::IsItemHovered();

            bool activeDrop = false;
            const auto* payload = ImGui::GetDragDropPayload();
            if (payload && (payload->IsDataType(kItemPayload) || payload->IsDataType(kLootPayload)))
            {
                activeDrop = dHovered;
            }

            UIWidgets::DrawHazardDropZone(dDraw, dropPos, dropMax, L10n::Tr("RAPID JETTISON // DRAG HERE TO DROP"),
                                         dHovered, activeDrop);

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kItemPayload))
                {
                    if (context.dropItem)
                    {
                        context.dropItem(*static_cast<const FItemInstanceId*>(p->Data), -1);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        void DrawInventoryPanel(const FHudContext& context)
        {
            if (!context.showInventory || context.inventory == nullptr)
            {
                return;
            }
            const auto* payload = ImGui::GetDragDropPayload();
            if (!payload) InventoryDrag = {};
            else if ((payload->IsDataType(kItemPayload) || payload->IsDataType(kLootPayload)) &&
                     ImGui::IsKeyPressed(ImGuiKey_R, false))
            {
                InventoryDrag.rotated = !InventoryDrag.rotated;
                InventoryDrag.anchorX = InventoryDrag.anchorY = 0;
            }
            const Inventory& inventory = *context.inventory;
            const ImGuiViewport* vp = ImGui::GetMainViewport();

            ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(vp->Size, ImGuiCond_Always);
            UITheme::FFieldStyleScope style;
            ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(10, 12, 11, 224));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 14.0f));

            ImGui::Begin("##nd_inv_window", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

            // ====================================================================
            // INVENTORY TITLE (Mockup 03)
            // ====================================================================
            using namespace UITheme;
            const ImVec2 headerPos = ImGui::GetCursorScreenPos();
            const float fullW = ImGui::GetContentRegionAvail().x;
            const float headerH = S(54.0f);
            const ImVec2 headerMax(headerPos.x + fullW, headerPos.y + headerH);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            draw->AddRectFilled(headerPos, headerMax, Colors::SurfaceHeader, S(8.0f));
            draw->AddRect(headerPos, headerMax, Colors::BorderTactical, S(8.0f), 0, 1.4f);
            draw->AddRectFilled(headerPos, ImVec2(headerPos.x + S(5.5f), headerMax.y), Colors::AccentAmber, S(8.0f), ImDrawFlags_RoundCornersLeft);

            ImGui::PushFont(HeadingFont(), S(30.0f));
            ImGui::SetCursorScreenPos(ImVec2(headerPos.x + S(18.0f), headerPos.y + (headerH - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::TextUnformatted(L10n::Tr("Inventory"));
            ImGui::PopFont();

            const float closeBtnW = S(136.0f);
            const float closeBtnH = S(36.0f);
            ImGui::SetCursorScreenPos(ImVec2(headerMax.x - closeBtnW - S(10.0f), headerPos.y + (headerH - closeBtnH) * 0.5f));
            if (UIWidgets::GhostButton(L10n::Label("CLOSE [TAB]"), ImVec2(closeBtnW, closeBtnH)) && context.closeInventory)
            {
                context.closeInventory();
            }

            ImGui::SetCursorScreenPos(ImVec2(headerPos.x, headerMax.y + S(12.0f)));

            // ====================================================================
            // 3-COLUMN ERGONOMIC TACTICAL LAYOUT
            // Column 0: VICINITY & JETTISON (0.22)
            // Column 1: OPERATOR DOSSIER, VITALS & WEAPONS (0.38)
            // Column 2: 5-TILE EQUIPMENT ROW, CARGO RIGS & QUICKBAR (0.40)
            // ====================================================================
            const float footerHeight = S(48.0f);
            const float bodyHeight = std::max(S(140.0f), ImGui::GetContentRegionAvail().y - footerHeight);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(S(12.0f), S(6.0f)));

            if (ImGui::BeginTable("##inv_3col_table", 3,
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp,
                                  ImVec2(0.0f, bodyHeight)))
            {
                ImGui::TableSetupColumn("vicinity", ImGuiTableColumnFlags_WidthStretch, 0.22f);
                ImGui::TableSetupColumn("operator_center", ImGuiTableColumnFlags_WidthStretch, 0.38f);
                ImGui::TableSetupColumn("rigs_and_cargo", ImGuiTableColumnFlags_WidthStretch, 0.40f);
                ImGui::TableNextRow();
                const float columnHeight = bodyHeight - S(12.0f);

                // COLUMN 0: VICINITY & JETTISON
                ImGui::TableSetColumnIndex(0);
                DrawVicinityColumn(context, columnHeight);

                // COLUMN 1: OPERATOR DOSSIER, INLINE VITALS & WEAPONS
                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("##col_operator", ImVec2(0.0f, columnHeight), false);

                const float characterHeight = std::clamp(columnHeight * 0.44f, S(280.0f), S(360.0f));
                DrawCharacterPreview(context, characterHeight);
                ImGui::Spacing();

                UIWidgets::DrawSectionHeader(L10n::Tr("TACTICAL ARSENAL & WEAPON RIGS"), nullptr, Colors::AccentOrange);
                DrawWeaponSlot(context, NextDayz::L10n::Tr("PRIMARY WEAPON [1]"), 0);
                DrawWeaponSlot(context, NextDayz::L10n::Tr("SECONDARY WEAPON [2]"), 1);
                DrawWeaponSlot(context, NextDayz::L10n::Tr("WAIST SIDEARM [3]"), 2);

                ImGui::EndChild();

                // COLUMN 2: 5-CARD HORIZONTAL GEAR ROW, CARGO RIGS & QUICKBAR
                ImGui::TableSetColumnIndex(2);
                const float qbHeight = S(94.0f);
                const float cargoScrollHeight = std::max(S(120.0f), columnHeight - qbHeight - S(8.0f));

                ImGui::BeginChild("##col_cargo_scroll", ImVec2(0.0f, cargoScrollHeight), false);

                UIWidgets::DrawSectionHeader(L10n::Tr("BODY ARMOR & EQUIPMENT RIG"), nullptr, Colors::AccentViolet);
                const float eqAvailW = ImGui::GetContentRegionAvail().x;
                const float eqGap = S(8.0f);
                const float eqTileW = std::floor((eqAvailW - eqGap * 4.0f) / 5.0f);
                const float eqTileH = std::clamp(eqTileW * 1.12f, S(86.0f), S(120.0f));
                DrawEquipmentSlotTile(context, NextDayz::L10n::Tr("HEAD"), EEquipSlot::Head, eqTileW, eqTileH);
                ImGui::SameLine(0.0f, eqGap);
                DrawEquipmentSlotTile(context, NextDayz::L10n::Tr("TORSO"), EEquipSlot::Torso, eqTileW, eqTileH);
                ImGui::SameLine(0.0f, eqGap);
                DrawEquipmentSlotTile(context, NextDayz::L10n::Tr("VEST"), EEquipSlot::Vest, eqTileW, eqTileH);
                ImGui::SameLine(0.0f, eqGap);
                DrawEquipmentSlotTile(context, NextDayz::L10n::Tr("LEGS"), EEquipSlot::Legs, eqTileW, eqTileH);
                ImGui::SameLine(0.0f, eqGap);
                DrawEquipmentSlotTile(context, NextDayz::L10n::Tr("BACKPACK"), EEquipSlot::Back, eqTileW, eqTileH);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                for (const auto& container : inventory.Containers())
                {
                    DrawCargoContainer(context, container);
                }

                ImGui::EndChild();

                ImGui::Spacing();
                DrawTacticalQuickbar(context);

                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            // ====================================================================
            // BOTTOM TACTICAL HOTKEYS & NOTIFICATIONS BAR
            // ====================================================================
            ImGui::Spacing();
            const ImVec2 fPos = ImGui::GetCursorScreenPos();
            const float fW = ImGui::GetContentRegionAvail().x;
            const float fH = S(38.0f);
            const ImVec2 fMax(fPos.x + fW, fPos.y + fH);

            draw->AddRectFilled(fPos, fMax, Colors::SurfaceHeader, S(7.0f));
            draw->AddRect(fPos, fMax, Colors::BorderMuted, S(7.0f), 0, 1.2f);

            ImFont* boldFont = BoldFont();
            if (!context.notifications.empty() && context.notifications.back().age < 6.5f)
            {
                const auto& message = context.notifications.back();
                const ImU32 notifCol = message.severity == 2 ? Colors::AccentRose :
                                       message.severity == 1 ? Colors::AccentAmber : Colors::AccentEmerald;
                const auto text = FitText(message.text.Display(), fW * 0.36f);
                const float nSz = S(15.0f);
                const ImVec2 nSize = boldFont->CalcTextSizeA(nSz, FLT_MAX, 0.0f, text.c_str());
                draw->AddRectFilled(ImVec2(fMax.x - nSize.x - S(20.0f), fPos.y + S(5.0f)),
                                    ImVec2(fMax.x - S(6.0f), fMax.y - S(5.0f)), IM_COL32(20, 26, 22, 232), S(5.0f));
                draw->AddText(boldFont, nSz, ImVec2(fMax.x - nSize.x - S(13.0f), fPos.y + (fH - nSize.y) * 0.5f), notifCol, text.c_str());
            }

            float curX = fPos.x + S(10.0f);
            const float badgeH = S(24.0f);
            const float keyY = fPos.y + (fH - badgeH) * 0.5f;
            const float guideSz = S(15.0f);
            const auto drawKeyGuide = [&](const char* key, const char* desc)
            {
                const float kw = UIWidgets::DrawKeyBadge(draw, ImVec2(curX, keyY), key, badgeH);
                curX += kw + S(6.0f);
                const ImVec2 dSz = boldFont->CalcTextSizeA(guideSz, FLT_MAX, 0.0f, desc);
                draw->AddText(boldFont, guideSz, ImVec2(curX, fPos.y + (fH - dSz.y) * 0.5f), Colors::TextSecondary, desc);
                curX += dSz.x + S(16.0f);
            };

            drawKeyGuide("LMB x2", L10n::Tr("Equip/Use"));
            drawKeyGuide("RMB", L10n::Tr("Menu"));
            drawKeyGuide("DRAG", L10n::Tr("Move"));
            drawKeyGuide("R", L10n::Tr("Rotate"));
            drawKeyGuide("4-7", L10n::Tr("Quickbar"));
            drawKeyGuide("H", L10n::Tr("Bandage"));
            drawKeyGuide("M", L10n::Tr("Map"));
            drawKeyGuide("TAB", L10n::Tr("Close"));

            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(1);
        }

        // ====================================================================
        // SESSION OVERLAY (UNIFIED PAUSE & KIA DEBRIEF)
        // ====================================================================
        void DrawSessionOverlay(const FHudContext& context)
        {
            if (context.settingsOpen) return;
            if (!context.paused && context.survival.lifeState != EPlayerLifeState::Dead)
            {
                return;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const bool dead = context.survival.lifeState == EPlayerLifeState::Dead;
            ImGui::GetBackgroundDrawList()->AddRectFilled(
                viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                dead ? IM_COL32(24, 8, 8, 212) : IM_COL32(10, 12, 11, 198));

            UITheme::FFieldStyleScope style;
            using namespace UITheme;
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(std::min(dead ? S(620.0f) : S(800.0f), viewport->WorkSize.x - S(40.0f)), 0.0f), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(30.0f), S(26.0f)));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(12.0f), S(10.0f)));

            ImGui::Begin("##nd_session", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings);

            const ImVec2 winMin = ImGui::GetWindowPos();
            const ImVec2 winMax(winMin.x + ImGui::GetWindowSize().x, winMin.y + ImGui::GetWindowSize().y);
            ImGui::GetWindowDrawList()->AddRectFilled(
                winMin, ImVec2(winMax.x, winMin.y + S(4.0f)),
                dead ? Colors::AccentRose : Colors::AccentAmber, S(8.0f), ImDrawFlags_RoundCornersTop);

            if (dead)
            {
                ImGui::PushFont(BoldFont(), S(15.5f));
                ImGui::TextColored(ColorVec4(Colors::AccentRose), "%s", NextDayz::L10n::Tr("KILLED IN ACTION"));
                ImGui::PopFont();
                ImGui::PushFont(HeadingFont(), S(36.0f));
                ImGui::TextUnformatted(NextDayz::L10n::Tr("YOU ARE DEAD"));
                ImGui::PopFont();
                ImGui::Spacing();

                if (ImGui::BeginTable("##kia_stats", 3, ImGuiTableFlags_SizingStretchSame))
                {
                    const auto statCard = [](const char* label, const std::string& val, ImU32 accent)
                    {
                        ImGui::TableNextColumn();
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        const float w = ImGui::GetContentRegionAvail().x;
                        const float h = S(68.0f);
                        const ImVec2 q(p.x + w, p.y + h);
                        UIWidgets::DrawFieldCard(ImGui::GetWindowDrawList(), p, q, accent);
                        ImGui::GetWindowDrawList()->AddText(BoldFont(), S(14.0f), ImVec2(p.x + S(14.0f), p.y + S(10.0f)), Colors::TextSecondary, label);
                        ImGui::GetWindowDrawList()->AddText(HeadingFont(), S(24.0f), ImVec2(p.x + S(14.0f), p.y + S(32.0f)), Colors::TextPrimary, val.c_str());
                        ImGui::Dummy(ImVec2(w, h));
                    };
                    const std::string cause = context.survival.lastDamageSource.empty()
                        ? std::string(NextDayz::L10n::Tr("Unknown"))
                        : std::string(NextDayz::L10n::Tr(context.survival.lastDamageSource));
                    char timeStr[32];
                    std::snprintf(timeStr, sizeof(timeStr), "%02d:%02d",
                                  static_cast<int>(context.survivalSeconds) / 60,
                                  static_cast<int>(context.survivalSeconds) % 60);
                    statCard(NextDayz::L10n::Tr("Cause of death"), cause, Colors::AccentRose);
                    statCard(NextDayz::L10n::Tr("Survival time"), timeStr, Colors::AccentAmber);
                    statCard(NextDayz::L10n::Tr("Infected eliminated"), std::to_string(context.kills), Colors::TextSecondary);
                    ImGui::EndTable();
                }

                ImGui::Spacing();
                ImGui::TextColored(ColorVec4(Colors::TextSecondary), "%s",
                                   NextDayz::L10n::Tr("Your carried gear remains where you fell. Storage and gold are safe."));
                ImGui::Spacing();
                if (context.restartSession && UIWidgets::PrimaryButton(NextDayz::L10n::Label("WAKE UP AGAIN"), ImVec2(-1.0f, S(48.0f))))
                    context.restartSession();
                if (context.returnToLobby && UIWidgets::PrimaryButton(NextDayz::L10n::Label("RETURN TO LOBBY"), ImVec2(-1.0f, S(44.0f))))
                    context.returnToLobby();
                if (context.openSettings && UIWidgets::GhostButton(NextDayz::L10n::Label("SETTINGS"), ImVec2(-1.0f, S(40.0f))))
                    context.openSettings();
            }
            else
            {
                if (ImGui::BeginTable("##pause_cols", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthStretch, 0.44f);
                    ImGui::TableSetupColumn("controls", ImGuiTableColumnFlags_WidthStretch, 0.56f);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    ImGui::PushFont(BoldFont(), S(15.0f));
                    ImGui::TextColored(ColorVec4(Colors::AccentAmber), "%s", NextDayz::L10n::Tr("Pause"));
                    ImGui::PopFont();
                    ImGui::PushFont(HeadingFont(), S(34.0f));
                    ImGui::TextUnformatted(NextDayz::L10n::Tr("GAME PAUSED"));
                    ImGui::PopFont();
                    ImGui::TextDisabled("%s", NextDayz::L10n::Tr("Press Esc to resume"));
                    ImGui::Spacing();

                    if (context.resumeSession && UIWidgets::PrimaryButton(NextDayz::L10n::Label("RESUME GAME"), ImVec2(-1.0f, S(46.0f))))
                        context.resumeSession();
                    if (context.openSettings && UIWidgets::GhostButton(NextDayz::L10n::Label("SETTINGS"), ImVec2(-1.0f, S(40.0f))))
                        context.openSettings();
                    if (context.restartSession && UIWidgets::GhostButton(NextDayz::L10n::Label("RESTART RUN"), ImVec2(-1.0f, S(40.0f))))
                        context.restartSession();
                    if (context.returnToLobby && UIWidgets::GhostButton(NextDayz::L10n::Label("RETURN TO LOBBY"), ImVec2(-1.0f, S(40.0f)), false, true))
                        context.returnToLobby();

                    ImGui::TableNextColumn();
                    UIWidgets::DrawSectionHeader(NextDayz::L10n::Tr("Controls"), nullptr, Colors::AccentAmber);
                    const auto keyRow = [](const char* keys, const char* desc)
                    {
                        ImGui::PushFont(BoldFont(), S(16.0f));
                        ImGui::TextColored(ColorVec4(Colors::AccentAmber), "%s", keys);
                        ImGui::PopFont();
                        ImGui::SameLine(S(165.0f));
                        ImGui::TextColored(ColorVec4(Colors::TextSecondary), "%s", NextDayz::L10n::Tr(desc));
                    };
                    keyRow("WASD / Shift / C", "Move / Sprint / Crouch");
                    keyRow("LMB / RMB / R", "Fire / Aim / Reload");
                    keyRow("E / F / B", "Interact / Fill / Extract");
                    keyRow("TAB / M / H", "Inventory / Map / Bandage");
                    keyRow("1 2 3 / 4-7", "Weapons / Quickbar");
                    keyRow("V / Alt", "View / Free look");
                    ImGui::EndTable();
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
        }
    }

    // ========================================================================
    // MAIN DRAW ENTRY POINT
    // ========================================================================
    std::string AgentTargets()
    {
        std::string out;
        for (const auto& [key, target] : AgentTargetsShown)
        {
            const float x = (target.min.x + target.max.x) * 0.5f / std::max(AgentDisplay.x, 1.0f);
            const float y = (target.min.y + target.max.y) * 0.5f / std::max(AgentDisplay.y, 1.0f);
            out += fmt::format("{}={:.5f},{:.5f},{};", key, x, y, target.visible ? 1 : 0);
        }
        return out;
    }

    void RevealAgentTarget(std::string key) { AgentReveal = std::move(key); }

    void Draw(const FHudContext& context)
    {
        AgentTargetsDrawing.clear();
        DrawFrame(context);
        AgentTargetsShown = std::move(AgentTargetsDrawing);
        AgentDisplay = ImGui::GetIO().DisplaySize;
    }

    void DrawFrame(const FHudContext& context)
    {
        if (!context.sessionLabel.empty())
        {
            const auto* viewport = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const ImVec2 sz = ImGui::CalcTextSize(context.sessionLabel.c_str());
            const ImVec2 a(viewport->WorkPos.x + 20.0f, viewport->WorkPos.y + 18.0f);
            const ImVec2 b(a.x + sz.x + 18.0f, a.y + sz.y + 8.0f);
            draw->AddRectFilled(a, b, IM_COL32(14, 17, 15, 175), 2.0f);
            draw->AddRectFilled(a, ImVec2(a.x + 3.0f, b.y), UITheme::Colors::AccentAmber);
            draw->AddText(ImVec2(a.x + 10.0f, a.y + 4.0f), UITheme::Colors::TextSecondary, context.sessionLabel.c_str());
        }
        if (context.showInventory)
        {
            DrawInventoryPanel(context);
            DrawSessionOverlay(context);
            return;
        }
        DrawAdsVignette(context);
        DrawScope(context);
        DrawDamageFeedback(context);
        if (context.hasWeapon && !context.showMap && !context.showInventory)
        {
            DrawCrosshair(context);
        }
        DrawClock(context);
        DrawCompass(context);
        DrawObjective(context);
        DrawWeaponReadout(context);
        DrawSurvival(context);
        DrawQuickbar(context);
        DrawNotifications(context);
        DrawInteractionPrompt(context);
        DrawMap(context);
        DrawSessionOverlay(context);
    }
}
