#include "NextDayzHUD.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

#include <imgui.h>

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponSystem.hpp"

namespace NextDayz::NextDayzHUD
{
    namespace
    {
        const char* KindLabel(EItemKind kind)
        {
            switch (kind)
            {
            case EItemKind::Weapon:   return "Weapon";
            case EItemKind::Ammo:     return "Ammo";
            case EItemKind::Clothing: return "Clothing";
            case EItemKind::Consumable: return "Consumable";
            case EItemKind::Melee: return "Melee";
            case EItemKind::Misc:
            default:                  return "Misc";
            }
        }

        void DrawCrosshair(const FHudContext& context)
        {
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            const ImU32 color = IM_COL32(235, 235, 235, 220);
            const ImU32 outline = IM_COL32(20, 20, 20, 210);
            const float gap = context.firstPerson && context.aiming ? 2.0f : 6.0f;
            const float len = context.firstPerson && context.aiming ? 5.0f : 10.0f;
            const auto line = [&](const ImVec2& a, const ImVec2& b) {
                draw->AddLine(a, b, outline, 4.0f);
                draw->AddLine(a, b, color, 1.8f);
            };
            line(ImVec2(center.x - gap - len, center.y), ImVec2(center.x - gap, center.y));
            line(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + len, center.y));
            line(ImVec2(center.x, center.y - gap - len), ImVec2(center.x, center.y - gap));
            line(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + len));
            draw->AddCircleFilled(center, 2.4f, outline);
            draw->AddCircleFilled(center, 1.2f, color);

            // Hit marker: four ticks that pop out and settle. White for a hit,
            // amber for the head, red and larger for a kill.
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

