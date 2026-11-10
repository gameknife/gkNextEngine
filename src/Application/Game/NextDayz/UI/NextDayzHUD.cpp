#include "Engine/Common/CoreMinimal.hpp"
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

        std::string FitText(std::string text, float width)
        {
            if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
            while (!text.empty() && ImGui::CalcTextSize((text + "…").c_str()).x > width)
            {
                size_t last = text.size() - 1;
                while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80) --last;
                text.resize(last);
            }
            return text + "…";
        }

        bool DrawItemIcon(const FHudContext& context, std::string_view id, ImVec2 min, ImVec2 max, bool rotated = false)
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
            ImGui::GetWindowDrawList()->AddImageQuad(static_cast<ImTextureID>(texture), a, ImVec2(b.x, a.y), b,
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
        // CLOCK, COMPASS & OBJECTIVE
        // ====================================================================
        void DrawClock(const FHudContext& context)
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 20.0f, vp->Pos.y + 20.0f), ImGuiCond_Always,
                                    ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.65f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, UITheme::Colors::SurfaceBg);
            ImGui::PushStyleColor(ImGuiCol_Border, UITheme::Colors::BorderMuted);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));

            ImGui::Begin("##nd_clock", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "%02d:%02d", context.hour, context.minute);
            ImGui::SameLine();
            const bool clear = !context.overcast;
            ImGui::TextColored(clear ? ImVec4(1.0f, 0.82f, 0.3f, 1.0f) : ImVec4(0.65f, 0.72f, 0.8f, 1.0f), "• %s",
                               context.weather.empty() ? (clear ? NextDayz::L10n::Tr("Clear") : NextDayz::L10n::Tr("Overcast")) : context.weather.c_str());
            ImGui::End();

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }

        void DrawObjective(const FHudContext& context)
        {
            if (context.objective.empty())
            {
                return;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float top = context.hasCompass ? 56.0f : 20.0f;
            ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->Pos.y + top), ImGuiCond_Always,
                                    ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.70f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, UITheme::Colors::SurfaceBg);
            ImGui::PushStyleColor(ImGuiCol_Border, UITheme::Colors::BorderMuted);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 5.0f));

            ImGui::Begin("##nd_objective", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), "%s", NextDayz::L10n::Tr("GOAL"));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.92f, 0.92f, 0.92f, 1.0f), "%s", context.objective.c_str());
            ImGui::End();

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }

        void DrawCompass(const FHudContext& context)
        {
            if (!context.hasCompass)
            {
                return;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const float width = std::min(520.0f, viewport->Size.x * 0.42f);
            const ImVec2 center(viewport->GetCenter().x, viewport->Pos.y + 28.0f);
            const float pxPerDegree = width / 120.0f;

            // Background tactical plate
            draw->AddRectFilled(ImVec2(center.x - width / 2 - 10, center.y - 16),
                                ImVec2(center.x + width / 2 + 10, center.y + 16),
                                UITheme::Colors::SurfaceBg, 4.0f);
            draw->AddRect(ImVec2(center.x - width / 2 - 10, center.y - 16),
                          ImVec2(center.x + width / 2 + 10, center.y + 16),
                          UITheme::Colors::BorderMuted, 4.0f);

            const float heading = BearingDegrees(context.yawRadians);
            for (int tick = 0; tick < 360; tick += 15)
            {
                float delta = static_cast<float>(tick) - heading;
                delta = std::remainder(delta, 360.0f);
                if (std::abs(delta) > 60.0f)
                {
                    continue;
                }
                const float x = center.x + delta * pxPerDegree;
                const float fade = 1.0f - std::abs(delta) / 60.0f;
                const ImU32 color = IM_COL32(230, 228, 214, static_cast<int>(220.0f * fade));
                const char* label = tick == 0 ? NextDayz::L10n::Tr("N") : tick == 90 ? NextDayz::L10n::Tr("E") : tick == 180 ? NextDayz::L10n::Tr("S") : tick == 270 ? NextDayz::L10n::Tr("W") : nullptr;
                if (label)
                {
                    draw->AddText(ImVec2(x - 4.0f, center.y - 9.0f),
                                  tick == 0 ? IM_COL32(255, 110, 90, static_cast<int>(255.0f * fade)) : color, label);
                }
                else
                {
                    draw->AddLine(ImVec2(x, center.y - (tick % 45 == 0 ? 8.0f : 4.0f)),
                                  ImVec2(x, center.y + 4.0f), color, 1.5f);
                }
            }
            if (context.cacheMarker)
            {
                const glm::vec3 delta3 = *context.cacheMarker - context.playerPosition;
                const float bearing = glm::degrees(std::atan2(delta3.x, -delta3.z));
                const float delta = std::clamp(std::remainder(bearing - heading, 360.0f), -60.0f, 60.0f);
                const float x = center.x + delta * pxPerDegree;
                draw->AddTriangleFilled(ImVec2(x - 6, center.y + 16), ImVec2(x + 6, center.y + 16), ImVec2(x, center.y + 8),
                                        IM_COL32(255, 70, 50, 240));
                char text[128];
                std::snprintf(text, sizeof(text), "%.0fm", glm::length(glm::vec2(delta3.x, delta3.z)));
                draw->AddText(ImVec2(x - 14.0f, center.y + 17.0f), IM_COL32(255, 120, 100, 230), text);
            }
            // Center needle
            draw->AddLine(ImVec2(center.x, center.y - 16), ImVec2(center.x, center.y - 9),
                          UITheme::Colors::AccentAmber, 2.0f);
            char degrees[16];
            std::snprintf(degrees, sizeof(degrees), "%03.0f", heading);
            draw->AddText(ImVec2(center.x - 11.0f, center.y + 18.0f), UITheme::Colors::TextSecondary, degrees);
        }

        // ====================================================================
        // OBJECT INTERACTION PROMPT & ACTION RADIAL PROGRESS
        // ====================================================================
        void DrawInteractionPrompt(const FHudContext& context)
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();

            // 1. Ongoing Action Radial Progress (e.g. Bandaging, Drinking, Eating)
            if (context.actionActive)
            {
                const ImVec2 center(vp->GetCenter().x, vp->GetCenter().y + 50.0f);
                const float radius = 38.0f;
                const float thickness = 4.0f;

                UIWidgets::DrawRadialProgress(draw, center, radius, thickness, context.actionProgress,
                                              UITheme::Colors::AccentAmber, IM_COL32(20, 26, 35, 200));

                // Action verb text in center
                const std::string& action = context.actionLabel.empty() ? NextDayz::L10n::Tr("INTERACTING") : context.actionLabel;
                const ImVec2 textSize = ImGui::CalcTextSize(action.c_str());
                draw->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                              UITheme::Colors::TextPrimary, action.c_str());

                // Percentage indicator below
                char pctText[16];
                std::snprintf(pctText, sizeof(pctText), "%d%%", static_cast<int>(context.actionProgress * 100.0f));
                const ImVec2 pctSize = ImGui::CalcTextSize(pctText);
                draw->AddText(ImVec2(center.x - pctSize.x * 0.5f, center.y + radius + 8.0f),
                              UITheme::Colors::TextHighlight, pctText);
                return;
            }

            if (context.interactionPrompt.empty())
            {
                return;
            }

            // 2. Tactical Interaction Pill
            // Positioned smoothly below center
            const ImVec2 center(vp->GetCenter().x, vp->Pos.y + vp->Size.y * 0.64f);

            // Check if prompt has multiple choices (e.g. well: "[E] Drink   [F] Fill bottle")
            const std::string& prompt = context.interactionPrompt;
            if (prompt.find("[E]") != std::string::npos && prompt.find("[F]") != std::string::npos)
            {
                const float pillH = 34.0f;
                const float gap = 16.0f;
                const float w1 = 150.0f;
                const float w2 = 180.0f;
                const float totalW = w1 + w2 + gap;
                const float startX = center.x - totalW * 0.5f;

                // Pill 1: [E] Drink
                const ImVec2 p1Min(startX, center.y - pillH * 0.5f);
                const ImVec2 p1Max(p1Min.x + w1, p1Min.y + pillH);
                draw->AddRectFilled(p1Min, p1Max, UITheme::Colors::SurfaceBg, 4.0f);
                draw->AddRect(p1Min, p1Max, UITheme::Colors::BorderMuted, 4.0f);
                UIWidgets::DrawKeyBadge(draw, ImVec2(p1Min.x + 8.0f, p1Min.y + 5.0f), "E", 24.0f);
                draw->AddText(ImVec2(p1Min.x + 40.0f, p1Min.y + 8.0f), UITheme::Colors::TextPrimary, NextDayz::L10n::Tr("DRINK"));

                // Pill 2: [F] Fill Bottle
                const ImVec2 p2Min(p1Max.x + gap, center.y - pillH * 0.5f);
                const ImVec2 p2Max(p2Min.x + w2, p2Min.y + pillH);
                draw->AddRectFilled(p2Min, p2Max, UITheme::Colors::SurfaceBg, 4.0f);
                draw->AddRect(p2Min, p2Max, UITheme::Colors::BorderMuted, 4.0f);
                UIWidgets::DrawKeyBadge(draw, ImVec2(p2Min.x + 8.0f, p2Min.y + 5.0f), "F", 24.0f);
                draw->AddText(ImVec2(p2Min.x + 40.0f, p2Min.y + 8.0f), UITheme::Colors::AccentCyan, NextDayz::L10n::Tr("FILL BOTTLE"));
                return;
            }

            // Single action pill: e.g. "[E] Take AK-74"
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
            const ImVec2 actionSize = ImGui::CalcTextSize(action.c_str());
            const ImVec2 targetSize = ImGui::CalcTextSize(target.c_str());
            const float pillH = 36.0f;
            const float paddingX = 14.0f;
            const float pillW = 28.0f + 10.0f + actionSize.x + 12.0f + targetSize.x + paddingX * 2.0f;

            const ImVec2 min(center.x - pillW * 0.5f, center.y - pillH * 0.5f);
            const ImVec2 max(min.x + pillW, min.y + pillH);

            // Modern frosted card background
            draw->AddRectFilled(min, max, UITheme::Colors::SurfaceBg, 4.0f);
            draw->AddRect(min, max, UITheme::Colors::BorderMuted, 4.0f);

            // Left key badge
            UIWidgets::DrawKeyBadge(draw, ImVec2(min.x + 8.0f, min.y + 6.0f), key.c_str(), 24.0f);

            // Action verb
            const float actionX = min.x + 40.0f;
            draw->AddText(ImVec2(actionX, min.y + 9.0f), UITheme::Colors::AccentAmber, action.c_str());

            // Target item with subtle category indicator if known
            const float targetX = actionX + actionSize.x + 12.0f;
            draw->AddText(ImVec2(targetX, min.y + 9.0f), UITheme::Colors::TextPrimary, target.c_str());
        }

        // ====================================================================
        // SURVIVAL VITALS (HEALTH, HUNGER, HYDRATION & DEBUFFS)
        // ====================================================================
        void DrawSurvival(const FHudContext& context)
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();

            const float panelW = 230.0f;
            const float panelH = 92.0f;
            const ImVec2 panelPos(vp->Pos.x + 20.0f, vp->Pos.y + vp->Size.y - panelH - 20.0f);

            // Tactical surface plate
            draw->AddRectFilled(panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH),
                                UITheme::Colors::SurfaceBg, 4.0f);
            draw->AddRect(panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH),
                          UITheme::Colors::BorderMuted, 4.0f);

            const auto renderBar = [&](const char* label, float value, float yOffset, ImU32 color, bool pulse = false)
            {
                const ImVec2 textPos(panelPos.x + 12.0f, panelPos.y + yOffset);
                draw->AddText(textPos, UITheme::Colors::TextSecondary, label);

                const ImVec2 barMin(panelPos.x + 68.0f, panelPos.y + yOffset + 2.0f);
                const ImVec2 barMax(panelPos.x + panelW - 14.0f, barMin.y + 10.0f);
                UIWidgets::DrawTacticalBar(draw, barMin, barMax, value / 100.0f, color,
                                           IM_COL32(24, 30, 40, 200), pulse);
            };

            const FSurvivalSnapshot& s = context.survival;
            const bool lowHealth = s.health < 25.0f;
            renderBar(NextDayz::L10n::Tr("HEALTH"), s.health, 12.0f, UITheme::Colors::AccentRose, lowHealth);
            renderBar(NextDayz::L10n::Tr("HUNGER"), s.hunger, 38.0f, UITheme::Colors::AccentAmber, s.hunger < 20.0f);
            renderBar(NextDayz::L10n::Tr("THIRST"), s.hydration, 64.0f, UITheme::Colors::AccentCyan, s.hydration < 20.0f);

            // Status badges floating above vitals panel
            float badgeY = panelPos.y - 28.0f;
            const float badgeX = panelPos.x;

            if (s.bleedingWounds > 0)
            {
                char text[128];
                std::snprintf(text, sizeof(text), NextDayz::L10n::Tr("BLEEDING x%d  [H]"), s.bleedingWounds);
                const ImVec2 size = UIWidgets::DrawStatusBadge(draw, ImVec2(badgeX, badgeY), "🩸", text,
                                                               UITheme::Colors::SlotInvalidDropBg,
                                                               UITheme::Colors::AccentRose,
                                                               UITheme::Colors::TextPrimary);
                badgeY -= size.y + 6.0f;
            }

            if (s.cold > 0.25f)
            {
                const char* coldText = s.cold > 0.55f ? NextDayz::L10n::Tr("FREEZING") : NextDayz::L10n::Tr("COLD");
                const ImVec2 size = UIWidgets::DrawStatusBadge(draw, ImVec2(badgeX, badgeY), "❄️", coldText,
                                                               IM_COL32(30, 58, 90, 200),
                                                               UITheme::Colors::AccentIceBlue,
                                                               UITheme::Colors::AccentIceBlue);
                badgeY -= size.y + 6.0f;
            }

            if (s.healing && s.bleedingWounds == 0)
            {
                const char* healText = s.painkillerSeconds > 0.0f ? NextDayz::L10n::Tr("PAINKILLERS") : NextDayz::L10n::Tr("HEALING");
                const ImVec2 size = UIWidgets::DrawStatusBadge(draw, ImVec2(badgeX, badgeY), "💊", healText,
                                                               UITheme::Colors::SlotValidDropBg,
                                                               UITheme::Colors::AccentEmerald,
                                                               UITheme::Colors::AccentEmerald);
                badgeY -= size.y + 6.0f;
            }

            if (context.weak)
            {
                const ImVec2 size = UIWidgets::DrawStatusBadge(draw, ImVec2(badgeX, badgeY), "⚡", NextDayz::L10n::Tr("WEAK - NO SPRINT"),
                                                               IM_COL32(70, 55, 20, 200),
                                                               UITheme::Colors::AccentAmber,
                                                               UITheme::Colors::AccentAmber);
                badgeY -= size.y + 6.0f;
            }

            if (context.lanternLit)
            {
                char lanternText[128];
                std::snprintf(lanternText, sizeof(lanternText), NextDayz::L10n::Tr("LANTERN %.0fm"), context.lanternMinutes);
                UIWidgets::DrawStatusBadge(draw, ImVec2(badgeX, badgeY), "💡", lanternText,
                                           IM_COL32(60, 50, 25, 200),
                                           UITheme::Colors::AccentAmber,
                                           UITheme::Colors::TextPrimary);
            }
        }

        // ====================================================================
        // WEAPON READOUT & BOTTOM QUICKBAR
        // ====================================================================
        void DrawWeaponReadout(const FHudContext& context)
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();

            const float cardW = 200.0f;
            const float cardH = 68.0f;
            const ImVec2 cardPos(vp->Pos.x + vp->Size.x - cardW - 20.0f, vp->Pos.y + vp->Size.y - cardH - 20.0f);

            draw->AddRectFilled(cardPos, ImVec2(cardPos.x + cardW, cardPos.y + cardH),
                                UITheme::Colors::SurfaceBg, 4.0f);
            draw->AddRect(cardPos, ImVec2(cardPos.x + cardW, cardPos.y + cardH),
                          UITheme::Colors::BorderMuted, 4.0f);

            if (!context.hasWeapon)
            {
                draw->AddText(ImVec2(cardPos.x + 14.0f, cardPos.y + 14.0f), UITheme::Colors::TextMuted, NextDayz::L10n::Tr("UNARMED"));
                draw->AddText(ImVec2(cardPos.x + 14.0f, cardPos.y + 36.0f), UITheme::Colors::TextMuted,
                              context.firstPerson ? NextDayz::L10n::Tr("[FPS]") : NextDayz::L10n::Tr("[TPS]"));
                return;
            }

            // Weapon Title
            draw->AddText(ImVec2(cardPos.x + 14.0f, cardPos.y + 10.0f), UITheme::Colors::TextPrimary,
                          context.weaponName.c_str());

            if (context.reloading)
            {
                draw->AddText(ImVec2(cardPos.x + 14.0f, cardPos.y + 32.0f), UITheme::Colors::AccentAmber, NextDayz::L10n::Tr("RELOADING..."));
                const ImVec2 barMin(cardPos.x + 14.0f, cardPos.y + 52.0f);
                const ImVec2 barMax(cardPos.x + cardW - 14.0f, barMin.y + 4.0f);
                const float t = static_cast<float>(ImGui::GetTime());
                const float wave = 0.5f + 0.5f * std::sin(t * 8.0f);
                UIWidgets::DrawTacticalBar(draw, barMin, barMax, wave, UITheme::Colors::AccentAmber);
            }
            else if (context.melee)
            {
                draw->AddText(ImVec2(cardPos.x + 14.0f, cardPos.y + 30.0f), UITheme::Colors::TextSecondary,
                              context.swinging ? NextDayz::L10n::Tr("STRIKING...") : context.aiming ? NextDayz::L10n::Tr("GUARD READY") : NextDayz::L10n::Tr("RMB  READY"));
                draw->AddText(ImVec2(cardPos.x + 14.0f, cardPos.y + 47.0f), UITheme::Colors::TextMuted, NextDayz::L10n::Tr("LMB  STRIKE"));
            }
            else
            {
                // Ammo readout: Large magazine count, small reserve
                char magText[16];
                std::snprintf(magText, sizeof(magText), "%d", context.ammoInMag);
                char reserveText[16];
                std::snprintf(reserveText, sizeof(reserveText), "/ %d", context.ammoReserve);

                const ImU32 magColor = context.ammoInMag == 0 ? UITheme::Colors::AccentRose : UITheme::Colors::TextPrimary;
                draw->AddText(ImVec2(cardPos.x + 14.0f, cardPos.y + 30.0f), magColor, magText);

                const ImVec2 magSize = ImGui::CalcTextSize(magText);
                draw->AddText(ImVec2(cardPos.x + 16.0f + magSize.x, cardPos.y + 33.0f),
                              UITheme::Colors::TextSecondary, reserveText);
            }

            // Slot badge on right
            char slotText[128];
            std::snprintf(slotText, sizeof(slotText), NextDayz::L10n::Tr("SLOT %d  %s"), context.activeSlot + 1,
                          context.firstPerson ? NextDayz::L10n::Tr("FPS") : NextDayz::L10n::Tr("TPS"));
            const ImVec2 slotSize = ImGui::CalcTextSize(slotText);
            draw->AddText(ImVec2(cardPos.x + cardW - slotSize.x - 12.0f, cardPos.y + 42.0f),
                          UITheme::Colors::TextMuted, slotText);
        }

        void DrawQuickbar(const FHudContext& context)
        {
            if (context.inventory == nullptr)
            {
                return;
            }
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();

            const float size = 52.0f;
            const float gap = 8.0f;
            const float total = 4.0f * size + 3.0f * gap;
            const ImVec2 origin(vp->GetCenter().x - total * 0.5f, vp->Pos.y + vp->Size.y - size - 20.0f);

            for (int slot = 0; slot < 4; ++slot)
            {
                const ImVec2 a(origin.x + slot * (size + gap), origin.y);
                const ImVec2 b(a.x + size, a.y + size);
                const std::string& itemId = context.quickSlots[slot];
                const int count = itemId.empty() ? 0 : context.inventory->CountOf(itemId);
                const bool occupied = !itemId.empty();

                UIWidgets::DrawSlotFrame(draw, a, b, occupied, false, false, false, 4.0f);

                // Slot Key badge (4..7)
                char key[4];
                std::snprintf(key, sizeof(key), "%d", slot + 4);
                draw->AddText(ImVec2(a.x + 5.0f, a.y + 3.0f), UITheme::Colors::TextMuted, key);

                if (occupied)
                {
                    const std::string name = FitText(ItemName(itemId), size - 10.0f);
                    draw->AddText(ImVec2(a.x + 5.0f, a.y + 18.0f),
                                  count > 0 ? UITheme::Colors::TextPrimary : UITheme::Colors::TextMuted,
                                  name.c_str());

                    char amount[16];
                    std::snprintf(amount, sizeof(amount), "x%d", count);
                    const ImVec2 amtSize = ImGui::CalcTextSize(amount);
                    draw->AddText(ImVec2(b.x - amtSize.x - 4.0f, b.y - amtSize.y - 2.0f),
                                  count > 0 ? UITheme::Colors::AccentAmber : UITheme::Colors::TextMuted,
                                  amount);
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
            const float side = std::min(vp->Size.x, vp->Size.y) * 0.82f;
            ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(side, side + 40.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.96f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, UITheme::Colors::SurfaceBg);
            ImGui::PushStyleColor(ImGuiCol_Border, UITheme::Colors::BorderMuted);

            ImGui::Begin(NextDayz::L10n::Label("Riverland Tactical Map##nd_map"), nullptr,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings);
            if (!context.hasMap)
            {
                ImGui::TextWrapped("%s", NextDayz::L10n::Tr("You have no map. Look for one in shops, police stations and military sites."));
                ImGui::End();
                ImGui::PopStyleColor(2);
                return;
            }
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float mapSide = side - 20.0f;
            const ImVec2 end(origin.x + mapSide, origin.y + mapSide);
            draw->AddRectFilled(origin, end, IM_COL32(196, 188, 158, 255));

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
            ImGui::PopStyleColor(2);
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
            if (installed != EWeaponAttachment::None && context.removeAttachment && ImGui::MenuItem(L10n::Label("Remove attachment")))
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
                    if (ImGui::MenuItem(L10n::Label("Merge matching stacks"), nullptr, false,
                                       hasMatching && instance.count < def->maxStack))
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
                if (ImGui::MenuItem(L10n::Label("Rotate"), nullptr, false,
                    context.inventory->CanPlace(instance.instanceId, instance.containerId, rotated)) && context.placeItem)
                    context.placeItem(instance.instanceId, instance.containerId, rotated);
                if ((def->kind == EItemKind::Consumable || def->use != EItemUse::None) && context.useItem &&
                    ImGui::MenuItem(NextDayz::L10n::Label("Use")))
                {
                    context.useItem(instance.instanceId);
                }
                if (def->kind == EItemKind::Clothing && !inEquipmentSlot && context.wear && ImGui::MenuItem(NextDayz::L10n::Label("Wear")))
                {
                    context.wear(instance.instanceId);
                }
                if (inEquipmentSlot && context.takeOff && ImGui::MenuItem(NextDayz::L10n::Label("Take off")))
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
                if (instance.count > 1 && context.splitItem && ImGui::MenuItem(NextDayz::L10n::Label("Split stack")))
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
                    if (instance.count > 1 && ImGui::MenuItem(NextDayz::L10n::Label("Drop One")))
                    {
                        context.dropItem(instance.instanceId, 1);
                    }
                    if (ImGui::MenuItem(instance.count > 1 ? NextDayz::L10n::Tr("Drop All") : NextDayz::L10n::Tr("Drop")))
                    {
                        context.dropItem(instance.instanceId, -1);
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
            return clicked;
        }

        // Equipment Slot Card in the Center Paperdoll Column
        void DrawEquipmentSlot(const FHudContext& context, const char* label, EEquipSlot slot)
        {
            const Inventory& inventory = *context.inventory;
            const FItemInstanceId worn = inventory.WornInSlot(slot);
            const FItemInstance* instance = worn ? inventory.FindInstance(worn) : nullptr;
            const FItemDef* def = instance ? FindItemDef(instance->defId) : nullptr;

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float slotW = ImGui::GetContentRegionAvail().x;
            const float slotH = 48.0f;
            const ImVec2 max(pos.x + slotW, pos.y + slotH);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            ImGui::PushID(label);
            ImGui::InvisibleButton("##eq_btn", ImVec2(slotW, slotH));
            const bool hovered = ImGui::IsItemHovered();

            // Background slot frame & corner ticks
            UIWidgets::DrawSlotFrame(draw, pos, max, instance != nullptr, hovered, false, false, 3.0f);
            UIWidgets::DrawCornerTicks(draw, pos, max, 6.0f, hovered ? UITheme::Colors::AccentAmber : UITheme::Colors::BorderMuted);

            const char* slotType = (slot == EEquipSlot::Head) ? "HEAD" :
                                   (slot == EEquipSlot::Torso) ? "TORSO" :
                                   (slot == EEquipSlot::Vest) ? "VEST" :
                                   (slot == EEquipSlot::Legs) ? "LEGS" :
                                   (slot == EEquipSlot::Back) ? "BACKPACK" : "GEAR";

            if (def && instance)
            {
                // Left tactical gear category stripe
                draw->AddRectFilled(pos, ImVec2(pos.x + 3.0f, max.y), UITheme::Colors::AccentViolet, 3.0f);

                DrawItemIcon(context, def->id, ImVec2(pos.x + 6.0f, pos.y + 4.0f), ImVec2(pos.x + 52.0f, max.y - 4.0f));

                // Slot title & Item name
                draw->AddText(ImVec2(pos.x + 58.0f, pos.y + 5.0f), UITheme::Colors::TextMuted, label);
                const auto name = FitText(std::string(L10n::Tr(def->displayName)), slotW - 60.0f - 110.0f);
                draw->AddText(ImVec2(pos.x + 58.0f, pos.y + 24.0f), UITheme::Colors::TextPrimary, name.c_str());

                // Attribute badge on right
                char statText[48] = "";
                if (def->armor > 0.0f)
                {
                    std::snprintf(statText, sizeof(statText), NextDayz::L10n::Tr("Armor +%.0f%%"), def->armor * 100.0f);
                }
                else if (def->warmth > 0.0f)
                {
                    std::snprintf(statText, sizeof(statText), NextDayz::L10n::Tr("Warmth +%.0f%%"), def->warmth * 100.0f);
                }
                else if (def->containerSize.Area() > 0)
                {
                    std::snprintf(statText, sizeof(statText), NextDayz::L10n::Tr("+%d Slots"), def->containerSize.Area());
                }

                if (statText[0] != '\0')
                {
                    const ImVec2 sSize = ImGui::CalcTextSize(statText);
                    const ImVec2 bMin(max.x - sSize.x - 16.0f, pos.y + 12.0f);
                    const ImVec2 bMax(max.x - 8.0f, pos.y + 34.0f);
                    draw->AddRectFilled(bMin, bMax, IM_COL32(16, 24, 34, 220), 2.0f);
                    draw->AddRect(bMin, bMax, UITheme::Colors::BorderMuted, 2.0f, 0, 1.0f);
                    draw->AddText(ImVec2(bMin.x + 4.0f, bMin.y + 3.0f), UITheme::Colors::AccentEmerald, statText);
                }

                if (hovered)
                {
                    ItemTooltip(context, *def, *instance);
                }

                // Drag Source
                if (ImGui::BeginDragDropSource())
                {
                    StartItemDrag(*instance, pos);
                    const FItemInstanceId id = instance->instanceId;
                    ImGui::SetDragDropPayload(kItemPayload, &id, sizeof(id));
                    ItemIconPreview(context, *def, InventoryDrag.rotated, 96.0f);
                    ImGui::TextUnformatted(std::string(NextDayz::L10n::Tr(def->displayName)).c_str());
                    ImGui::EndDragDropSource();
                }

                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && context.takeOff)
                {
                    context.takeOff(instance->instanceId);
                }
            }
            else
            {
                // Empty slot with tactical line silhouette
                UIWidgets::DrawSlotSilhouette(draw, ImVec2(pos.x + 6.0f, pos.y + 4.0f), ImVec2(pos.x + 52.0f, max.y - 4.0f), slotType);
                draw->AddText(ImVec2(pos.x + 58.0f, pos.y + 6.0f), UITheme::Colors::TextMuted, label);
                draw->AddText(ImVec2(pos.x + 58.0f, pos.y + 24.0f), IM_COL32(80, 95, 120, 180), NextDayz::L10n::Tr("[ EMPTY SLOT ]"));
            }

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
            ImGui::Spacing();
        }

        void DrawWeaponSlot(const FHudContext& context, const char* label, int slot)
        {
            const Inventory& inventory = *context.inventory;
            const FItemInstanceId id = context.weapons ? context.weapons->SlotInstance(slot) : 0;
            const FItemInstance* instance = id ? inventory.FindInstance(id) : nullptr;
            const FItemDef* def = instance ? FindItemDef(instance->defId) : nullptr;

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float slotW = ImGui::GetContentRegionAvail().x;
            const float slotH = instance ? 54.0f : 48.0f;
            const ImVec2 max(pos.x + slotW, pos.y + slotH);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            ImGui::PushID(slot + 2000);
            ImGui::InvisibleButton("##wp_slot_btn", ImVec2(instance ? slotW - 74.0f : slotW, slotH));
            const bool hovered = ImGui::IsItemHovered();

            UIWidgets::DrawSlotFrame(draw, pos, max, instance != nullptr, hovered, false, false, 3.0f);
            UIWidgets::DrawCornerTicks(draw, pos, max, 6.0f, hovered ? UITheme::Colors::AccentAmber : UITheme::Colors::BorderMuted);

            if (def && instance)
            {
                // Weapon orange category stripe
                draw->AddRectFilled(pos, ImVec2(pos.x + 3.0f, max.y), UITheme::Colors::AccentOrange, 3.0f);

                DrawItemIcon(context, def->id, ImVec2(pos.x + 6.0f, pos.y + 4.0f), ImVec2(pos.x + 110.0f, max.y - 4.0f));

                const float textOffset = 118.0f;
                draw->AddText(ImVec2(pos.x + textOffset, pos.y + 6.0f), UITheme::Colors::TextMuted, label);
                const auto name = FitText(std::string(L10n::Tr(def->displayName)), slotW - textOffset - 170.0f);
                draw->AddText(ImVec2(pos.x + textOffset, pos.y + 27.0f), UITheme::Colors::TextPrimary, name.c_str());

                // Ammo badge
                char ammoText[32];
                std::snprintf(ammoText, sizeof(ammoText), "[ %d ]", instance->loadedAmmo);
                const ImVec2 aSize = ImGui::CalcTextSize(ammoText);
                const ImVec2 aMin(max.x - aSize.x - 82.0f, pos.y + 15.0f);
                const ImVec2 aMax(max.x - 74.0f, pos.y + 37.0f);
                draw->AddRectFilled(aMin, aMax, IM_COL32(14, 22, 32, 230), 2.0f);
                draw->AddRect(aMin, aMax, UITheme::Colors::BorderMuted, 2.0f, 0, 1.0f);
                draw->AddText(ImVec2(aMin.x + 4.0f, aMin.y + 3.0f), UITheme::Colors::AccentCyan, ammoText);

                if (hovered)
                {
                    ItemTooltip(context, *def, *instance);
                }
            }
            else
            {
                // Empty weapon slot with silhouette
                UIWidgets::DrawSlotSilhouette(draw, ImVec2(pos.x + 8.0f, pos.y + 4.0f), ImVec2(pos.x + 80.0f, max.y - 4.0f),
                                              slot == 2 ? "SIDEARM" : "WEAPON");
                draw->AddText(ImVec2(pos.x + 90.0f, pos.y + 8.0f), UITheme::Colors::TextMuted, label);
                draw->AddText(ImVec2(pos.x + 90.0f, pos.y + 26.0f), IM_COL32(80, 95, 120, 180), NextDayz::L10n::Tr("[ NO WEAPON EQUIPPED ]"));
            }

            // Drag drop to equip weapon
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kItemPayload))
                {
                    const auto* dragged = inventory.FindInstance(*static_cast<const FItemInstanceId*>(payload->Data));
                    const auto* definition = dragged ? FindItemDef(dragged->defId) : nullptr;
                    if (instance && dragged && FindAttachmentDef(dragged->defId) && context.installAttachment)
                    {
                        context.installAttachment(instance->instanceId, dragged->instanceId);
                    }
                    else if (context.equipWeapon && definition && WeaponSystem::CanEquipInSlot(slot, definition->weaponDefId))
                    {
                        context.equipWeapon(*static_cast<const FItemInstanceId*>(payload->Data), slot);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Holster button & Tactical Rails
            if (instance)
            {
                ImGui::SetCursorScreenPos(ImVec2(max.x - 70.0f, pos.y + 14.0f));
                if (ImGui::Button(NextDayz::L10n::Label("Holster"), ImVec2(64.0f, 26.0f)) && context.unequipWeapon)
                {
                    context.unequipWeapon(slot);
                }
                ImGui::SetCursorScreenPos(ImVec2(pos.x, max.y + 4.0f));

                // INTEGRATED TACTICAL ATTACHMENT RAILS
                if (def && def->kind == EItemKind::Weapon)
                {
                    constexpr size_t kSlotCount = kAttachmentSlotCount;
                    const float railGap = 4.0f;
                    const float railW = std::floor((slotW - railGap * static_cast<float>(kSlotCount - 1)) / static_cast<float>(kSlotCount));
                    const float railH = 26.0f;

                    for (size_t index = 0; index < kSlotCount; ++index)
                    {
                        const auto kind = static_cast<EAttachmentSlot>(index);
                        const auto* part = FindAttachmentDef(instance->attachments[index]);
                        const ImVec2 rMin = ImGui::GetCursorScreenPos();
                        const ImVec2 rMax(rMin.x + railW, rMin.y + railH);

                        ImGui::PushID(static_cast<int>(index * 100 + slot));
                        ImGui::InvisibleButton("##rail_btn", ImVec2(railW, railH));
                        const bool rHovered = ImGui::IsItemHovered();

                        // Slot frame
                        draw->AddRectFilled(rMin, rMax, part ? IM_COL32(18, 28, 40, 230) : IM_COL32(14, 18, 24, 180), 2.0f);
                        draw->AddRect(rMin, rMax, part ? UITheme::Colors::AccentCyan :
                                      rHovered ? UITheme::Colors::AccentAmber : UITheme::Colors::BorderMuted, 2.0f, 0, 1.0f);

                        // Rail Slot Abbreviation Tag
                        const char* slotTag = (kind == EAttachmentSlot::Optic) ? "OPT" :
                                              (kind == EAttachmentSlot::Muzzle) ? "MUZ" :
                                              (kind == EAttachmentSlot::Magazine) ? "MAG" :
                                              (kind == EAttachmentSlot::Grip) ? "GRP" : "TAC";

                        if (part)
                        {
                            draw->AddRectFilled(rMin, ImVec2(rMin.x + 3.0f, rMax.y), UITheme::Colors::AccentCyan, 2.0f);
                            const auto pName = FitText(fmt::format("{}: {}", slotTag, L10n::Tr(part->displayName)), railW - 10.0f);
                            draw->AddText(ImVec2(rMin.x + 6.0f, rMin.y + 5.0f), UITheme::Colors::TextPrimary, pName.c_str());

                            if (rHovered)
                            {
                                ImGui::SetTooltip("%s: %s\n%s", std::string(L10n::Tr(AttachmentSlotName(kind))).c_str(),
                                                  std::string(L10n::Tr(part->displayName)).c_str(),
                                                  L10n::Tr("[RMB] Detach attachment"));
                            }

                            // Right-click to detach
                            if (ImGui::BeginPopupContextItem("##part_menu"))
                            {
                                if (ImGui::MenuItem(L10n::Label("Detach Attachment")) && context.removeAttachment)
                                {
                                    context.removeAttachment(instance->instanceId, kind);
                                }
                                ImGui::EndPopup();
                            }
                        }
                        else
                        {
                            char emptyText[32];
                            std::snprintf(emptyText, sizeof(emptyText), "+ %s", slotTag);
                            const ImVec2 eSize = ImGui::CalcTextSize(emptyText);
                            draw->AddText(ImVec2(rMin.x + (railW - eSize.x) * 0.5f, rMin.y + 5.0f),
                                          rHovered ? UITheme::Colors::AccentAmber : IM_COL32(90, 105, 125, 180), emptyText);

                            if (rHovered)
                            {
                                ImGui::SetTooltip("%s: %s", L10n::Tr(AttachmentSlotName(kind)),
                                                  L10n::Tr("Drag compatible attachment here"));
                            }

                            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                            {
                                ImGui::OpenPopup("parts_picker");
                            }
                        }

                        // Drag drop attachment on this rail
                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const auto* payload = ImGui::AcceptDragDropPayload(kItemPayload))
                            {
                                const auto* dragged = inventory.FindInstance(*static_cast<const FItemInstanceId*>(payload->Data));
                                const auto* accessory = dragged ? FindAttachmentDef(dragged->defId) : nullptr;
                                if (accessory && accessory->slot == kind && context.installAttachment)
                                {
                                    context.installAttachment(instance->instanceId, dragged->instanceId);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (ImGui::BeginPopup("parts_picker"))
                        {
                            DrawAttachmentOptions(context, *instance, kind);
                            ImGui::EndPopup();
                        }

                        ImGui::PopID();
                        if (index + 1 < kSlotCount)
                        {
                            ImGui::SameLine(0.0f, railGap);
                        }
                    }
                    ImGui::Spacing();
                }
            }

            ImGui::PopID();
            ImGui::Spacing();
        }

        void DrawCargoGrid(const FHudContext& context, const FInventoryContainer& container)
        {
            const auto& inventory = *context.inventory;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            constexpr float maxCellSize = 48.0f;
            const float cell = std::max(1.0f, std::floor(std::min(maxCellSize,
                ImGui::GetContentRegionAvail().x / static_cast<float>(container.size.width))));
            const ImVec2 gridSize(cell * container.size.width, cell * container.size.height);
            auto* draw = ImGui::GetWindowDrawList();
            ImGui::BeginGroup();
            for (int y = 0; y < container.size.height; ++y)
                for (int x = 0; x < container.size.width; ++x)
                {
                    const ImVec2 a(origin.x + x * cell + 1.0f, origin.y + y * cell + 1.0f);
                    const ImVec2 b(a.x + cell - 2.0f, a.y + cell - 2.0f);
                    UIWidgets::DrawSlotFrame(draw, a, b, false, false, false, false, 2.0f);
                    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
                    draw->AddLine(ImVec2(c.x - 2.0f, c.y), ImVec2(c.x + 2.0f, c.y), UITheme::Colors::GridCrossDot, 1.0f);
                    draw->AddLine(ImVec2(c.x, c.y - 2.0f), ImVec2(c.x, c.y + 2.0f), UITheme::Colors::GridCrossDot, 1.0f);
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
        // CHARACTER PREVIEW, VITALS & TACTICAL QUICKBAR
        // ====================================================================
        void DrawVitalsMonitor(const FHudContext& context, const ImVec2& pos, const ImVec2& size)
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddText(pos, UITheme::Colors::TextPrimary, L10n::Tr("VITALS"));
            const ImU32 healthCol = context.survival.health > 50.0f ? UITheme::Colors::AccentEmerald :
                                    context.survival.health > 25.0f ? UITheme::Colors::AccentAmber : UITheme::Colors::AccentRose;
            UIWidgets::DrawECGLine(draw, ImVec2(pos.x, pos.y + 24.0f), ImVec2(size.x, 22.0f),
                                   context.survival.health < 30.0f ? 110.0f : 72.0f, healthCol);

            const float rowY = pos.y + 58.0f;
            const float rowStep = std::clamp((size.y - 110.0f) / 5.0f, 36.0f, 60.0f);
            char hpText[32];
            std::snprintf(hpText, sizeof(hpText), "%.0f%%", context.survival.health);
            UIWidgets::DrawVitalsGauge(draw, ImVec2(pos.x, rowY), size.x, L10n::Tr("HEALTH"),
                                       context.survival.health / 100.0f, healthCol, hpText, context.survival.health < 25.0f);

            char energyText[32];
            std::snprintf(energyText, sizeof(energyText), "%.0f%%", context.survival.hunger);
            UIWidgets::DrawVitalsGauge(draw, ImVec2(pos.x, rowY + rowStep), size.x,
                                       L10n::Tr("ENERGY"), context.survival.hunger / 100.0f,
                                       UITheme::Colors::AccentAmber, energyText);

            char hydroText[32];
            std::snprintf(hydroText, sizeof(hydroText), "%.0f%%", context.survival.hydration);
            UIWidgets::DrawVitalsGauge(draw, ImVec2(pos.x, rowY + rowStep * 2.0f), size.x,
                                       L10n::Tr("HYDRATION"), context.survival.hydration / 100.0f,
                                       UITheme::Colors::AccentCyan, hydroText);

            const float warm01 = std::clamp(1.0f - context.survival.cold, 0.0f, 1.0f);
            UIWidgets::DrawVitalsGauge(draw, ImVec2(pos.x, rowY + rowStep * 3.0f), size.x,
                                       L10n::Tr("TEMP"), warm01,
                                       context.survival.cold > 0.5f ? UITheme::Colors::AccentIceBlue : IM_COL32(45, 212, 191, 255),
                                       L10n::Tr(context.survival.cold > 0.5f ? "COLD" : "WARM"));

            char armorText[32];
            std::snprintf(armorText, sizeof(armorText), "%.0f%%", context.armor * 100.0f);
            UIWidgets::DrawVitalsGauge(draw, ImVec2(pos.x, rowY + rowStep * 4.0f), size.x,
                                       L10n::Tr("ARMOR"), context.armor,
                                       IM_COL32(129, 140, 248, 255), armorText);

            // Separate status rows keep simultaneous bleeding and healing clear of the gauges.
            float statusY = rowY + rowStep * 5.0f;
            if (context.survival.bleedingWounds > 0)
            {
                const auto label = L10n::Format("BLEEDING x{}", context.survival.bleedingWounds);
                const ImVec2 badge = UIWidgets::DrawStatusBadge(draw, ImVec2(pos.x, statusY), "!",
                    label.c_str(), IM_COL32(60, 10, 15, 230), UITheme::Colors::AccentRose, UITheme::Colors::AccentRose);
                statusY += badge.y + 4.0f;
            }
            if (context.survival.healing)
            {
                UIWidgets::DrawStatusBadge(draw, ImVec2(pos.x, statusY), "+", L10n::Tr("RECOVERING"),
                    IM_COL32(10, 50, 30, 230), UITheme::Colors::AccentEmerald, UITheme::Colors::AccentEmerald);
            }
        }

        void DrawCharacterPreview(const FHudContext& context, float height)
        {
            static float yaw = 0.35f;
            static float distance = 3.4f;

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
            const ImVec2 max(pos.x + width, pos.y + height);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(pos, max, UITheme::Colors::SurfaceCard, 3.0f);
            draw->AddRect(pos, max, UITheme::Colors::BorderMuted, 3.0f, 0, 1.0f);
            UIWidgets::DrawCornerTicks(draw, pos, max, 10.0f, UITheme::Colors::BorderTactical);
            draw->AddText(ImVec2(pos.x + 12.0f, pos.y + 10.0f), UITheme::Colors::TextSecondary, L10n::Tr("CURRENT LOADOUT"));

            const float vitalsWidth = std::clamp(width * 0.30f, 150.0f, 220.0f);
            const float dividerX = max.x - vitalsWidth - 28.0f;
            const ImVec2 previewPos(pos.x + 8.0f, pos.y + 34.0f);
            const ImVec2 previewSize(std::max(1.0f, dividerX - previewPos.x - 10.0f), height - 110.0f);
            const ImVec2 previewMax(previewPos.x + previewSize.x, previewPos.y + previewSize.y);
            draw->AddLine(ImVec2(dividerX, previewPos.y), ImVec2(dividerX, max.y - 48.0f), UITheme::Colors::BorderMuted);

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
                draw->AddText(ImVec2(previewPos.x + 8.0f, previewPos.y + previewSize.y * 0.45f),
                              UITheme::Colors::TextMuted, L10n::Tr("Preparing character preview..."));

            ImGui::SetCursorScreenPos(ImVec2(previewPos.x + 4.0f, previewMax.y + 6.0f));
            if (ImGui::SmallButton(L10n::Label("Front"))) yaw = 0.0f;
            ImGui::SameLine();
            if (ImGui::SmallButton(L10n::Label("Back"))) yaw = glm::pi<float>();
            ImGui::SameLine();
            if (ImGui::SmallButton(L10n::Label("Reset")))
            {
                yaw = 0.35f;
                distance = 3.4f;
            }

            DrawVitalsMonitor(context, ImVec2(dividerX + 14.0f, previewPos.y), ImVec2(vitalsWidth, height - 92.0f));

            // Field tools share the character panel's bottom edge.
            const float toolY = max.y - 34.0f;
            draw->AddLine(ImVec2(pos.x + 10.0f, toolY - 8.0f), ImVec2(max.x - 10.0f, toolY - 8.0f), UITheme::Colors::BorderMuted);
            const float chipW = (width - 20.0f - 18.0f) / 4.0f;
            const auto drawToolChip = [&](float x, const char* name, bool owned)
            {
                const ImVec2 tPos(x, toolY);
                const ImVec2 tSize(chipW, 22.0f);
                const ImVec2 tMax(tPos.x + tSize.x, tPos.y + tSize.y);
                draw->AddRectFilled(tPos, tMax, owned ? IM_COL32(20, 32, 44, 230) : IM_COL32(15, 18, 22, 160), 2.0f);
                draw->AddRect(tPos, tMax, owned ? UITheme::Colors::AccentCyan : UITheme::Colors::BorderMuted, 2.0f, 0, 1.0f);
                const ImVec2 txtSize = ImGui::CalcTextSize(name);
                draw->AddText(ImVec2(tPos.x + (tSize.x - txtSize.x) * 0.5f, tPos.y + 4.0f),
                              owned ? UITheme::Colors::TextPrimary : IM_COL32(90, 100, 115, 160), name);
            };

            drawToolChip(pos.x + 10.0f + (chipW + 6.0f) * 0.0f, L10n::Tr("COMPASS"), context.inventory->HasTool(EItemTool::Compass));
            drawToolChip(pos.x + 10.0f + (chipW + 6.0f) * 1.0f, L10n::Tr("MAP"), context.inventory->HasTool(EItemTool::Map));
            drawToolChip(pos.x + 10.0f + (chipW + 6.0f) * 2.0f, L10n::Tr("OPENER"), context.inventory->CanOpenCans());
            drawToolChip(pos.x + 10.0f + (chipW + 6.0f) * 3.0f, L10n::Tr("LANTERN"), context.lanternLit || context.lanternMinutes > 0.0f);

            ImGui::SetCursorScreenPos(ImVec2(pos.x, max.y + 6.0f));
            ImGui::Dummy(ImVec2(width, 0.0f));
        }

        void DrawTacticalQuickbar(const FHudContext& context)
        {
            if (context.inventory == nullptr) return;

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const float slotH = 50.0f;
            constexpr int kSlots = 4;
            const float gap = 6.0f;
            const float slotW = std::floor((width - gap * (kSlots - 1)) / static_cast<float>(kSlots));
            ImDrawList* draw = ImGui::GetWindowDrawList();

            // Header title
            draw->AddText(ImVec2(pos.x + 2.0f, pos.y), UITheme::Colors::TextMuted, L10n::Tr("TACTICAL QUICKBAR [4 - 7]"));
            const float startY = pos.y + 18.0f;

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

                UIWidgets::DrawSlotFrame(draw, a, b, occupied, hovered, false, false, 3.0f);
                UIWidgets::DrawCornerTicks(draw, a, b, 5.0f, hovered ? UITheme::Colors::AccentAmber : UITheme::Colors::BorderMuted);

                // Slot Key badge (4..7)
                char key[8];
                std::snprintf(key, sizeof(key), "%d", slot + 4);
                UIWidgets::DrawKeyBadge(draw, ImVec2(a.x + 3.0f, a.y + 3.0f), key, 16.0f);

                if (occupied && def)
                {
                    DrawItemIcon(context, def->id, ImVec2(a.x + 24.0f, a.y + 3.0f), ImVec2(b.x - 4.0f, b.y - 14.0f));

                    if (count > 0)
                    {
                        char cStr[16];
                        std::snprintf(cStr, sizeof(cStr), "x%d", count);
                        const ImVec2 cSize = ImGui::CalcTextSize(cStr);
                        draw->AddText(ImVec2(b.x - cSize.x - 4.0f, b.y - 15.0f),
                                      UITheme::Colors::AccentAmber, cStr);
                    }

                    const auto sName = FitText(std::string(L10n::Tr(def->displayName)), slotW - 8.0f);
                    draw->AddText(ImVec2(a.x + 4.0f, b.y - 13.0f), UITheme::Colors::TextSecondary, sName.c_str());

                    if (hovered)
                    {
                        ImGui::SetTooltip("%s [%d]\n%s\n%s", L10n::Tr("Quickbar Slot"), slot + 4,
                                          std::string(L10n::Tr(def->displayName)).c_str(),
                                          L10n::Tr("[RMB] Unbind slot"));
                    }

                    if (ImGui::BeginPopupContextItem("##qb_ctx"))
                    {
                        if (ImGui::MenuItem(L10n::Label("Unbind Slot")) && context.bindQuick)
                        {
                            context.bindQuick(slot, "");
                        }
                        ImGui::EndPopup();
                    }
                }
                else
                {
                    draw->AddText(ImVec2(a.x + (slotW - 36.0f) * 0.5f, a.y + 24.0f),
                                  IM_COL32(75, 88, 105, 160), L10n::Tr("[ EMPTY ]"));
                }

                // Drag Drop Target to bind item
                if (ImGui::BeginDragDropTarget())
                {
                    if (const auto* payload = ImGui::AcceptDragDropPayload(kItemPayload))
                    {
                        const auto* dragged = context.inventory->FindInstance(*static_cast<const FItemInstanceId*>(payload->Data));
                        if (dragged && context.bindQuick)
                        {
                            context.bindQuick(slot, dragged->defId);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::PopID();
            }

            ImGui::SetCursorScreenPos(ImVec2(pos.x, startY + slotH + 8.0f));
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
                for (const FHudNearbyLoot& loot : context.nearby)
                {
                    ImGui::PushID(loot.index);
                    const ImVec2 lootPos = ImGui::GetCursorScreenPos();
                    const float cardW = ImGui::GetContentRegionAvail().x;
                    const float cardH = 44.0f;
                    const ImVec2 cardMax(lootPos.x + cardW, lootPos.y + cardH);

                    ImGui::SetCursorScreenPos(ImVec2(lootPos.x + 58.0f, lootPos.y));
                    ImGui::InvisibleButton("##loot_card", ImVec2(cardW - 58.0f, cardH));
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
                    if (ImGui::Button(NextDayz::L10n::Label("Take"), ImVec2(50.0f, 26.0f)) && context.takeLoot)
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
            ImGui::SetNextWindowBgAlpha(1.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, UITheme::Colors::SurfaceBg);
            ImGui::PushStyleColor(ImGuiCol_Border, UITheme::Colors::BorderMuted);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));

            ImGui::Begin("##nd_inv_window", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

            // ====================================================================
            // INVENTORY TITLE
            // ====================================================================
            const ImVec2 headerPos = ImGui::GetCursorScreenPos();
            const float fullW = ImGui::GetContentRegionAvail().x;
            const float headerH = 46.0f;
            const ImVec2 headerMax(headerPos.x + fullW, headerPos.y + headerH);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            draw->AddRectFilled(headerPos, headerMax, UITheme::Colors::SurfaceHeader, 3.0f);
            draw->AddRect(headerPos, headerMax, UITheme::Colors::BorderMuted, 3.0f, 0, 1.0f);
            UIWidgets::DrawCornerTicks(draw, headerPos, headerMax, 8.0f, UITheme::Colors::BorderTactical);
            draw->AddRectFilled(headerPos, ImVec2(headerPos.x + 4.0f, headerMax.y), UITheme::Colors::AccentAmber, 3.0f);

            ImGui::PushFont(context.titleFont ? context.titleFont : ImGui::GetFont());
            ImGui::SetCursorScreenPos(ImVec2(headerPos.x + 14.0f, headerPos.y + (headerH - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::TextUnformatted(L10n::Tr("Inventory"));
            ImGui::PopFont();

            const float closeBtnW = 110.0f;
            const float closeBtnH = 32.0f;
            ImGui::SetCursorScreenPos(ImVec2(headerMax.x - closeBtnW - 8.0f, headerPos.y + 7.0f));
            if (ImGui::Button(L10n::Label("CLOSE [TAB]"), ImVec2(closeBtnW, closeBtnH)) && context.closeInventory)
            {
                context.closeInventory();
            }

            ImGui::SetCursorScreenPos(ImVec2(headerPos.x, headerMax.y + 10.0f));

            // ====================================================================
            // 3-COLUMN ERGONOMIC TACTICAL LAYOUT
            // Column 0: VICINITY & JETTISON (0.22)
            // Column 1: OPERATOR DOSSIER, VITALS & WEAPONS (0.39)
            // Column 2: LOADOUT SLOTS, CARGO RIGS & QUICKBAR (0.39)
            // ====================================================================
            const float footerHeight = 44.0f;
            const float bodyHeight = std::max(100.0f, ImGui::GetContentRegionAvail().y - footerHeight);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(10.0f, 6.0f));

            if (ImGui::BeginTable("##inv_3col_table", 3,
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp,
                                  ImVec2(0.0f, bodyHeight)))
            {
                ImGui::TableSetupColumn("vicinity", ImGuiTableColumnFlags_WidthStretch, 0.22f);
                ImGui::TableSetupColumn("operator_center", ImGuiTableColumnFlags_WidthStretch, 0.39f);
                ImGui::TableSetupColumn("rigs_and_cargo", ImGuiTableColumnFlags_WidthStretch, 0.39f);
                ImGui::TableNextRow();
                const float columnHeight = bodyHeight - 12.0f;

                // ------------------------------------------------------------
                // COLUMN 0: VICINITY & JETTISON
                // ------------------------------------------------------------
                ImGui::TableSetColumnIndex(0);
                DrawVicinityColumn(context, columnHeight);

                // ------------------------------------------------------------
                // COLUMN 1: OPERATOR DOSSIER, VITALS & WEAPONS
                // ------------------------------------------------------------
                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("##col_operator", ImVec2(0.0f, columnHeight), false);

                // The survivor and vitals share the main panel; reserve room for all weapon rigs below.
                const float characterHeight = std::max(380.0f, columnHeight - 360.0f);
                DrawCharacterPreview(context, characterHeight);
                ImGui::Spacing();

                // Firearms Arsenal
                const ImVec2 wpHeaderPos = ImGui::GetCursorScreenPos();
                const float wpW = ImGui::GetContentRegionAvail().x;
                draw->AddRectFilled(wpHeaderPos, ImVec2(wpHeaderPos.x + wpW, wpHeaderPos.y + 24.0f), UITheme::Colors::SurfaceHeader, 3.0f);
                draw->AddRectFilled(wpHeaderPos, ImVec2(wpHeaderPos.x + 3.0f, wpHeaderPos.y + 24.0f), UITheme::Colors::AccentOrange, 3.0f);
                draw->AddText(ImVec2(wpHeaderPos.x + 10.0f, wpHeaderPos.y + 4.0f), UITheme::Colors::TextPrimary, L10n::Tr("TACTICAL ARSENAL & WEAPON RIGS"));
                ImGui::Dummy(ImVec2(wpW, 26.0f));
                ImGui::Spacing();

                DrawWeaponSlot(context, NextDayz::L10n::Tr("PRIMARY WEAPON [1]"), 0);
                DrawWeaponSlot(context, NextDayz::L10n::Tr("SECONDARY WEAPON [2]"), 1);
                DrawWeaponSlot(context, NextDayz::L10n::Tr("WAIST SIDEARM [3]"), 2);

                ImGui::EndChild();

                // ------------------------------------------------------------
                // COLUMN 2: LOADOUT SLOTS, CARGO RIGS & QUICKBAR
                // ------------------------------------------------------------
                ImGui::TableSetColumnIndex(2);
                const float qbHeight = 78.0f;
                const float cargoScrollHeight = std::max(100.0f, columnHeight - qbHeight - 6.0f);

                ImGui::BeginChild("##col_cargo_scroll", ImVec2(0.0f, cargoScrollHeight), false);

                // 1. Body Equipment Slots
                const ImVec2 eqHeaderPos = ImGui::GetCursorScreenPos();
                const float eqW = ImGui::GetContentRegionAvail().x;
                draw->AddRectFilled(eqHeaderPos, ImVec2(eqHeaderPos.x + eqW, eqHeaderPos.y + 24.0f), UITheme::Colors::SurfaceHeader, 3.0f);
                draw->AddRectFilled(eqHeaderPos, ImVec2(eqHeaderPos.x + 3.0f, eqHeaderPos.y + 24.0f), UITheme::Colors::AccentViolet, 3.0f);
                draw->AddText(ImVec2(eqHeaderPos.x + 10.0f, eqHeaderPos.y + 4.0f), UITheme::Colors::TextPrimary, L10n::Tr("BODY ARMOR & EQUIPMENT RIG"));
                ImGui::Dummy(ImVec2(eqW, 26.0f));
                ImGui::Spacing();

                DrawEquipmentSlot(context, NextDayz::L10n::Tr("HEAD"), EEquipSlot::Head);
                DrawEquipmentSlot(context, NextDayz::L10n::Tr("TORSO"), EEquipSlot::Torso);
                DrawEquipmentSlot(context, NextDayz::L10n::Tr("VEST"), EEquipSlot::Vest);
                DrawEquipmentSlot(context, NextDayz::L10n::Tr("LEGS"), EEquipSlot::Legs);
                DrawEquipmentSlot(context, NextDayz::L10n::Tr("BACKPACK"), EEquipSlot::Back);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // 2. Cargo Rig Containers
                for (const auto& container : inventory.Containers())
                {
                    DrawCargoContainer(context, container);
                }

                ImGui::EndChild();

                // 3. Fixed Tactical Quickbar pinned at bottom of column 2
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
            const ImVec2 fMax(fPos.x + fW, fPos.y + 34.0f);

            draw->AddRectFilled(fPos, fMax, UITheme::Colors::SurfaceHeader, 2.0f);
            draw->AddRect(fPos, fMax, UITheme::Colors::BorderMuted, 2.0f, 0, 1.0f);

            // Notifications pill on right if active
            if (!context.notifications.empty() && context.notifications.back().age < 6.5f)
            {
                const auto& message = context.notifications.back();
                const ImU32 notifCol = message.severity == 2 ? UITheme::Colors::AccentRose :
                                       message.severity == 1 ? UITheme::Colors::AccentAmber : UITheme::Colors::AccentEmerald;
                const auto text = FitText(message.text.Display(), fW * 0.4f);
                const ImVec2 nSize = ImGui::CalcTextSize(text.c_str());
                draw->AddRectFilled(ImVec2(fMax.x - nSize.x - 16.0f, fPos.y + 5.0f),
                                    ImVec2(fMax.x - 6.0f, fMax.y - 5.0f), IM_COL32(20, 26, 36, 220), 2.0f);
                draw->AddText(ImVec2(fMax.x - nSize.x - 12.0f, fPos.y + 8.0f), notifCol, text.c_str());
            }

            // Tactical keycaps row
            float curX = fPos.x + 8.0f;
            const float keyY = fPos.y + 6.0f;
            const auto drawKeyGuide = [&](const char* key, const char* desc)
            {
                const float kw = UIWidgets::DrawKeyBadge(draw, ImVec2(curX, keyY), key, 22.0f);
                curX += kw + 5.0f;
                draw->AddText(ImVec2(curX, keyY + 3.0f), UITheme::Colors::TextSecondary, desc);
                curX += ImGui::CalcTextSize(desc).x + 14.0f;
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
            ImGui::PopStyleColor(2);
        }

        // ====================================================================
        // SESSION OVERLAY (PAUSE & DEATH SCREEN)
        // ====================================================================
        void DrawSessionOverlay(const FHudContext& context)
        {
            if (context.settingsOpen) return;
            if (!context.paused && context.survival.lifeState != EPlayerLifeState::Dead)
            {
                return;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            if (context.survival.lifeState == EPlayerLifeState::Dead)
            {
                ImGui::GetBackgroundDrawList()->AddRectFilled(viewport->Pos,
                    ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                    IM_COL32(20, 0, 0, 160));
            }
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(std::min(760.0f, viewport->WorkSize.x - 40.0f), 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.95f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, UITheme::Colors::SurfaceBg);
            ImGui::PushStyleColor(ImGuiCol_Border, UITheme::Colors::BorderMuted);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));

            ImGui::Begin("##nd_session", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
            if (context.survival.lifeState == EPlayerLifeState::Dead)
            {
                ImGui::SetWindowFontScale(1.5f);
                ImGui::TextColored(ImVec4(0.95f, 0.2f, 0.15f, 1.0f), "%s", NextDayz::L10n::Tr("YOU ARE DEAD"));
                ImGui::SetWindowFontScale(1.0f);
                ImGui::Separator();
                ImGui::Text(NextDayz::L10n::Tr("Cause:       %s"), context.survival.lastDamageSource.empty()
                    ? NextDayz::L10n::Tr("Unknown") : NextDayz::L10n::Tr(context.survival.lastDamageSource));
                ImGui::Text(NextDayz::L10n::Tr("Survived:    %02d:%02d"), static_cast<int>(context.survivalSeconds) / 60,
                            static_cast<int>(context.survivalSeconds) % 60);
                ImGui::Text(NextDayz::L10n::Tr("Infected:    %d Killed"), context.kills);
                ImGui::Spacing();
                if (context.restartSession && ImGui::Button(NextDayz::L10n::Label("WAKE UP AGAIN"), ImVec2(-1.0f, 38.0f)))
                {
                    context.restartSession();
                }
            }
            else
            {
                ImGui::SetWindowFontScale(1.25f);
                ImGui::TextUnformatted(NextDayz::L10n::Tr("GAME PAUSED"));
                ImGui::SetWindowFontScale(1.0f);
                ImGui::TextDisabled("%s", NextDayz::L10n::Tr("Press Esc to resume"));
                ImGui::Separator();
                ImGui::TextWrapped("%s", NextDayz::L10n::Tr("WASD move  Shift sprint  Ctrl walk  C crouch  Space jump/vault"));
                ImGui::TextWrapped("%s", NextDayz::L10n::Tr("LMB fire/swing  RMB tap ADS / hold aim  R reload  1/2/3/Q weapons  E take  F fill bottle"));
                ImGui::TextWrapped("%s", NextDayz::L10n::Tr("Tab inventory  4-7 quickbar  H bandage  M map  L lantern  V view  Alt free look"));
                ImGui::Spacing();
                if (context.resumeSession && ImGui::Button(NextDayz::L10n::Label("RESUME GAME"), ImVec2(-1.0f, 38.0f)))
                    context.resumeSession();
                if (context.openSettings && ImGui::Button(NextDayz::L10n::Label("SETTINGS"), ImVec2(-1.0f, 38.0f)))
                    context.openSettings();
                const float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                if (context.restartSession && ImGui::Button(NextDayz::L10n::Label("RESTART RUN"), ImVec2(halfWidth, 36.0f)))
                {
                    context.restartSession();
                }
                if (context.restartSession) ImGui::SameLine();
                if (context.returnToLobby && ImGui::Button(NextDayz::L10n::Label("RETURN TO LOBBY"), ImVec2(-1,36)))
                    context.returnToLobby();
            }
            if (context.survival.lifeState == EPlayerLifeState::Dead && context.openSettings &&
                ImGui::Button(NextDayz::L10n::Label("SETTINGS"), ImVec2(-1.0f, 38.0f))) context.openSettings();
            if (context.survival.lifeState == EPlayerLifeState::Dead && context.returnToLobby &&
                ImGui::Button(NextDayz::L10n::Label("RETURN TO LOBBY"), ImVec2(-1.0f, 36.0f))) context.returnToLobby();
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
    }

    // ========================================================================
    // MAIN DRAW ENTRY POINT
    // ========================================================================
    void Draw(const FHudContext& context)
    {
        if (!context.sessionLabel.empty())
        {
            const auto* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 20, viewport->WorkPos.y + 20));
            ImGui::SetNextWindowBgAlpha(0.8f);
            ImGui::Begin("##nd_session_mode", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextUnformatted(context.sessionLabel.c_str());
            ImGui::End();
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
