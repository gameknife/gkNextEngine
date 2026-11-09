#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/UI/Localization.hpp"

// World overlay layers of the NextDayz debugger. Everything is projected with
// the camera the frame renders with and drawn on ImGui's background list:
// segments are clipped at the near plane, so a ring around the player or a
// path behind the camera draws correctly, and the overlay shows the same in
// every renderer, with any upscaler, and in UI screenshots. No depth test: the
// layers read through walls, which is what AI/loot debugging wants.

#include "NextDayzDebugger.hpp"

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>
#include <imgui.h>

#include "Engine/Utilities/Math.hpp"
#include "Gameplay/AI/NavGrid.h"

#include "Application/Game/NextDayz/Combat/CombatSystem.hpp"
#include "Application/Game/NextDayz/Combat/NoiseSystem.hpp"
#include "Application/Game/NextDayz/Debugger/DebugStyle.hpp"
#include "Application/Game/NextDayz/Inventory/LootSystem.hpp"
#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"
#include "Application/Game/NextDayz/World/WorldAnchorRegistry.hpp"
#include "Application/Game/NextDayz/World/ZombieSpawnDirector.hpp"

namespace NextDayz::Debug
{
    struct FPainter
    {
        ImDrawList* draw = nullptr;
        glm::mat4 viewProjection{1.0f};
        glm::vec2 origin{0.0f};
        glm::vec2 size{1.0f};
        float nearW = 0.1f;

        glm::vec2 ToScreen(const glm::vec4& clip) const
        {
            const glm::vec2 ndc = glm::vec2(clip) / clip.w;
            return origin + glm::vec2((ndc.x * 0.5f + 0.5f) * size.x, (-ndc.y * 0.5f + 0.5f) * size.y);
        }

        // A point in front of the camera and roughly on screen.
        bool Point(const glm::vec3& world, glm::vec2& screen) const
        {
            const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
            if (clip.w <= nearW)
            {
                return false;
            }
            screen = ToScreen(clip);
            return screen.x > origin.x - size.x * 0.25f && screen.x < origin.x + size.x * 1.25f &&
                   screen.y > origin.y - size.y * 0.25f && screen.y < origin.y + size.y * 1.25f;
        }

        void Line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, float width = 1.5f) const
        {
            glm::vec4 clipA = viewProjection * glm::vec4(a, 1.0f);
            glm::vec4 clipB = viewProjection * glm::vec4(b, 1.0f);
            if (clipA.w <= nearW && clipB.w <= nearW)
            {
                return;
            }
            // Clip the part behind the near plane off in clip space.
            if (clipA.w <= nearW)
            {
                clipA = glm::mix(clipA, clipB, (nearW - clipA.w) / (clipB.w - clipA.w));
            }
            else if (clipB.w <= nearW)
            {
                clipB = glm::mix(clipB, clipA, (nearW - clipB.w) / (clipA.w - clipB.w));
            }
            const glm::vec2 from = ToScreen(clipA);
            const glm::vec2 to = ToScreen(clipB);
            const ImVec2 p0(from.x, from.y);
            const ImVec2 p1(to.x, to.y);
            draw->AddLine(p0, p1, IM_COL32(6, 8, 12, static_cast<int>(150.0f * color.a)), width + 1.5f);
            draw->AddLine(p0, p1, Style::U32(color), width);
        }

        void Dot(const glm::vec3& world, const glm::vec4& color, float radius = 3.0f) const
        {
            glm::vec2 screen;
            if (Point(world, screen))
            {
                draw->AddCircleFilled(ImVec2(screen.x, screen.y), radius, Style::U32(color));
            }
        }

        void LabelAt(const glm::vec2& screen, const glm::vec4& color, const std::string& text) const
        {
            const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
            const ImVec2 position(screen.x + 8.0f, screen.y - textSize.y * 0.5f);
            const ImVec2 min(position.x - 3.0f, position.y - 2.0f);
            const ImVec2 max(position.x + textSize.x + 3.0f, position.y + textSize.y + 2.0f);
            draw->AddRectFilled(min, max, IM_COL32(8, 11, 15, 200), 3.0f);
            draw->AddRect(min, max, Style::U32(Style::Alpha(color, 0.8f)), 3.0f);
            draw->AddText(position, Style::U32(color), text.c_str());
        }

