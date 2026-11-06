#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/UI/Localization.hpp"
#include "NextDayzHUD.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

#include <imgui.h>

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
        void DrawScope(const FHudContext& context)
        {
            if (!context.scopeView || !context.firstPerson || !context.aiming || context.showInventory || context.showMap) return;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImVec2 center = viewport->GetCenter();
            const float radius = std::min(viewport->Size.x, viewport->Size.y) * 0.43f;
            const float outside = std::hypot(viewport->Size.x, viewport->Size.y);
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            constexpr int segments = 96;
            const auto point = [&](float angle, float distance) {
                return ImVec2(center.x + std::cos(angle) * distance, center.y + std::sin(angle) * distance);
            };
            // One magnified scene through the lens; the housing masks its
            // periphery. The unscoped weapon mesh would occlude this image.
            const ImDrawListFlags flags = draw->Flags;
            draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            for (int i = 0; i < segments; ++i)
            {
                const float a = 6.2831853f * i / segments;
                const float b = 6.2831853f * (i + 1) / segments;
                draw->AddQuadFilled(point(a, radius), point(a, outside), point(b, outside), point(b, radius), IM_COL32(8, 9, 10, 255));
            }
            draw->Flags = flags;
            draw->AddCircle(center, radius - 2.0f, IM_COL32(30, 32, 33, 255), segments, 9.0f);
            const ImU32 ink = IM_COL32(12, 14, 12, 245);
            const auto line = [&](float x0, float y0, float x1, float y1) {
                draw->AddLine(ImVec2(center.x + x0 * radius, center.y + y0 * radius),
                              ImVec2(center.x + x1 * radius, center.y + y1 * radius), ink, 1.7f);
            };
            if (context.optic == EWeaponAttachment::Scope4x)
            {
                line(-0.80f, 0.0f, 0.80f, 0.0f);
                line(0.0f, -0.80f, 0.0f, 0.80f);
                for (int i = 1; i <= 5; ++i)
                {
                    line(-0.015f, i * 0.10f, 0.015f, i * 0.10f);
                    line(i * 0.10f, -0.015f, i * 0.10f, 0.015f);
                    line(-i * 0.10f, -0.015f, -i * 0.10f, 0.015f);
                }
                return;
            }
            // PSO-style chevron: its apex is the camera / hitscan aim point.
            for (int i = 0; i < 4; ++i)
            {
                const float y = i * 0.085f;
                line(-0.027f, y + 0.027f, 0.0f, y);
                line(0.0f, y, 0.027f, y + 0.027f);
            }
            line(-0.75f, 0.0f, -0.12f, 0.0f);
            line(0.12f, 0.0f, 0.75f, 0.0f);
            for (int i = 2; i <= 7; ++i)
            {
                const float x = i * 0.1f;
                line(-x, -0.012f, -x, 0.012f);
                line(x, -0.012f, x, 0.012f);
            }
        }

        void DrawCrosshair(const FHudContext& context)
        {
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            if (context.firstPerson && context.aiming && context.sightReady && !context.showInventory && !context.showMap &&
                (context.optic == EWeaponAttachment::RedDot || context.optic == EWeaponAttachment::Holographic))
            {
                const ImVec2 reticle(center.x + context.reflexOffsetPx.x, center.y + context.reflexOffsetPx.y);
                const ImU32 red = IM_COL32(255, 58, 40, 245);
                draw->AddCircleFilled(reticle, 2.0f, red, 12);
                if (context.optic == EWeaponAttachment::Holographic)
                {
                    draw->AddCircle(reticle, 12.0f, red, 48, 1.5f);
                    draw->AddLine(ImVec2(reticle.x, reticle.y - 16), ImVec2(reticle.x, reticle.y - 10), red, 1.5f);
                    draw->AddLine(ImVec2(reticle.x, reticle.y + 10), ImVec2(reticle.x, reticle.y + 16), red, 1.5f);
                }
            }
            const ImU32 color = IM_COL32(235, 235, 235, 220);
            const ImU32 outline = IM_COL32(20, 20, 20, 210);
            constexpr float gap = 6.0f;
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
            else if (!(context.firstPerson && context.aiming))
            {
                line(ImVec2(center.x - gap - len, center.y), ImVec2(center.x - gap, center.y));
                line(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + len, center.y));
                line(ImVec2(center.x, center.y - gap - len), ImVec2(center.x, center.y - gap));
                line(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + len));
            }
            if (context.melee || !(context.firstPerson && context.aiming))
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
            ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), NextDayz::L10n::Tr("GOAL"));
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
                ImGui::TextWrapped(NextDayz::L10n::Tr("You have no map. Look for one in shops, police stations and military sites."));
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
            ImGui::TextDisabled(NextDayz::L10n::Tr("M close  |  grid 100 m  |  north up"));
            ImGui::End();
            ImGui::PopStyleColor(2);
        }

        // ====================================================================
        // MODERN GRID-BASED INVENTORY & EXPLICIT DRAG-DROP
        // ====================================================================

        void ItemTooltip(const FItemDef& definition, const FItemInstance& instance)
        {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, UITheme::Colors::SurfaceBg);
            ImGui::PushStyleColor(ImGuiCol_Border, UITheme::Colors::BorderMuted);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
            ImGui::BeginTooltip();

            // Category badge & Title
            ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), "[%s]", UITheme::ItemKindBadge(definition.kind));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", std::string(NextDayz::L10n::Tr(definition.displayName)).c_str());

            const int vol = Inventory::InstanceVolume(instance);
            ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.70f, 1.0f), NextDayz::L10n::Tr("Volume: %d Slot%s"), vol, vol == 1 ? "" : "s");

            if (!definition.description.empty())
            {
                ImGui::Separator();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20.0f);
                ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "%s", std::string(NextDayz::L10n::Tr(definition.description)).c_str());
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
                              const ImVec2& slotSize, bool inEquipmentSlot = false)
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
            UIWidgets::DrawSlotFrame(draw, pos, max, true, hovered, false, false, 4.0f);

            // Left Category Color Accent Stripe
            const ImU32 kindColor = UITheme::ItemKindColor(def->kind);
            draw->AddRectFilled(pos, ImVec2(pos.x + 3.0f, max.y), kindColor, 4.0f);

            // Item Display Name
            const std::string name = FitText(std::string(NextDayz::L10n::Tr(def->displayName)), slotSize.x - 14.0f);
            draw->AddText(ImVec2(pos.x + 7.0f, pos.y + 6.0f), UITheme::Colors::TextPrimary, name.c_str());

            // Bottom-right indicator: Ammo count or stack count
            if (instance.count > 1)
            {
                char countText[16];
                std::snprintf(countText, sizeof(countText), "x%d", instance.count);
                const ImVec2 cSize = ImGui::CalcTextSize(countText);
                draw->AddText(ImVec2(max.x - cSize.x - 5.0f, max.y - cSize.y - 4.0f),
                              UITheme::Colors::AccentAmber, countText);
            }
            else if (def->kind == EItemKind::Weapon)
            {
                char ammoText[16];
                std::snprintf(ammoText, sizeof(ammoText), "[%d]", instance.loadedAmmo);
                const ImVec2 aSize = ImGui::CalcTextSize(ammoText);
                draw->AddText(ImVec2(max.x - aSize.x - 5.0f, max.y - aSize.y - 4.0f),
                              UITheme::Colors::AccentCyan, ammoText);
            }

            // Hover tooltip
            if (hovered)
            {
                ItemTooltip(*def, instance);
            }

            // Drag Source
            if (ImGui::BeginDragDropSource())
            {
                const FItemInstanceId id = instance.instanceId;
                ImGui::SetDragDropPayload(kItemPayload, &id, sizeof(id));

                // Floating Ghost Item Card preview
                ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), "%s", std::string(NextDayz::L10n::Tr(def->displayName)).c_str());
                if (instance.count > 1)
                {
                    ImGui::SameLine();
                    ImGui::Text("x%d", instance.count);
                }
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
            const float slotH = 46.0f;
            const ImVec2 max(pos.x + slotW, pos.y + slotH);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            ImGui::PushID(label);
            ImGui::InvisibleButton("##eq_btn", ImVec2(slotW, slotH));
            const bool hovered = ImGui::IsItemHovered();

            // Background slot frame
            UIWidgets::DrawSlotFrame(draw, pos, max, instance != nullptr, hovered, false, false, 4.0f);

            // Left badge: Slot Type Tag
            draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 6.0f), UITheme::Colors::TextMuted, label);

            if (def && instance)
            {
                // Item Name
                draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 24.0f), UITheme::Colors::TextPrimary,
                              std::string(NextDayz::L10n::Tr(def->displayName)).c_str());

                // Attribute pills on right
                char statText[48] = "";
                if (def->armor > 0.0f)
                {
                    std::snprintf(statText, sizeof(statText), NextDayz::L10n::Tr("Armor +%.0f%%"), def->armor * 100.0f);
                }
                else if (def->warmth > 0.0f)
                {
                    std::snprintf(statText, sizeof(statText), NextDayz::L10n::Tr("Warmth +%.0f%%"), def->warmth * 100.0f);
                }
                else if (def->containerCapacity > 0)
                {
                    std::snprintf(statText, sizeof(statText), NextDayz::L10n::Tr("+%d Slots"), def->containerCapacity);
                }

                if (statText[0] != '\0')
                {
                    const ImVec2 sSize = ImGui::CalcTextSize(statText);
                    draw->AddText(ImVec2(max.x - sSize.x - 12.0f, pos.y + 14.0f),
                                  UITheme::Colors::AccentEmerald, statText);
                }

                if (hovered)
                {
                    ItemTooltip(*def, *instance);
                }

                // Drag Source: dragging worn gear to take off / move
                if (ImGui::BeginDragDropSource())
                {
                    const FItemInstanceId id = instance->instanceId;
                    ImGui::SetDragDropPayload(kItemPayload, &id, sizeof(id));
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
                draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 24.0f), UITheme::Colors::TextMuted, NextDayz::L10n::Tr("[ EMPTY ]"));
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
            const float slotH = 50.0f;
            const ImVec2 max(pos.x + slotW, pos.y + slotH);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            ImGui::PushID(slot + 2000);
            // Leave the Holster button outside the card's hit area.
            ImGui::InvisibleButton("##wp_slot_btn", ImVec2(instance ? slotW - 70.0f : slotW, slotH));
            const bool hovered = ImGui::IsItemHovered();

            UIWidgets::DrawSlotFrame(draw, pos, max, instance != nullptr, hovered, false, false, 4.0f);
            draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 6.0f), UITheme::Colors::TextMuted, label);

            if (def && instance)
            {
                draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 26.0f), UITheme::Colors::TextPrimary,
                              std::string(NextDayz::L10n::Tr(def->displayName)).c_str());

                char ammoText[32];
                std::snprintf(ammoText, sizeof(ammoText), NextDayz::L10n::Tr("Ammo: %d"), instance->loadedAmmo);
                const ImVec2 aSize = ImGui::CalcTextSize(ammoText);
                draw->AddText(ImVec2(max.x - aSize.x - 70.0f, pos.y + 26.0f), UITheme::Colors::AccentCyan, ammoText);

                if (hovered)
                {
                    ItemTooltip(*def, *instance);
                }
            }
            else
            {
                draw->AddText(ImVec2(pos.x + 10.0f, pos.y + 26.0f), UITheme::Colors::TextMuted, NextDayz::L10n::Tr("[ NONE ]"));
            }

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

            // Keep the weapon card body as the target; the nested button must
            // not replace ImGui's last item before BeginDragDropTarget().
            if (instance)
            {
                ImGui::SetCursorScreenPos(ImVec2(max.x - 62.0f, pos.y + 12.0f));
                if (ImGui::SmallButton(NextDayz::L10n::Label("Holster")) && context.unequipWeapon)
                {
                    context.unequipWeapon(slot);
                }
                ImGui::SetCursorScreenPos(ImVec2(pos.x, max.y + ImGui::GetStyle().ItemSpacing.y));
            }

            if (instance && def && def->kind == EItemKind::Weapon && ImGui::TreeNode(L10n::Label("Attachments")))
            {
                for (size_t index = 0; index < kAttachmentSlotCount; ++index)
                {
                    const auto kind = static_cast<EAttachmentSlot>(index);
                    const auto* part = FindAttachmentDef(instance->attachments[index]);
                    const auto text = fmt::format("{}: {}", L10n::Tr(AttachmentSlotName(kind)),
                                                  part ? L10n::Tr(part->displayName) : std::string_view(L10n::Tr("[ NONE ]")));
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::Button(text.c_str(), ImVec2(-1.0f, 0.0f))) ImGui::OpenPopup("parts");
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
                    if (ImGui::BeginPopup("parts"))
                    {
                        DrawAttachmentOptions(context, *instance, kind);
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
            ImGui::Spacing();
        }

        // ====================================================================
        // MAIN INVENTORY WINDOW (3-COLUMN TACTICAL LAYOUT)
        // ====================================================================
        void DrawInventoryPanel(const FHudContext& context)
        {
            if (!context.showInventory || context.inventory == nullptr)
            {
                return;
            }
            const Inventory& inventory = *context.inventory;
            const ImGuiViewport* vp = ImGui::GetMainViewport();

            ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(std::min(1220.0f, vp->Size.x - 30.0f), std::min(720.0f, vp->Size.y - 60.0f)),
                                     ImGuiCond_Appearing);
            ImGui::SetNextWindowBgAlpha(0.95f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, UITheme::Colors::SurfaceBg);
            ImGui::PushStyleColor(ImGuiCol_Border, UITheme::Colors::BorderMuted);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));

            ImGui::Begin(NextDayz::L10n::Label("INVENTORY & TACTICAL LOADOUT##nd_inv_window"), nullptr, ImGuiWindowFlags_NoCollapse);

            // Three columns: Vicinity (26%), Equipment (32%), Cargo Containers (42%)
            if (ImGui::BeginTable("##inv_3col_table", 3,
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                  ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 1.8f)))
            {
                ImGui::TableSetupColumn(NextDayz::L10n::Tr("VICINITY (GROUND)"), ImGuiTableColumnFlags_WidthStretch, 0.26f);
                ImGui::TableSetupColumn(NextDayz::L10n::Tr("EQUIPMENT & GEAR"), ImGuiTableColumnFlags_WidthStretch, 0.32f);
                ImGui::TableSetupColumn(NextDayz::L10n::Tr("CARGO CONTAINERS"), ImGuiTableColumnFlags_WidthStretch, 0.42f);
                ImGui::TableHeadersRow();
                ImGui::TableNextRow();
                const float columnHeight = std::max(100.0f, ImGui::GetContentRegionAvail().y);

                // ------------------------------------------------------------
                // COLUMN 0: VICINITY & DROP ZONE
                // ------------------------------------------------------------
                ImGui::TableSetColumnIndex(0);
                ImGui::BeginChild("##vicinity", ImVec2(0.0f, columnHeight));
                ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), NextDayz::L10n::Tr("NEARBY LOOT (%d)"), static_cast<int>(context.nearby.size()));
                ImGui::Spacing();

                if (context.nearby.empty())
                {
                    ImGui::TextDisabled(NextDayz::L10n::Tr("No ground items nearby."));
                }

                for (const FHudNearbyLoot& loot : context.nearby)
                {
                    ImGui::PushID(loot.index);
                    const ImVec2 lootPos = ImGui::GetCursorScreenPos();
                    const float cardW = ImGui::GetContentRegionAvail().x;
                    const float cardH = 38.0f;
                    ImDrawList* draw = ImGui::GetWindowDrawList();

                    // The Take button needs its own hit area. Overlapping it with
                    // the card's InvisibleButton would steal its mouse press.
                    ImGui::SetCursorScreenPos(ImVec2(lootPos.x + 56.0f, lootPos.y));
                    ImGui::InvisibleButton("##loot_card", ImVec2(cardW - 56.0f, cardH));
                    const bool hovered = ImGui::IsItemHovered();

                    // Bind the source to the card before submitting its Take button.
                    if (ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload(kLootPayload, &loot.index, sizeof(loot.index));
                        ImGui::TextUnformatted(NextDayz::L10n::Localize(loot.name, NextDayz::L10n::Language()).c_str());
                        ImGui::EndDragDropSource();
                    }

                    UIWidgets::DrawSlotFrame(draw, lootPos, ImVec2(lootPos.x + cardW, lootPos.y + cardH),
                                             true, hovered, false, false, 4.0f);

                    // Quick Take Button
                    ImGui::SetCursorScreenPos(ImVec2(lootPos.x + 6.0f, lootPos.y + 7.0f));
                    if (ImGui::SmallButton(NextDayz::L10n::Label("Take")) && context.takeLoot)
                    {
                        context.takeLoot(loot.index, 0, EEquipSlot::None);
                    }

                    // Loot Name & Distance
                    draw->AddText(ImVec2(lootPos.x + 56.0f, lootPos.y + 10.0f), UITheme::Colors::TextPrimary,
                                  NextDayz::L10n::Localize(loot.name, NextDayz::L10n::Language()).c_str());
                    char distText[16];
                    std::snprintf(distText, sizeof(distText), "%.1fm", loot.distance);
                    const ImVec2 dSize = ImGui::CalcTextSize(distText);
                    draw->AddText(ImVec2(lootPos.x + cardW - dSize.x - 8.0f, lootPos.y + 10.0f),
                                  UITheme::Colors::TextMuted, distText);

                    ImGui::SetCursorScreenPos(ImVec2(lootPos.x, lootPos.y + cardH + ImGui::GetStyle().ItemSpacing.y));

                    ImGui::PopID();
                    ImGui::Spacing();
                }

                // Explicit DROP ZONE: Drag items here to drop to ground
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                const ImVec2 dropPos = ImGui::GetCursorScreenPos();
                const float dropW = ImGui::GetContentRegionAvail().x;
                const float dropH = std::max(90.0f, ImGui::GetContentRegionAvail().y - 10.0f);
                const ImVec2 dropMax(dropPos.x + dropW, dropPos.y + dropH);
                ImDrawList* draw = ImGui::GetWindowDrawList();

                ImGui::InvisibleButton("##drop_zone_btn", ImVec2(dropW, dropH));

                // Dashed drop zone box
                draw->AddRect(dropPos, dropMax, UITheme::Colors::BorderMuted, 4.0f, 0, 1.5f);
                const char* dropPrompt = NextDayz::L10n::Tr("[ DRAG ITEMS HERE TO DROP ]");
                const ImVec2 promptSize = ImGui::CalcTextSize(dropPrompt);
                draw->AddText(ImVec2(dropPos.x + (dropW - promptSize.x) * 0.5f, dropPos.y + (dropH - promptSize.y) * 0.5f),
                              UITheme::Colors::TextMuted, dropPrompt);

                if (ImGui::BeginDragDropTarget())
                {
                    draw->AddRectFilled(dropPos, dropMax, UITheme::Colors::SlotInvalidDropBg, 4.0f);
                    draw->AddRect(dropPos, dropMax, UITheme::Colors::AccentRose, 4.0f, 0, 2.0f);
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kItemPayload))
                    {
                        if (context.dropItem)
                        {
                            context.dropItem(*static_cast<const FItemInstanceId*>(payload->Data), -1);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // ------------------------------------------------------------
                // COLUMN 1: EQUIPMENT & GEAR (PAPERDOLL SLOTS)
                // ------------------------------------------------------------
                ImGui::EndChild();
                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("##equipment", ImVec2(0.0f, columnHeight));
                ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), NextDayz::L10n::Tr("GEAR & WEAPONS"));
                ImGui::Spacing();

                DrawEquipmentSlot(context, NextDayz::L10n::Tr("HEAD"), EEquipSlot::Head);
                DrawEquipmentSlot(context, NextDayz::L10n::Tr("TORSO"), EEquipSlot::Torso);
                DrawEquipmentSlot(context, NextDayz::L10n::Tr("VEST"), EEquipSlot::Vest);
                DrawEquipmentSlot(context, NextDayz::L10n::Tr("LEGS"), EEquipSlot::Legs);
                DrawEquipmentSlot(context, NextDayz::L10n::Tr("BACKPACK"), EEquipSlot::Back);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                DrawWeaponSlot(context, NextDayz::L10n::Tr("PRIMARY WEAPON"), 0);
                DrawWeaponSlot(context, NextDayz::L10n::Tr("SECONDARY WEAPON"), 1);
                DrawWeaponSlot(context, NextDayz::L10n::Tr("WAIST SIDEARM [3]"), 2);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Gear Statistics Card
                ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), NextDayz::L10n::Tr("PROTECTION & TOOLS"));
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), NextDayz::L10n::Tr("Armor:   %.0f%%"), context.armor * 100.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), NextDayz::L10n::Tr("Warmth:  %.0f%%"), context.warmth * 100.0f);
                ImGui::TextColored(ImVec4(0.7f, 0.75f, 0.8f, 1.0f), NextDayz::L10n::Tr("Tools:   %s%s%s%s"),
                                   inventory.HasTool(EItemTool::Compass) ? NextDayz::L10n::Tr("Compass ") : "",
                                   inventory.HasTool(EItemTool::Map) ? NextDayz::L10n::Tr("Map ") : "",
                                   inventory.CanOpenCans() ? NextDayz::L10n::Tr("Can-Opener ") : "",
                                   (!inventory.HasTool(EItemTool::Compass) && !inventory.HasTool(EItemTool::Map) &&
                                    !inventory.CanOpenCans()) ? NextDayz::L10n::Tr("None") : "");

                // ------------------------------------------------------------
                // COLUMN 2: CARGO CONTAINERS (GRID SLOTS MATRIX)
                // ------------------------------------------------------------
                ImGui::EndChild();
                ImGui::TableSetColumnIndex(2);
                ImGui::BeginChild("##cargo", ImVec2(0.0f, columnHeight));
                ImGui::TextColored(ImVec4(0.96f, 0.62f, 0.04f, 1.0f), NextDayz::L10n::Tr("CARGO CAPACITY: %d / %d SLOTS"),
                                   inventory.UsedCapacity(), inventory.TotalCapacity());
                ImGui::Spacing();

                for (const FInventoryContainer& container : inventory.Containers())
                {
                    if (!container.active)
                    {
                        continue;
                    }
                    const int used = inventory.ContainerUsed(container.containerId);
                    const int capacity = container.capacity;
                    ImGui::PushID(static_cast<int>(container.containerId));
                    ImGui::BeginGroup();

                    // Container Header Banner
                    const ImVec2 bannerPos = ImGui::GetCursorScreenPos();
                    const float bannerW = ImGui::GetContentRegionAvail().x;
                    const float bannerH = 26.0f;
                    ImDrawList* cDraw = ImGui::GetWindowDrawList();

                    cDraw->AddRectFilled(bannerPos, ImVec2(bannerPos.x + bannerW, bannerPos.y + bannerH),
                                         UITheme::Colors::SurfaceHeader, 3.0f);
                    cDraw->AddText(ImVec2(bannerPos.x + 8.0f, bannerPos.y + 4.0f), UITheme::Colors::TextPrimary,
                                   NextDayz::L10n::Tr(container.displayName));

                    char capText[128];
                    std::snprintf(capText, sizeof(capText), NextDayz::L10n::Tr("%d / %d Slots"), used, capacity);
                    const ImVec2 capSize = ImGui::CalcTextSize(capText);
                    const ImU32 capColor = used >= capacity ? UITheme::Colors::AccentRose : UITheme::Colors::AccentAmber;
                    cDraw->AddText(ImVec2(bannerPos.x + bannerW - capSize.x - 8.0f, bannerPos.y + 4.0f),
                                   capColor, capText);

                    ImGui::Dummy(ImVec2(bannerW, bannerH));

                    // Capacity Progress Bar
                    const ImVec2 barMin(bannerPos.x, bannerPos.y + bannerH + 2.0f);
                    const ImVec2 barMax(bannerPos.x + bannerW, barMin.y + 4.0f);
                    UIWidgets::DrawTacticalBar(cDraw, barMin, barMax,
                                               capacity > 0 ? static_cast<float>(used) / static_cast<float>(capacity) : 0.0f,
                                               used >= capacity ? UITheme::Colors::AccentRose : UITheme::Colors::AccentEmerald);
                    ImGui::Dummy(ImVec2(bannerW, 8.0f));

                    // Container Grid Matrix
                    // Determine grid dimensions: 4 columns or 5 columns
                    const int cols = (bannerW > 320.0f) ? 5 : 4;
                    const float slotPadding = 6.0f;
                    const float slotW = (bannerW - (cols - 1) * slotPadding) / static_cast<float>(cols);
                    const float slotH = 46.0f;
                    const ImVec2 slotSize(slotW, slotH);

                    // Collect items in this container
                    std::vector<const FItemInstance*> containerItems;
                    for (const FItemInstance& item : inventory.Instances())
                    {
                        if (item.containerId == container.containerId)
                        {
                            containerItems.push_back(&item);
                        }
                    }

                    // A nested table owns row heights and wrapping. SameLine
                    // cursor state from card buttons/popups must not size the grid.
                    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(slotPadding * 0.5f, slotPadding * 0.5f));
                    if (ImGui::BeginTable("##cargo_grid", cols,
                        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX))
                    {
                        int currentSlot = 0;
                        // Render occupied item slots
                        for (const FItemInstance* item : containerItems)
                        {
                            ImGui::TableNextColumn();
                            DrawGridItemSlot(context, *item, slotSize, false);
                            currentSlot++;
                        }

                        // Show actual free volume; a full container has no fake empty cell.
                        const int emptySlotsToShow = std::max(0, capacity - used);
                        const int totalSlotsToDraw = currentSlot + emptySlotsToShow;

                        while (currentSlot < totalSlotsToDraw)
                        {
                            ImGui::TableNextColumn();

                            const ImVec2 ePos = ImGui::GetCursorScreenPos();
                            ImGui::PushID(currentSlot + 10000);
                            ImGui::InvisibleButton("##empty_slot", slotSize);
                            const bool eHovered = ImGui::IsItemHovered();

                            UIWidgets::DrawSlotFrame(cDraw, ePos, ImVec2(ePos.x + slotW, ePos.y + slotH),
                                                     false, eHovered, false, false, 4.0f);

                            // Plus sign in center of empty slot
                            cDraw->AddText(ImVec2(ePos.x + slotW * 0.5f - 4.0f, ePos.y + slotH * 0.5f - 7.0f),
                                           UITheme::Colors::SlotEmptyBorder, "+");

                            ImGui::PopID();
                            currentSlot++;
                        }
                        ImGui::EndTable();
                    }
                    ImGui::PopStyleVar();

                    // EndGroup exposes the complete banner + grid as one target,
                    // including occupied cells, gaps and containers with no free cells.
                    ImGui::EndGroup();
                    AcceptContainerDrop(context, container.containerId);

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::PopID();
                }

                ImGui::EndChild();
                ImGui::EndTable();
            }

            // Bottom Shortcut Instruction Bar
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.70f, 1.0f));
            ImGui::TextWrapped("%s%s", NextDayz::L10n::Tr("[LMB Double-Click] Equip/Use  |  [RMB] Context Menu  |  [Drag & Drop] Move/Drop  |  "),
                               NextDayz::L10n::Tr("[4-7] Quickbar  |  [H] Bandage  |  [M] Map  |  [L] Lantern  |  [TAB] Close"));
            ImGui::PopStyleColor();

            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }

        // ====================================================================
        // SESSION OVERLAY (PAUSE & DEATH SCREEN)
        // ====================================================================
        void DrawSessionOverlay(const FHudContext& context)
        {
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
                ImGui::TextColored(ImVec4(0.95f, 0.2f, 0.15f, 1.0f), NextDayz::L10n::Tr("YOU ARE DEAD"));
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
                ImGui::TextDisabled(NextDayz::L10n::Tr("Press Esc to resume"));
                ImGui::Separator();
                ImGui::TextWrapped("%s", NextDayz::L10n::Tr("WASD move  Shift sprint  Ctrl walk  C crouch  Space jump/vault"));
                ImGui::TextWrapped("%s", NextDayz::L10n::Tr("LMB fire/swing  RMB tap ADS / hold aim  R reload  1/2/3/Q weapons  E take  F fill bottle"));
                ImGui::TextWrapped("%s", NextDayz::L10n::Tr("Tab inventory  4-7 quickbar  H bandage  M map  L lantern  V view"));
                ImGui::Spacing();
                if (context.restartSession && ImGui::Button(NextDayz::L10n::Label("RESTART RUN"), ImVec2(-1.0f, 36.0f)))
                {
                    context.restartSession();
                }
            }
            NextDayz::L10n::DrawLanguageSelector();
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
        DrawInventoryPanel(context);
        DrawMap(context);
        DrawSessionOverlay(context);
    }
}