        // Red edge vignette when struck (and pulsing at low health), plus an arc
        // around the crosshair pointing at whoever hit you.
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
                // Up the screen is ahead; + yaw (attacker on the left) lands left.
                const float angle = std::atan2(-std::cos(context.hurtYawRadians), -std::sin(context.hurtYawRadians));
                const int alpha = static_cast<int>(235.0f * std::clamp(context.hurt, 0.0f, 1.0f));
                ImDrawList* front = ImGui::GetForegroundDrawList();
                front->PathArcTo(center, radius, angle - 0.38f, angle + 0.38f, 16);
                front->PathStroke(IM_COL32(20, 0, 0, alpha / 2), 0, 11.0f);
                front->PathArcTo(center, radius, angle - 0.36f, angle + 0.36f, 16);
                front->PathStroke(IM_COL32(225, 30, 22, alpha), 0, 7.0f);
            }
        }

        ImU32 SeverityColor(int severity, float alpha)
        {
            const int a = static_cast<int>(255.0f * std::clamp(alpha, 0.0f, 1.0f));
            switch (severity)
            {
            case 1: return IM_COL32(255, 196, 80, a);
            case 2: return IM_COL32(255, 86, 70, a);
            case 3: return IM_COL32(140, 230, 140, a);
            default: return IM_COL32(225, 225, 215, a);
            }
        }

        std::string ItemName(std::string_view itemId)
        {
            const FItemDef* definition = FindItemDef(itemId);
            return definition ? std::string(definition->displayName) : std::string(itemId);
        }

        // Bearing in degrees clockwise from north. Engine +Z is south on the map.
        float BearingDegrees(float yawRadians)
        {
            const float degrees = glm::degrees(std::atan2(std::sin(yawRadians), -std::cos(yawRadians)));
            return degrees < 0.0f ? degrees + 360.0f : degrees;
        }

        void DrawClock(const FHudContext& context)
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 16.0f, vp->Pos.y + 16.0f), ImGuiCond_Always,
                                    ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.30f);
            ImGui::Begin("##nd_clock", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            ImGui::Text("%02d:%02d", context.hour, context.minute);
            ImGui::SameLine();
            const bool clear = !context.overcast;
            ImGui::TextColored(clear ? ImVec4(1.0f, 0.9f, 0.5f, 1.0f) : ImVec4(0.7f, 0.75f, 0.8f, 1.0f), "%s",
                               context.weather.empty() ? (clear ? "Clear" : "Overcast") : context.weather.c_str());
            ImGui::End();
        }

        void DrawObjective(const FHudContext& context)
        {
            if (context.objective.empty())
            {
                return;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float top = context.hasCompass ? 52.0f : 16.0f;
            ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->Pos.y + top), ImGuiCond_Always,
                                    ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.25f);
            ImGui::Begin("##nd_objective", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            ImGui::TextDisabled("SURVIVAL GOAL");
            ImGui::SameLine();
            ImGui::TextUnformatted(context.objective.c_str());
            ImGui::End();
        }

        // Heading strip along the top edge: ticks every 15 degrees, letters at
        // the cardinals, a red pip for a radio cache.
        void DrawCompass(const FHudContext& context)
        {
            if (!context.hasCompass)
            {
                return;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const float width = std::min(520.0f, viewport->Size.x * 0.4f);
            const ImVec2 center(viewport->GetCenter().x, viewport->Pos.y + 26.0f);
            const float pxPerDegree = width / 120.0f;
            draw->AddRectFilled(ImVec2(center.x - width / 2 - 8, center.y - 16), ImVec2(center.x + width / 2 + 8, center.y + 16),
                                IM_COL32(10, 12, 12, 120), 4.0f);
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
                const char* label = tick == 0 ? "N" : tick == 90 ? "E" : tick == 180 ? "S" : tick == 270 ? "W" : nullptr;
                if (label)
                {
                    draw->AddText(ImVec2(x - 4.0f, center.y - 9.0f), tick == 0 ? IM_COL32(255, 110, 90, static_cast<int>(255.0f * fade)) : color, label);
                }
                else
                {
                    draw->AddLine(ImVec2(x, center.y - (tick % 45 == 0 ? 8.0f : 4.0f)), ImVec2(x, center.y + 4.0f), color, 1.5f);
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
                char text[32];
                std::snprintf(text, sizeof(text), "%.0fm", glm::length(glm::vec2(delta3.x, delta3.z)));
                draw->AddText(ImVec2(x - 14.0f, center.y + 17.0f), IM_COL32(255, 120, 100, 230), text);
            }
            draw->AddLine(ImVec2(center.x, center.y - 16), ImVec2(center.x, center.y - 10), IM_COL32(255, 255, 255, 255), 2.0f);
            char degrees[16];
            std::snprintf(degrees, sizeof(degrees), "%03.0f", heading);
            draw->AddText(ImVec2(center.x - 11.0f, center.y + 18.0f), IM_COL32(230, 228, 214, 230), degrees);
        }

        void DrawWeaponReadout(const FHudContext& context)
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 16.0f, vp->Pos.y + vp->Size.y - 16.0f),
                                    ImGuiCond_Always, ImVec2(1.0f, 1.0f));
            ImGui::SetNextWindowBgAlpha(0.30f);
            ImGui::Begin("##nd_weapon", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            if (!context.hasWeapon)
            {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Unarmed");
            }
            else if (context.reloading)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s  Reloading...", context.weaponName.c_str());
            }
            else if (context.melee)
            {
                ImGui::Text("%s", context.weaponName.c_str());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  melee");
            }
            else
            {
                ImGui::Text("%s", context.weaponName.c_str());
                ImGui::SameLine();
                ImGui::TextColored(context.ammoInMag == 0 ? ImVec4(1.0f, 0.4f, 0.3f, 1.0f) : ImVec4(0.9f, 0.95f, 1.0f, 1.0f),
                                   "  %d / %d", context.ammoInMag, context.ammoReserve);
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Slot %d  [%s]", context.activeSlot + 1,
                               context.firstPerson ? "FPS" : "TPS");
            ImGui::End();
        }

        void DrawQuickbar(const FHudContext& context)
        {
            if (context.inventory == nullptr)
            {
                return;
            }
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const float size = 54.0f;
            const float gap = 6.0f;
            const float total = 4.0f * size + 3.0f * gap;
            const ImVec2 origin(vp->GetCenter().x - total / 2.0f, vp->Pos.y + vp->Size.y - size - 18.0f);
            for (int slot = 0; slot < 4; ++slot)
            {
                const ImVec2 a(origin.x + slot * (size + gap), origin.y);
                const ImVec2 b(a.x + size, a.y + size);
                const std::string& itemId = context.quickSlots[slot];
                const int count = itemId.empty() ? 0 : context.inventory->CountOf(itemId);
                draw->AddRectFilled(a, b, IM_COL32(12, 14, 14, itemId.empty() ? 70 : 140), 4.0f);
                draw->AddRect(a, b, IM_COL32(200, 196, 180, count > 0 ? 160 : 60), 4.0f);
                char key[4];
                std::snprintf(key, sizeof(key), "%d", slot + 3);
                draw->AddText(ImVec2(a.x + 4, a.y + 2), IM_COL32(200, 196, 180, 200), key);
                if (!itemId.empty())
                {
                    std::string name = ItemName(itemId);
                    if (name.size() > 8)
                    {
                        name = name.substr(0, 7) + ".";
                    }
                    draw->AddText(ImVec2(a.x + 4, a.y + 20), IM_COL32(235, 232, 220, count > 0 ? 235 : 90), name.c_str());
                    char amount[8];
                    std::snprintf(amount, sizeof(amount), "x%d", count);
                    draw->AddText(ImVec2(a.x + 4, a.y + 36), IM_COL32(170, 170, 160, 200), amount);
                }
            }
        }

        void DrawInteractionPrompt(const FHudContext& context)
        {
            if (context.interactionPrompt.empty())
            {
                return;
            }
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.62f),
                                    ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.35f);
            ImGui::Begin("##nd_interact", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            ImGui::TextUnformatted(context.interactionPrompt.c_str());
            ImGui::End();
        }

        // DayZ-style vitals: short bars plus the body's complaints underneath.
        void DrawSurvival(const FHudContext& context)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 16.0f, viewport->Pos.y + viewport->Size.y - 16.0f),
                                    ImGuiCond_Always, ImVec2(0.0f, 1.0f));
            ImGui::SetNextWindowBgAlpha(0.30f);
            ImGui::Begin("##nd_survival", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            const auto bar = [](const char* label, float value, const ImVec4& color)
            {
                ImGui::TextUnformatted(label);
                ImGui::SameLine(78.0f);
                const bool low = value < 25.0f;
                const float pulse = low ? 0.6f + 0.4f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f) : 1.0f;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(color.x, color.y, color.z, pulse));
                ImGui::ProgressBar(value / 100.0f, ImVec2(145.0f, 12.0f), "");
                ImGui::PopStyleColor();
            };
            const FSurvivalSnapshot& s = context.survival;
            bar("Health", s.health, ImVec4(0.78f, 0.18f, 0.16f, 1.0f));
            bar("Food", s.hunger, ImVec4(0.82f, 0.60f, 0.16f, 1.0f));
            bar("Water", s.hydration, ImVec4(0.18f, 0.52f, 0.88f, 1.0f));
            const float t = static_cast<float>(ImGui::GetTime());
            if (s.bleedingWounds > 0)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.25f + 0.15f * std::sin(t * 8.0f), 0.2f, 1.0f), "BLEEDING x%d  [H] bandage",
                                   s.bleedingWounds);
            }
            if (s.cold > 0.55f)
            {
                ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "FREEZING");
            }
            else if (s.cold > 0.25f)
            {
                ImGui::TextColored(ImVec4(0.65f, 0.82f, 1.0f, 1.0f), "Cold");
            }
            if (context.weak)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Weak - cannot sprint");
            }
            if (s.healing && s.bleedingWounds == 0)
            {
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), s.painkillerSeconds > 0.0f ? "Painkillers" : "Healing");
            }
            if (context.lanternLit)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.4f, 1.0f), "Lantern lit  %.0f min", context.lanternMinutes);
            }
            if (s.lifeState == EPlayerLifeState::Dead)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.25f, 1.0f), "DEAD - %s", s.lastDamageSource.c_str());
            }
            ImGui::End();
        }

        // Messages fade after a few seconds, newest at the bottom.
        void DrawNotifications(const FHudContext& context)
        {
            if (context.notifications.empty())
            {
                return;
            }
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            float y = vp->Pos.y + vp->Size.y - 96.0f;
            const float right = vp->Pos.x + vp->Size.x - 18.0f;
            for (auto it = context.notifications.rbegin(); it != context.notifications.rend(); ++it)
            {
                const float alpha = std::clamp(1.0f - (it->age - 5.0f) / 1.5f, 0.0f, 1.0f);
                if (alpha <= 0.0f)
                {
                    continue;
                }
                const ImVec2 size = ImGui::CalcTextSize(it->text.c_str());
                const ImVec2 a(right - size.x - 12.0f, y - size.y - 6.0f);
                draw->AddRectFilled(a, ImVec2(right, y), IM_COL32(10, 10, 10, static_cast<int>(130.0f * alpha)), 3.0f);
                draw->AddText(ImVec2(a.x + 6.0f, a.y + 3.0f), SeverityColor(it->severity, alpha), it->text.c_str());
                y -= size.y + 10.0f;
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
            ImGui::Begin("Riverland Map##nd_map", nullptr,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings);
            if (!context.hasMap)
            {
                ImGui::TextWrapped("You have no map. Look for one in shops, police stations and military sites.");
                ImGui::End();
                return;
            }
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float mapSide = side - 20.0f;
            const ImVec2 end(origin.x + mapSide, origin.y + mapSide);
            draw->AddRectFilled(origin, end, IM_COL32(196, 188, 158, 255));
            // Map space: 1000 m square, north (scad +y = engine -Z) up.
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
            // The river and the main road, hand-traced from the map's own spec.
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
                draw->AddText(ImVec2(p.x + 7, p.y - 7), IM_COL32(30, 28, 24, 255), poi.name.c_str());
            }
            if (context.cacheMarker)
            {
                const ImVec2 p = toScreen(context.cacheMarker->x, context.cacheMarker->z);
                draw->AddCircle(p, 10.0f, IM_COL32(220, 40, 30, 255), 16, 3.0f);
                draw->AddText(ImVec2(p.x + 12, p.y + 2), IM_COL32(200, 30, 20, 255), "Supply cache");
            }
            // You are here: an arrow along the view direction.
            const ImVec2 me = toScreen(context.playerPosition.x, context.playerPosition.z);
            const glm::vec2 forward(std::sin(context.yawRadians), std::cos(context.yawRadians));
            const glm::vec2 right(forward.y, -forward.x);
            const auto offset = [&](float f, float r)
            {
                return ImVec2(me.x + (forward.x * f + right.x * r), me.y + (forward.y * f + right.y * r));
            };
            draw->AddTriangleFilled(offset(12.0f, 0.0f), offset(-7.0f, 6.0f), offset(-7.0f, -6.0f), IM_COL32(20, 90, 220, 255));
            ImGui::Dummy(ImVec2(mapSide, mapSide));
            ImGui::TextDisabled("M close  |  grid 100 m  |  north up");
            ImGui::End();
        }

        constexpr const char* kItemPayload = "ND_ITEM";
        constexpr const char* kLootPayload = "ND_LOOT";

        void ItemTooltip(const FItemDef& definition, const FItemInstance& instance)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(std::string(definition.displayName).c_str());
            ImGui::TextDisabled("%s  |  %d slot%s", KindLabel(definition.kind), Inventory::InstanceVolume(instance),
                                Inventory::InstanceVolume(instance) == 1 ? "" : "s");
            if (!definition.description.empty())
            {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
                ImGui::TextUnformatted(std::string(definition.description).c_str());
                ImGui::PopTextWrapPos();
            }
            if (definition.kind == EItemKind::Weapon)
            {
                ImGui::Text("Loaded: %d", instance.loadedAmmo);
            }
            if (definition.armor > 0.0f)
            {
                ImGui::Text("Armor %.0f%%", definition.armor * 100.0f);
            }
            if (definition.warmth > 0.0f)
            {
                ImGui::Text("Warmth %.0f%%", definition.warmth * 100.0f);
            }
            ImGui::EndTooltip();
        }

        // One inventory row: drag source, tooltip, double-click default action and a context menu.
        void DrawItemRow(const FHudContext& context, const FItemInstance& instance, bool worn)
        {
            const FItemDef* definition = FindItemDef(instance.defId);
            if (!definition)
            {
                return;
            }
            const Inventory& inventory = *context.inventory;
            ImGui::PushID(static_cast<int>(instance.instanceId));
            char label[96];
            if (instance.count > 1)
            {
                std::snprintf(label, sizeof(label), "%s x%d", std::string(definition->displayName).c_str(), instance.count);
            }
            else if (definition->kind == EItemKind::Weapon)
            {
                std::snprintf(label, sizeof(label), "%s  [%d]", std::string(definition->displayName).c_str(), instance.loadedAmmo);
            }
            else
            {
                std::snprintf(label, sizeof(label), "%s", std::string(definition->displayName).c_str());
            }
            const bool inHands = context.weapons != nullptr &&
                                 (context.weapons->SlotInstance(0) == instance.instanceId ||
                                  context.weapons->SlotInstance(1) == instance.instanceId);
            if (inHands)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.9f, 1.0f, 1.0f));
            }
            const bool clicked = ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick);
            if (inHands)
            {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered())
            {
                ItemTooltip(*definition, instance);
            }
            if (ImGui::BeginDragDropSource())
            {
                const FItemInstanceId id = instance.instanceId;
                ImGui::SetDragDropPayload(kItemPayload, &id, sizeof(id));
                ImGui::TextUnformatted(label);
                ImGui::EndDragDropSource();
            }
            if (clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (definition->kind == EItemKind::Clothing && context.wear && context.takeOff)
                {
                    worn ? context.takeOff(instance.instanceId) : context.wear(instance.instanceId);
                }
                else if (IsWieldable(*definition) && context.equipWeapon)
                {
                    context.equipWeapon(instance.instanceId, definition->equipSlot == EEquipSlot::PrimaryWeapon ? 0 : 1);
                }
                else if (context.useItem)
                {
                    context.useItem(instance.instanceId);
                }
            }
            if (ImGui::BeginPopupContextItem("##item_menu"))
            {
                if ((definition->kind == EItemKind::Consumable || definition->use != EItemUse::None) && context.useItem &&
                    ImGui::MenuItem("Use"))
                {
                    context.useItem(instance.instanceId);
                }
                if (definition->kind == EItemKind::Clothing && !worn && context.wear && ImGui::MenuItem("Wear"))
                {
                    context.wear(instance.instanceId);
                }
                if (worn && context.takeOff && ImGui::MenuItem("Take off"))
                {
                    context.takeOff(instance.instanceId);
                }
                if (IsWieldable(*definition) && context.equipWeapon)
                {
                    if (ImGui::MenuItem("Equip: primary (1)")) context.equipWeapon(instance.instanceId, 0);
                    if (ImGui::MenuItem("Equip: secondary (2)")) context.equipWeapon(instance.instanceId, 1);
                }
                if (instance.count > 1 && context.splitItem && ImGui::MenuItem("Split stack"))
                {
                    context.splitItem(instance.instanceId);
                }
                if (!worn && context.moveItem && ImGui::BeginMenu("Move to"))
                {
                    for (const FInventoryContainer& container : inventory.Containers())
                    {
                        if (container.containerId == instance.containerId || container.ownerItemInstanceId == instance.instanceId)
                        {
                            continue;
                        }
                        char name[96];
                        std::snprintf(name, sizeof(name), "%s (%d/%d)", container.displayName.c_str(),
                                      inventory.ContainerUsed(container.containerId), container.capacity);
                        if (ImGui::MenuItem(name))
                        {
                            context.moveItem(instance.instanceId, container.containerId);
                        }
                    }
                    ImGui::EndMenu();
                }
                if (context.bindQuick && ImGui::BeginMenu("Quickbar"))
                {
                    for (int slot = 0; slot < 4; ++slot)
                    {
                        char name[32];
                        std::snprintf(name, sizeof(name), "Key %d", slot + 3);
                        if (ImGui::MenuItem(name))
                        {
                            context.bindQuick(slot, instance.defId);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (context.dropItem)
                {
                    if (instance.count > 1 && ImGui::MenuItem("Drop one"))
                    {
                        context.dropItem(instance.instanceId, 1);
                    }
                    if (ImGui::MenuItem(instance.count > 1 ? "Drop all" : "Drop"))
                    {
                        context.dropItem(instance.instanceId, -1);
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        // A drop target over the last item: moving an item into a container,
        // or loot from the vicinity into the inventory.
        void AcceptDrops(const FHudContext& context, FContainerId container)
        {
            if (!ImGui::BeginDragDropTarget())
            {
                return;
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kItemPayload))
            {
                const FItemInstanceId id = *static_cast<const FItemInstanceId*>(payload->Data);
                if (container != 0 && context.moveItem)
                {
                    context.moveItem(id, container);
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kLootPayload))
            {
                if (context.takeLoot)
                {
                    context.takeLoot(*static_cast<const int*>(payload->Data));
                }
            }
            ImGui::EndDragDropTarget();
        }

        void DrawEquipmentSlot(const FHudContext& context, const char* label, EEquipSlot slot)
        {
            const Inventory& inventory = *context.inventory;
            const FItemInstanceId worn = inventory.WornInSlot(slot);
            ImGui::TextDisabled("%-8s", label);
            ImGui::SameLine(84.0f);
            if (worn == 0)
            {
                ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "-");
            }
            else if (const FItemInstance* instance = inventory.FindInstance(worn))
            {
                DrawItemRow(context, *instance, true);
            }
            // Dropping clothing on its slot wears it.
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kItemPayload))
                {
                    const FItemInstanceId id = *static_cast<const FItemInstanceId*>(payload->Data);
                    const FItemInstance* dragged = inventory.FindInstance(id);
                    const FItemDef* definition = dragged ? FindItemDef(dragged->defId) : nullptr;
                    if (definition && definition->equipSlot == slot && context.wear)
                    {
                        context.wear(id);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        void DrawWeaponSlot(const FHudContext& context, const char* label, int slot)
        {
            const Inventory& inventory = *context.inventory;
            ImGui::TextDisabled("%-8s", label);
            ImGui::SameLine(84.0f);
            const FItemInstanceId id = context.weapons ? context.weapons->SlotInstance(slot) : 0;
            const FItemInstance* instance = id ? inventory.FindInstance(id) : nullptr;
            const FItemDef* definition = instance ? FindItemDef(instance->defId) : nullptr;
            ImGui::PushID(slot + 1000);
            if (definition)
            {
                ImGui::Text("%s", std::string(definition->displayName).c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Holster") && context.unequipWeapon)
                {
                    context.unequipWeapon(slot);
                }
            }
            else
            {
                const std::string& weaponId = context.weapons ? context.weapons->SlotWeaponId(slot) : std::string();
                ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "%s", weaponId.empty() ? "-" : weaponId.c_str());
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kItemPayload))
                {
                    if (context.equipWeapon)
                    {
                        context.equipWeapon(*static_cast<const FItemInstanceId*>(payload->Data), slot);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }

        void DrawInventoryPanel(const FHudContext& context)
        {
            if (!context.showInventory || context.inventory == nullptr)
            {
                return;
            }
            const Inventory& inventory = *context.inventory;
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(std::min(1180.0f, vp->Size.x - 40.0f), std::min(660.0f, vp->Size.y - 80.0f)),
                                     ImGuiCond_Appearing);
            ImGui::SetNextWindowBgAlpha(0.93f);
            ImGui::Begin("Inventory##nd_inventory", nullptr, ImGuiWindowFlags_NoCollapse);

            if (ImGui::BeginTable("##nd_inventory_columns", 3,
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                  ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 1.6f)))
            {
                ImGui::TableSetupColumn("VICINITY", ImGuiTableColumnFlags_WidthStretch, 0.28f);
                ImGui::TableSetupColumn("EQUIPMENT", ImGuiTableColumnFlags_WidthStretch, 0.32f);
                ImGui::TableSetupColumn("CARGO", ImGuiTableColumnFlags_WidthStretch, 0.40f);
                ImGui::TableHeadersRow();
                ImGui::TableNextRow();

                // ---- vicinity: loot within reach; drag it right or press Take ----
                ImGui::TableSetColumnIndex(0);
                if (context.nearby.empty())
                {
                    ImGui::TextDisabled("Nothing within reach.");
                    ImGui::TextDisabled("Drag items here to drop them.");
                }
                for (const FHudNearbyLoot& loot : context.nearby)
                {
                    ImGui::PushID(loot.index);
                    if (ImGui::SmallButton("Take") && context.takeLoot)
                    {
                        context.takeLoot(loot.index);
                    }
                    ImGui::SameLine();
                    ImGui::Selectable(loot.name.c_str());
                    if (ImGui::BeginDragDropSource())
                    {
                        ImGui::SetDragDropPayload(kLootPayload, &loot.index, sizeof(loot.index));
                        ImGui::TextUnformatted(loot.name.c_str());
                        ImGui::EndDragDropSource();
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("%.1fm", loot.distance);
                    ImGui::PopID();
                }
                // The whole column takes an item dragged out of the inventory: drop it.
                ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(60.0f, ImGui::GetContentRegionAvail().y)));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kItemPayload))
                    {
                        if (context.dropItem)
                        {
                            context.dropItem(*static_cast<const FItemInstanceId*>(payload->Data), -1);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // ---- equipment: what is worn and what is in hand ----
                ImGui::TableSetColumnIndex(1);
                DrawEquipmentSlot(context, "Head", EEquipSlot::Head);
                DrawEquipmentSlot(context, "Torso", EEquipSlot::Torso);
                DrawEquipmentSlot(context, "Vest", EEquipSlot::Vest);
                DrawEquipmentSlot(context, "Legs", EEquipSlot::Legs);
                DrawEquipmentSlot(context, "Back", EEquipSlot::Back);
                ImGui::Separator();
                DrawWeaponSlot(context, "Primary", 0);
                DrawWeaponSlot(context, "Secondary", 1);
                ImGui::Separator();
                ImGui::Text("Armor   %.0f%%", context.armor * 100.0f);
                ImGui::Text("Warmth  %.0f%%", context.warmth * 100.0f);
                ImGui::Text("Cargo   %d / %d", inventory.UsedCapacity(), inventory.TotalCapacity());
                ImGui::Spacing();
                ImGui::TextDisabled("Tools:");
                ImGui::SameLine();
                ImGui::TextDisabled("%s%s%s%s", inventory.HasTool(EItemTool::Compass) ? "Compass " : "",
                                    inventory.HasTool(EItemTool::Map) ? "Map " : "",
                                    inventory.CanOpenCans() ? "Can-opener " : "",
                                    (!inventory.HasTool(EItemTool::Compass) && !inventory.HasTool(EItemTool::Map) &&
                                     !inventory.CanOpenCans()) ? "none" : "");

                // ---- cargo: every container with its contents ----
                ImGui::TableSetColumnIndex(2);
                for (const FInventoryContainer& container : inventory.Containers())
                {
                    if (!container.active)
                    {
                        continue;
                    }
                    const int used = inventory.ContainerUsed(container.containerId);
                    ImGui::PushID(static_cast<int>(container.containerId));
                    char header[128];
                    std::snprintf(header, sizeof(header), "%s   %d / %d%s", container.displayName.c_str(), used,
                                  container.capacity, container.weaponOnly ? "  (weapons)" : "");
                    ImGui::SeparatorText(header);
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, used >= container.capacity
                                                                      ? ImVec4(0.8f, 0.35f, 0.2f, 1.0f)
                                                                      : ImVec4(0.45f, 0.55f, 0.4f, 1.0f));
                    ImGui::ProgressBar(container.capacity > 0 ? static_cast<float>(used) / static_cast<float>(container.capacity) : 0.0f,
                                       ImVec2(-1.0f, 10.0f), "");
                    ImGui::PopStyleColor();
                    AcceptDrops(context, container.containerId);
                    for (const FItemInstance& instance : inventory.Instances())
                    {
                        if (instance.containerId == container.containerId)
                        {
                            DrawItemRow(context, instance, false);
                            AcceptDrops(context, container.containerId);
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.62f, 0.62f, 0.58f, 1.0f),
                               "Double-click: use / wear / equip   Right-click: more   Drag: move, wear, equip, drop   "
                               "Tab close   3-6 quickbar   H bandage   M map   L lantern");
            ImGui::End();
        }

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
                    ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), IM_COL32(20, 0, 0, 150));
            }
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.94f);
            ImGui::Begin("##nd_session", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
            if (context.survival.lifeState == EPlayerLifeState::Dead)
            {
                ImGui::SetWindowFontScale(1.5f);
                ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.15f, 1.0f), "YOU ARE DEAD");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::Separator();
                ImGui::Text("Cause      %s", context.survival.lastDamageSource.empty()
                    ? "unknown" : context.survival.lastDamageSource.c_str());
                ImGui::Text("Survived   %02d:%02d", static_cast<int>(context.survivalSeconds) / 60,
                            static_cast<int>(context.survivalSeconds) % 60);
                ImGui::Text("Infected   %d killed", context.kills);
                ImGui::Spacing();
                if (context.restartSession && ImGui::Button("Wake up again", ImVec2(-1.0f, 36.0f)))
                {
                    context.restartSession();
                }
            }
            else
            {
                ImGui::SetWindowFontScale(1.25f);
                ImGui::TextUnformatted("PAUSED");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::TextDisabled("Press Esc to resume");
                ImGui::Separator();
                ImGui::TextDisabled("WASD move  Shift sprint  Ctrl walk  C crouch  Space jump/vault");
                ImGui::TextDisabled("LMB fire/swing  RMB aim  R reload  1/2/Q weapons  E take  F fill bottle");
                ImGui::TextDisabled("Tab inventory  3-6 quickbar  H bandage  M map  L lantern  V view");
                if (context.restartSession && ImGui::Button("Restart run", ImVec2(-1.0f, 34.0f)))
                {
                    context.restartSession();
                }
            }
            ImGui::End();
        }
    }

    void Draw(const FHudContext& context)
    {
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