        void Label(const glm::vec3& world, const glm::vec4& color, const std::string& text) const
        {
            glm::vec2 screen;
            if (Point(world, screen))
            {
                LabelAt(screen, color, text);
            }
        }
    };

    namespace
    {
        constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};

        // Circle in the horizontal plane.
        void Ring(const FPainter& paint, const glm::vec3& center, float radius, const glm::vec4& color,
                  float width = 1.5f, int segments = 40)
        {
            glm::vec3 previous = center + glm::vec3(radius, 0.0f, 0.0f);
            for (int index = 1; index <= segments; ++index)
            {
                const float angle = glm::two_pi<float>() * static_cast<float>(index) / static_cast<float>(segments);
                const glm::vec3 next = center + glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
                paint.Line(previous, next, color, width);
                previous = next;
            }
        }

        // Pie slice in the horizontal plane: `forward` +- `halfAngle`.
        void Sector(const FPainter& paint, const glm::vec3& apex, const glm::vec3& forward, float halfAngle,
                    float radius, const glm::vec4& color, float width = 1.5f)
        {
            const float yaw = std::atan2(forward.x, forward.z);
            const int segments = std::max(6, static_cast<int>(halfAngle * 18.0f));
            const auto edge = [&](float angle)
            {
                return apex + glm::vec3(std::sin(angle), 0.0f, std::cos(angle)) * radius;
            };
            glm::vec3 previous = edge(yaw - halfAngle);
            paint.Line(apex, previous, color, width);
            for (int index = 1; index <= segments; ++index)
            {
                const float angle = yaw - halfAngle + 2.0f * halfAngle * static_cast<float>(index) / static_cast<float>(segments);
                const glm::vec3 next = edge(angle);
                paint.Line(previous, next, color, width);
                previous = next;
            }
            paint.Line(previous, apex, color, width);
        }

        void Cross(const FPainter& paint, const glm::vec3& at, float size, const glm::vec4& color, float width = 1.5f)
        {
            paint.Line(at - glm::vec3(size, 0.0f, size), at + glm::vec3(size, 0.0f, size), color, width);
            paint.Line(at - glm::vec3(size, 0.0f, -size), at + glm::vec3(size, 0.0f, -size), color, width);
        }

        void Arrow(const FPainter& paint, const glm::vec3& from, const glm::vec3& to, const glm::vec4& color,
                   float width = 2.0f)
        {
            paint.Line(from, to, color, width);
            glm::vec3 direction = to - from;
            direction.y = 0.0f;
            const float length = glm::length(direction);
            if (length < 1.0e-3f)
            {
                return;
            }
            direction /= length;
            const glm::vec3 side(direction.z, 0.0f, -direction.x);
            const float head = std::min(0.35f, length * 0.35f);
            paint.Line(to, to - direction * head + side * head * 0.6f, color, width);
            paint.Line(to, to - direction * head - side * head * 0.6f, color, width);
        }

        void Capsule(const FPainter& paint, const glm::vec3& a, const glm::vec3& b, float radius, const glm::vec4& color)
        {
            glm::vec3 axis = b - a;
            const float length = glm::length(axis);
            axis = length > 1.0e-4f ? axis / length : kUp;
            const glm::vec3 helper = std::abs(axis.y) < 0.9f ? kUp : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 u = glm::normalize(glm::cross(axis, helper));
            const glm::vec3 v = glm::cross(axis, u);
            constexpr int segments = 10;
            for (const glm::vec3& center : {a, b})
            {
                glm::vec3 previous = center + u * radius;
                for (int index = 1; index <= segments; ++index)
                {
                    const float angle = glm::two_pi<float>() * static_cast<float>(index) / segments;
                    const glm::vec3 next = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
                    paint.Line(previous, next, color, 1.0f);
                    previous = next;
                }
            }
            for (const glm::vec3& offset : {u, -u, v, -v})
            {
                paint.Line(a + offset * radius, b + offset * radius, color, 1.0f);
            }
        }

        // Tall vertical beam that is easy to find from far away.
        void Beam(const FPainter& paint, const glm::vec3& at, const glm::vec4& color, float height = 25.0f)
        {
            paint.Line(at, at + glm::vec3(0.0f, height, 0.0f), color, 2.5f);
        }
    }

    void NextDayzDebugger::DrawOverlay(const FGameFacts& facts)
    {
        if (!systems_.player || !systems_.player->IsValid() || !commands_.viewCamera)
        {
            return;
        }
        const std::optional<FViewCamera> camera = commands_.viewCamera();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!camera || !viewport || viewport->Size.x <= 1.0f || viewport->Size.y <= 1.0f)
        {
            return;
        }
        FPainter paint;
        paint.draw = ImGui::GetBackgroundDrawList();
        paint.origin = glm::vec2(viewport->Pos.x, viewport->Pos.y);
        paint.size = glm::vec2(viewport->Size.x, viewport->Size.y);
        paint.nearW = std::max(0.05f, camera->nearPlane);
        // Same projection as Runtime::EngineHelper::TryProjectWorldToScreenWithCamera.
        const float fov = camera->fovDegrees > 1.0f ? camera->fovDegrees : 60.0f;
        paint.viewProjection = Utilities::Math::ReverseZPerspective(glm::radians(fov), paint.size.x / paint.size.y,
                                                                    paint.nearW, camera->farPlane) *
                               camera->view;

        if (LayerOn(LayerNav)) OverlayNav(paint);
        if (LayerOn(LayerLootZones)) OverlayLootZones(paint);
        if (LayerOn(LayerLoot)) OverlayLoot(paint);
        if (LayerOn(LayerSpawns)) OverlaySpawns(paint);
        if (LayerOn(LayerNoise)) OverlayNoise(paint);
        if (LayerOn(LayerPerception)) OverlayPerception(paint);
        if (LayerOn(LayerInfected)) OverlayInfected(paint);
        if (LayerOn(LayerHitboxes)) OverlayHitboxes(paint);
        if (LayerOn(LayerTraces)) OverlayTraces(paint);
        if (LayerOn(LayerAnchors)) OverlayAnchors(paint, facts);
        if (LayerOn(LayerPlayer)) OverlayPlayer(paint);
        OverlaySelection(paint);
    }

    void NextDayzDebugger::OverlayInfected(FPainter& paint)
    {
        if (!systems_.zombies)
        {
            return;
        }
        const glm::vec3 feet = PlayerFeet();
        const FZombieRuntime* selected = SelectedZombie();
        for (const FZombieRuntime& zombie : systems_.zombies->Slots())
        {
            const float distance = zombie.active ? glm::distance(zombie.position, feet) : 0.0f;
            if (!zombie.active || distance > knobs_.overlayDistance)
            {
                continue;
            }
            const glm::vec4 color = Style::ZombieState(zombie.state);
            const bool isSelected = &zombie == selected;
            paint.Line(zombie.position, zombie.position + glm::vec3(0.0f, 1.9f, 0.0f), color, isSelected ? 3.0f : 2.0f);
            if (zombie.state == EZombieState::Dead)
            {
                continue;
            }
            const glm::vec3 chest = zombie.position + glm::vec3(0.0f, 1.0f, 0.0f);
            Arrow(paint, chest, chest + zombie.forward * 1.2f, color);

            // Remaining route, waypoint by waypoint.
            glm::vec3 previous = zombie.position + glm::vec3(0.0f, 0.15f, 0.0f);
            for (size_t waypoint = zombie.pathWaypoint; waypoint < zombie.path.size(); ++waypoint)
            {
                const glm::vec3 next = zombie.path[waypoint] + glm::vec3(0.0f, 0.15f, 0.0f);
                paint.Line(previous, next, Style::kPath, 2.0f);
                paint.Dot(next, Style::kPath, 3.0f);
                previous = next;
            }
            switch (zombie.state)
            {
            case EZombieState::Investigate:
            case EZombieState::Alert:
            case EZombieState::Chase:
            case EZombieState::Attack:
            {
                const glm::vec3 known = zombie.lastKnownPlayerPosition + glm::vec3(0.0f, 0.2f, 0.0f);
                Cross(paint, known, 0.4f, Style::kWarn, 2.0f);
                paint.Line(chest, known, Style::Alpha(Style::kWarn, 0.45f), 1.0f);
                break;
            }
            case EZombieState::Wander:
                Ring(paint, zombie.roamTarget + glm::vec3(0.0f, 0.1f, 0.0f), 0.35f, Style::Alpha(Style::kGood, 0.7f),
                     1.0f, 12);
                break;
            default:
                break;
            }
            if (zombie.state == EZombieState::Attack)
            {
                Ring(paint, zombie.position + glm::vec3(0.0f, 0.05f, 0.0f), ZombieDef(zombie.profile).attackRange,
                     Style::kBad, 2.0f, 24);
            }

            glm::vec2 screen;
            if (!paint.Point(zombie.position + glm::vec3(0.0f, 2.05f, 0.0f), screen))
            {
                continue;
            }
            if (distance <= knobs_.labelDistance || isSelected)
            {
                const size_t pathLeft = zombie.pathWaypoint < zombie.path.size() ? zombie.path.size() - zombie.pathWaypoint : 0;
                paint.LabelAt(screen, color,
                              NextDayz::L10n::Format("Z{}:{} {} {}  HP {:.0f}\n{:.1f} m  v {:.1f}  path {}{}  repath {:.1f}  "
                                          "stuck {:.1f}  poise {:.0f}{}{}",
                                          zombie.handle.index, zombie.handle.generation, ZombieDef(zombie.profile).id,
                                          NextDayz::L10n::Tr(ZombieStateName(zombie.state)), zombie.health, distance, zombie.Speed(),
                                          pathLeft, zombie.pathFailed ? NextDayz::L10n::Tr(" FAIL") : "", zombie.repathSeconds,
                                          zombie.stuckSeconds, zombie.poise, zombie.Lame() ? NextDayz::L10n::Tr("  lame") : "",
                                          zombie.seesPlayer ? NextDayz::L10n::Tr("  SEES") : ""));
            }
            else
            {
                paint.draw->AddCircleFilled(ImVec2(screen.x, screen.y), 3.5f, Style::U32(color));
            }
        }
    }

    void NextDayzDebugger::OverlayPerception(FPainter& paint)
    {
        if (!systems_.zombies)
        {
            return;
        }
        const glm::vec3 feet = PlayerFeet();
        const FZombieRuntime* selected = SelectedZombie();
        const float sightScale = systems_.zombies->SightScale();
        for (const FZombieRuntime& zombie : systems_.zombies->Slots())
        {
            if (!zombie.active || zombie.state == EZombieState::Dead)
            {
                continue;
            }
            if (selected ? &zombie != selected : glm::distance(zombie.position, feet) > 45.0f)
            {
                continue;
            }
            const FZombieDef& definition = ZombieDef(zombie.profile);
            const glm::vec3 eye = zombie.position + glm::vec3(0.0f, 1.4f, 0.0f);
            const glm::vec4 cone = zombie.seesPlayer ? Style::kBad : Style::Alpha(Style::kInfo, 0.55f);
            Sector(paint, zombie.position + glm::vec3(0.0f, 0.1f, 0.0f), zombie.forward,
                   glm::radians(definition.fieldOfViewDegrees * 0.5f), definition.sightDistance * sightScale, cone,
                   zombie.seesPlayer ? 2.0f : 1.0f);
            if (zombie.seesPlayer)
            {
                paint.Line(eye, feet + glm::vec3(0.0f, 1.0f, 0.0f), Style::kBad, 2.0f);
            }
            if (zombie.heardNoise > 0.0f)
            {
                paint.Line(eye, zombie.lastKnownPlayerPosition + glm::vec3(0.0f, 0.3f, 0.0f), Style::kWarn, 2.0f);
            }
        }
    }

    void NextDayzDebugger::OverlayHitboxes(FPainter& paint)
    {
        if (!systems_.zombies || !systems_.combat)
        {
            return;
        }
        const glm::vec3 feet = PlayerFeet();
        for (const FZombieRuntime& zombie : systems_.zombies->Slots())
        {
            if (!zombie.active || glm::distance(zombie.position, feet) > std::min(knobs_.overlayDistance, 80.0f))
            {
                continue;
            }
            const std::span<const FHitCapsule> capsules = systems_.combat->Hitboxes(zombie.handle);
            for (const FHitCapsule& capsule : capsules)
            {
                Capsule(paint, capsule.a, capsule.b, capsule.radius, Style::HitZone(capsule.zone));
            }
            if (capsules.empty() && zombie.state != EZombieState::Dead)
            {
                paint.Label(zombie.position + glm::vec3(0.0f, 2.4f, 0.0f), Style::kHover, NextDayz::L10n::Tr("NO HITBOXES"));
            }
        }
    }

    void NextDayzDebugger::OverlayTraces(FPainter& paint)
    {
        for (size_t index = 0; index < traces_.size(); ++index)
        {
            const FTraceRecord& record = traces_[index];
            const FWeaponTraceEvent& trace = record.trace;
            const glm::vec4 base = trace.hitZombie ? Style::kGood : trace.hit ? Style::kBad : Style::kWarn;
            const glm::vec4 color = Style::Alpha(base, std::clamp(record.remainingSeconds / 1.5f, 0.25f, 1.0f));
            // From just ahead of the eye: the origin itself sits on the near plane.
            paint.Line(trace.origin + trace.direction * 0.4f, trace.endPoint, color, 2.0f);
            paint.Dot(trace.endPoint, color, trace.hit ? 4.5f : 3.0f);
            if (index + 6 < traces_.size())
            {
                continue;   // label only the newest few
            }
            std::string what = trace.hitZombie ? NextDayz::L10n::Tr("INFECTED") : trace.hit ? NextDayz::L10n::Tr("WORLD") : NextDayz::L10n::Tr("MISS");
            if (trace.hit && !trace.hitZombie && commands_.nodeName)
            {
                what += " " + commands_.nodeName(trace.hitInstanceId);
            }
            paint.Label(trace.endPoint, base,
                        NextDayz::L10n::Format("#{}.{} {} {:.1f} m{}", trace.sequence, trace.pellet, what,
                                    glm::distance(trace.origin, trace.endPoint), trace.melee ? NextDayz::L10n::Tr(" melee") : ""));
        }
    }

    void NextDayzDebugger::OverlayNoise(FPainter& paint)
    {
        if (!systems_.noise)
        {
            return;
        }
        for (const FNoiseEvent& noise : systems_.noise->Events())
        {
            const float fade = std::clamp(noise.remainingSeconds / 1.25f, 0.2f, 1.0f);
            const glm::vec4 color = Style::Alpha(Style::Channel(EChannel::Noise), fade);
            const glm::vec3 center = noise.position + glm::vec3(0.0f, 0.2f, 0.0f);
            Ring(paint, center, noise.radius, color, 2.0f, 64);
            Ring(paint, center, std::min(noise.radius, 2.0f), color, 2.0f, 12);
            paint.Line(center, center + glm::vec3(0.0f, 3.0f, 0.0f), color, 2.0f);
            paint.Label(center + glm::vec3(0.0f, 3.0f, 0.0f), color,
                        NextDayz::L10n::Format("{} r{:.0f} x{:.1f} {:.1f}s", NextDayz::L10n::Tr(NoiseTypeName(noise.type)), noise.radius,
                                    noise.strength, noise.remainingSeconds));
        }
    }

    void NextDayzDebugger::OverlaySpawns(FPainter& paint)
    {
        if (!systems_.spawns)
        {
            return;
        }
        const glm::vec3 feet = PlayerFeet();
        const FZombieSpawnTuning& tuning = systems_.spawns->Tuning();
        const glm::vec3 ground = feet + glm::vec3(0.0f, 0.3f, 0.0f);
        Ring(paint, ground, tuning.minimumDistance, Style::Alpha(Style::kBad, 0.7f), 2.0f, 64);
        Ring(paint, ground, tuning.maximumDistance, Style::Alpha(Style::kGood, 0.6f), 2.0f, 96);
        Ring(paint, ground, tuning.recycleDistance, Style::Alpha(Style::kMuted, 0.5f), 1.5f, 96);

        const auto& points = systems_.spawns->Points();
        const double now = systems_.spawns->WorldSeconds();
        for (size_t index = 0; index < points.size(); ++index)
        {
            const FZombieSpawnPoint& point = points[index];
            const float distance = glm::distance(point.position, feet);
            if (distance > std::max(knobs_.overlayDistance, tuning.maximumDistance + 20.0f))
            {
                continue;
            }
            const glm::vec4 color = Style::SpawnVerdict(point.verdict);
            paint.Line(point.position, point.position + glm::vec3(0.0f, 3.0f, 0.0f), color, 3.0f);
            Ring(paint, point.position + glm::vec3(0.0f, 0.1f, 0.0f), 0.8f, color, 2.0f, 16);
            int out = 0;
            for (const FZombieHandle& handle : point.active)
            {
                if (const FZombieRuntime* member = systems_.zombies ? systems_.zombies->Resolve(handle) : nullptr)
                {
                    ++out;
                    paint.Line(point.position + glm::vec3(0.0f, 0.3f, 0.0f), member->position + glm::vec3(0.0f, 0.3f, 0.0f),
                               Style::Alpha(color, 0.4f), 1.0f);
                }
            }
            if (distance <= knobs_.labelDistance * 2.5f)
            {
                const double cooldown = std::max(0.0, point.cooldownUntil - now);
                paint.Label(point.position + glm::vec3(0.0f, 3.0f, 0.0f), color,
                            NextDayz::L10n::Format("S{} {} {}/{}  {}{}  {:.0f} m", index, ZombieDef(point.profile).id, out,
                                        point.groupSize, NextDayz::L10n::Tr(SpawnVerdictName(point.verdict)),
                                        cooldown > 0.0 ? NextDayz::L10n::Format(" {:.0f}s", cooldown) : std::string(), distance));
            }
        }
    }

    void NextDayzDebugger::OverlayLoot(FPainter& paint)
    {
        if (!systems_.loot)
        {
            return;
        }
        const LootSystem& loot = *systems_.loot;
        const glm::vec3 feet = PlayerFeet();
        const auto& entries = loot.Entries();
        for (size_t index = 0; index < entries.size(); ++index)
        {
            const FLootEntry& entry = entries[index];
            if (entry.state == FLootEntry::EState::Empty)
            {
                continue;
            }
            const float distance = glm::distance(entry.worldPos, feet);
            if (distance > knobs_.overlayDistance)
            {
                continue;
            }
            const FLootSlot* slot = loot.ResolveSlot(entry.directorHandle);
            glm::vec4 color = Style::LootCategory(entry.category);
            std::string state = NextDayz::L10n::Tr("Available");
            if (entry.state == FLootEntry::EState::Reserved)
            {
                color = Style::kWarn;
                state = NextDayz::L10n::Tr("Reserved");
            }
            else if (entry.state == FLootEntry::EState::Cooldown)
            {
                color = Style::Alpha(Style::kMuted, 0.8f);
                const double readyIn = std::max(0.0, loot.Director().ReadyAt(entry.directorHandle) - loot.WorldSeconds());
                state = NextDayz::L10n::Format("Cooldown {:.0f}s {}", readyIn, slot ? NextDayz::L10n::Tr(LootRespawnBlockName(slot->block)) : "");
            }
            const glm::vec3 top = entry.worldPos + glm::vec3(0.0f, 1.2f, 0.0f);
            paint.Line(entry.worldPos, top, color, 1.5f);
            glm::vec2 screen;
            if (!paint.Point(top, screen))
            {
                continue;
            }
            const ImVec2 c(screen.x, screen.y);
            paint.draw->AddQuadFilled(ImVec2(c.x, c.y - 5.0f), ImVec2(c.x + 5.0f, c.y), ImVec2(c.x, c.y + 5.0f),
                                      ImVec2(c.x - 5.0f, c.y), Style::U32(color));
            if (distance <= knobs_.labelDistance)
            {
                paint.LabelAt(screen, color,
                              NextDayz::L10n::Format("L{} {} | {} {} | {} | z{}", index,
                                          entry.displayName.empty() ? "-" : entry.displayName,
                                          NextDayz::L10n::Tr(LootProfileName(entry.profile)), NextDayz::L10n::Tr(LootCategoryName(entry.category)), state,
                                          entry.zone));
            }
        }
    }

    void NextDayzDebugger::OverlayLootZones(FPainter& paint)
    {
        if (!systems_.loot)
        {
            return;
        }
        const glm::vec3 feet = PlayerFeet();
        const auto& zones = systems_.loot->Zones();
        const float minimumDistance = systems_.loot->Director().Tuning().minimumPlayerDistance;
        for (size_t index = 0; index < zones.size(); ++index)
        {
            const FLootZone& zone = zones[index];
            const float edge = glm::length(glm::vec2(zone.center.x - feet.x, zone.center.z - feet.z)) - zone.radius;
            if (edge > knobs_.overlayDistance * 2.0f)
            {
                continue;
            }
            // Inside the zone radius + the minimum distance nothing in it restocks.
            const bool blocking = edge < minimumDistance;
            const glm::vec4 color = blocking ? Style::Alpha(Style::kWarn, 0.8f) : Style::Alpha(Style::kInfo, 0.6f);
            const glm::vec3 center(zone.center.x, feet.y + 0.5f, zone.center.z);
            Ring(paint, center, std::max(zone.radius, 2.0f), color, 2.0f, 48);
            Ring(paint, center, zone.radius + minimumDistance, Style::Alpha(color, 0.35f), 1.0f, 64);
            int available = 0;
            for (const FLootEntry& entry : systems_.loot->Entries())
            {
                available += entry.zone == static_cast<int>(index) && entry.state == FLootEntry::EState::Available ? 1 : 0;
            }
            paint.Label(center + glm::vec3(0.0f, 4.0f, 0.0f), color,
                        NextDayz::L10n::Format("zone {}  {}/{} stocked  r{:.0f}{}", index, available, zone.entries, zone.radius,
                                    blocking ? NextDayz::L10n::Tr("  (you are near: no restock)") : ""));
        }
    }

    void NextDayzDebugger::OverlayNav(FPainter& paint)
    {
        const NextGameplay::FNavGrid* grid = systems_.NavGrid();
        if (!grid || !grid->IsBuilt())
        {
            return;
        }
        const glm::vec3 feet = PlayerFeet();
        const float cell = grid->GetCellSize();
        const glm::vec3 origin = grid->GetWorldMin();
        const int centerX = static_cast<int>(std::floor((feet.x - origin.x) / cell));
        const int centerZ = static_cast<int>(std::floor((feet.z - origin.z) / cell));
        const int reach = static_cast<int>(std::ceil(knobs_.navRadius / cell));
        const glm::vec4 walkable = Style::Alpha(Style::kGood, 0.85f);
        const glm::vec4 blocked = Style::Alpha(Style::kBad, 0.8f);
        for (int gz = centerZ - reach; gz <= centerZ + reach; ++gz)
        {
            for (int gx = centerX - reach; gx <= centerX + reach; ++gx)
            {
                if (gx < 0 || gz < 0 || gx >= grid->GetWidth() || gz >= grid->GetHeight())
                {
                    continue;
                }
                glm::vec3 position = grid->GetCellWorldPosition(gx, gz);
                if (glm::length(glm::vec2(position.x - feet.x, position.z - feet.z)) > knobs_.navRadius)
                {
                    continue;
                }
                const bool open = grid->IsCellWalkable(gx, gz);
                float ground = 0.0f;
                position.y = (open || grid->SampleGroundHeight(position, ground)) ? position.y : feet.y;
                if (open)
                {
                    paint.Dot(position + glm::vec3(0.0f, 0.12f, 0.0f), walkable, 2.5f);
                }
                else
                {
                    Cross(paint, position + glm::vec3(0.0f, 0.12f, 0.0f), cell * 0.18f, blocked, 1.0f);
                }
            }
        }
        if (navProbePath_ && navProbePath_->size() > 1)
        {
            for (size_t index = 1; index < navProbePath_->size(); ++index)
            {
                paint.Line((*navProbePath_)[index - 1] + glm::vec3(0.0f, 0.3f, 0.0f),
                           (*navProbePath_)[index] + glm::vec3(0.0f, 0.3f, 0.0f), Style::kHover, 3.0f);
            }
        }
    }

    void NextDayzDebugger::OverlayAnchors(FPainter& paint, const FGameFacts& facts)
    {
        if (!systems_.anchors)
        {
            return;
        }
        const glm::vec3 feet = PlayerFeet();
        for (const FWorldAnchor& anchor : systems_.anchors->All())
        {
            const float distance = glm::distance(anchor.worldPos, feet);
            glm::vec4 color = Style::kMuted;
            std::string text;
            float height = 2.0f;
            switch (anchor.type)
            {
            case EWorldAnchorType::Poi:
                color = Style::kSelect;
                text = NextDayz::L10n::Format("{} ({})  {:.0f} m", anchor.label, anchor.profile, distance);
                height = 12.0f;
                break;
            case EWorldAnchorType::Well:
                color = Style::kInfo;
                text = NextDayz::L10n::Format("well  {:.0f} m", distance);
                height = 3.0f;
                break;
            case EWorldAnchorType::PlayerSafe:
                color = Style::kGood;
                text = NextDayz::L10n::Tr("player safe spawn");
                break;
            default:
                continue;   // loot and infected anchors have their own layers
            }
            if (anchor.type != EWorldAnchorType::Poi && distance > knobs_.overlayDistance)
            {
                continue;
            }
            paint.Line(anchor.worldPos, anchor.worldPos + glm::vec3(0.0f, height, 0.0f), color, 2.0f);
            if (anchor.type == EWorldAnchorType::Poi || distance <= knobs_.labelDistance * 2.0f)
            {
                paint.Label(anchor.worldPos + glm::vec3(0.0f, height, 0.0f), color, text);
            }
        }
        if (facts.cacheMarker)
        {
            Beam(paint, *facts.cacheMarker, Style::kWarn, 40.0f);
            paint.Label(*facts.cacheMarker + glm::vec3(0.0f, 6.0f, 0.0f), Style::kWarn,
                        NextDayz::L10n::Format("supply cache  {:.0f} m", glm::distance(*facts.cacheMarker, feet)));
        }
        for (const glm::vec3& cracker : facts.firecrackers)
        {
            Ring(paint, cracker + glm::vec3(0.0f, 0.1f, 0.0f), 1.0f, Style::kBad, 2.0f, 16);
            paint.Line(cracker, cracker + glm::vec3(0.0f, 2.0f, 0.0f), Style::kBad, 2.0f);
        }
    }

    void NextDayzDebugger::OverlayPlayer(FPainter& paint)
    {
        const glm::vec3 feet = PlayerFeet();
        const FPlayerLocomotionState& locomotion = systems_.player->LocomotionState();
        glm::vec3 velocity = locomotion.worldVelocity;
        velocity.y = 0.0f;
        if (glm::length(velocity) > 0.05f)
        {
            Arrow(paint, feet + glm::vec3(0.0f, 0.1f, 0.0f), feet + glm::vec3(0.0f, 0.1f, 0.0f) + velocity * 0.5f,
                  Style::kGood);
        }
        if (systems_.config)
        {
            Ring(paint, feet + glm::vec3(0.0f, 0.05f, 0.0f), systems_.config->Loot.ReachMeters,
                 Style::Alpha(Style::kInfo, 0.6f), 1.5f, 32);
        }
        // Where the eye actually looks (the hitscan origin), up to the first hit.
        const std::optional<FRayHit> hit = CrosshairHit(120.0f);
        if (hit)
        {
            Cross(paint, hit->point + hit->normal * 0.02f, 0.15f, Style::kSelect, 2.0f);
            paint.Label(hit->point, Style::kSelect,
                        NextDayz::L10n::Format("{:.1f} m  {}", hit->distance,
                                    commands_.nodeName ? commands_.nodeName(hit->instanceId) : std::string()));
        }
        if (systems_.loot && systems_.loot->Hovered())
        {
            Ring(paint, systems_.loot->Hovered()->worldPos + glm::vec3(0.0f, 0.05f, 0.0f), 0.5f, Style::kSelect, 2.5f, 16);
        }
    }

    void NextDayzDebugger::OverlaySelection(FPainter& paint)
    {
        const float pulse = 0.6f + 0.4f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
        const glm::vec4 color = Style::Alpha(Style::kSelect, pulse);
        std::optional<glm::vec3> where;
        if (const FZombieRuntime* zombie = SelectedZombie())
        {
            where = zombie->position;
        }
        else if (selection_.kind == ESelection::Loot && systems_.loot &&
                 selection_.index >= 0 && selection_.index < static_cast<int>(systems_.loot->Entries().size()))
        {
            where = systems_.loot->Entries()[selection_.index].worldPos;
        }
        else if (selection_.kind == ESelection::SpawnPoint && systems_.spawns && selection_.index >= 0 &&
                 selection_.index < static_cast<int>(systems_.spawns->Points().size()))
        {
            where = systems_.spawns->Points()[selection_.index].position;
        }
        if (where)
        {
            Ring(paint, *where + glm::vec3(0.0f, 0.08f, 0.0f), 0.9f, color, 3.0f, 24);
            Beam(paint, *where, color, 12.0f);
        }
        if (hoveredWorldPoint_)
        {
            Ring(paint, *hoveredWorldPoint_ + glm::vec3(0.0f, 0.08f, 0.0f), 1.2f, Style::kHover, 3.0f, 24);
            Beam(paint, *hoveredWorldPoint_, Style::kHover, 20.0f);
        }
    }
}
