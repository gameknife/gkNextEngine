#include "Engine/Common/CoreMinimal.hpp"

// The NextDayz debugger window: toolbar, warnings and one tab per system.

#include "NextDayzDebugger.hpp"

#include <cctype>
#include <chrono>
#include <numeric>

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>
#include <imgui.h>

#include "Gameplay/AI/NavGrid.h"

#include "Application/Game/NextDayz/Audio/NextDayzAudio.hpp"
#include "Application/Game/NextDayz/Combat/CombatEffects.hpp"
#include "Application/Game/NextDayz/Combat/CombatSystem.hpp"
#include "Application/Game/NextDayz/Combat/NoiseSystem.hpp"
#include "Application/Game/NextDayz/Data/ItemDefs.hpp"
#include "Application/Game/NextDayz/Debugger/DebugStyle.hpp"
#include "Application/Game/NextDayz/Inventory/LootSystem.hpp"
#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Player/PlayerActionController.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"
#include "Application/Game/NextDayz/Player/PlayerRigVisual.hpp"
#include "Application/Game/NextDayz/Player/SurvivalSystem.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponSystem.hpp"
#include "Application/Game/NextDayz/World/WorldAnchorRegistry.hpp"
#include "Application/Game/NextDayz/World/ZombieSpawnDirector.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieRigVisual.hpp"

namespace NextDayz::Debug
{
    namespace
    {
        constexpr float kKeyColumn = 150.0f;

        void Key(const char* key)
        {
            ImGui::TextDisabled("%s", key);
            ImGui::SameLine(kKeyColumn);
        }

        template <typename... Args>
        void KV(const char* key, fmt::format_string<Args...> format, Args&&... args)
        {
            Key(key);
            ImGui::TextUnformatted(fmt::format(format, std::forward<Args>(args)...).c_str());
        }

        void KVColored(const char* key, const glm::vec4& color, const std::string& text)
        {
            Key(key);
            ImGui::TextColored(Style::V4(color), "%s", text.c_str());
        }

        void Colored(const glm::vec4& color, const std::string& text)
        {
            ImGui::TextColored(Style::V4(color), "%s", text.c_str());
        }

        void Meter(const char* key, float value, float maximum, const glm::vec4& color, const std::string& overlay)
        {
            Key(key);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Style::V4(color));
            ImGui::ProgressBar(maximum > 0.0f ? std::clamp(value / maximum, 0.0f, 1.0f) : 0.0f, ImVec2(-1.0f, 0.0f),
                               overlay.c_str());
            ImGui::PopStyleColor();
        }

        template <size_t N>
        void Plot(const char* label, const TSeries<N>& series, float minimum, float maximum, const glm::vec4& color,
                  float height = 42.0f)
        {
            const std::string overlay = fmt::format("{} {:.1f}", label, series.Latest());
            ImGui::PushStyleColor(ImGuiCol_PlotLines, Style::V4(color));
            ImGui::PlotLines(fmt::format("##{}", label).c_str(), series.PlotValues(), series.PlotCount(),
                             series.PlotOffset(), overlay.c_str(), minimum, maximum, ImVec2(-1.0f, height));
            ImGui::PopStyleColor();
        }

        std::string MinutesSeconds(double seconds)
        {
            const int total = static_cast<int>(seconds);
            return fmt::format("{:02d}:{:02d}", total / 60, total % 60);
        }

        bool Contains(std::string_view text, std::string_view needle)
        {
            if (needle.empty())
            {
                return true;
            }
            const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
            return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                               [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
        }

        // InputText over a std::string without imgui_stdlib.
        bool InputString(const char* label, std::string& value, const char* hint = nullptr)
        {
            std::array<char, 128> buffer{};
            const size_t length = std::min(value.size(), buffer.size() - 1);
            std::copy_n(value.begin(), length, buffer.begin());
            const bool changed = hint ? ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.size())
                                      : ImGui::InputText(label, buffer.data(), buffer.size());
            if (changed)
            {
                value = buffer.data();
            }
            return changed;
        }

        constexpr std::array<const char*, 4> kProfiles = {"civilian", "industrial", "military", "wilderness"};
    }

    // ------------------------------------------------------------------ entry

    void NextDayzDebugger::Draw(const FGameFacts& facts)
    {
        ScopedTimer timer(profiler_, ETimer::Debugger);
        hoveredWorldPoint_.reset();
        if (!visible_)
        {
            return;
        }
        DrawWindow(facts);
        DrawOverlay(facts);
    }

    void NextDayzDebugger::DrawWindow(const FGameFacts& facts)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const bool interactive = cursorMode_ || facts.mouseFree;
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 12.0f, viewport->Pos.y + 12.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(590.0f, std::min(viewport->Size.y * 0.74f, 820.0f)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(440.0f, 220.0f), ImVec2(1200.0f, viewport->Size.y - 24.0f));
        ImGui::SetNextWindowBgAlpha(interactive ? 0.95f : 0.84f);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
        if (!interactive)
        {
            flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus;
        }
        if (ImGui::Begin("NextDayz Debug###NextDayzDebugger", nullptr, flags))
        {
            DrawToolbar(facts);
            DrawWarnings();
            if (ImGui::BeginTabBar("##debugTabs", ImGuiTabBarFlags_FittingPolicyScroll))
            {
                for (size_t index = 0; index < static_cast<size_t>(ETab::Count); ++index)
                {
                    const ETab tab = static_cast<ETab>(index);
                    const ImGuiTabItemFlags tabFlags =
                        requestedTab_ && *requestedTab_ == tab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                    if (!ImGui::BeginTabItem(TabName(tab), nullptr, tabFlags))
                    {
                        continue;
                    }
                    if (!requestedTab_)
                    {
                        tab_ = tab;
                    }
                    ImGui::BeginChild("##tabBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
                    switch (tab)
                    {
                    case ETab::Overview: TabOverview(facts); break;
                    case ETab::Player: TabPlayer(); break;
                    case ETab::Survival: TabSurvival(); break;
                    case ETab::Inventory: TabInventory(); break;
                    case ETab::Weapons: TabWeapons(); break;
                    case ETab::Infected: TabInfected(); break;
                    case ETab::Spawns: TabSpawns(); break;
                    case ETab::Loot: TabLoot(); break;
                    case ETab::World: TabWorld(facts); break;
                    case ETab::Combat: TabCombat(); break;
                    case ETab::Log: TabLog(); break;
                    case ETab::Count: break;
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            requestedTab_.reset();
        }
        ImGui::End();
    }

    void NextDayzDebugger::DrawToolbar(const FGameFacts& facts)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const FTimerStat& tick = profiler_.Stat(ETimer::Tick);
        const std::string gameClock = systems_.time
            ? fmt::format("{:02d}:{:02d} {}", systems_.time->HourInt(), systems_.time->MinuteInt(),
                          WeatherName(systems_.time->Weather()))
            : std::string("-");
        ImGui::Text("%.0f fps  %.1f ms | tick %.2f ms | sim %.1f s | life %s | %s", io.Framerate,
                    io.DeltaTime * 1000.0f, tick.averageMs, clock_, MinutesSeconds(facts.survivalSeconds).c_str(), gameClock.c_str());
        if (knobs_.frozen || facts.paused)
        {
            ImGui::SameLine();
            Colored(Style::kWarn, knobs_.frozen ? "  FROZEN" : "  PAUSED");
        }

        bool cursor = cursorMode_;
        if (ImGui::Checkbox("Cursor (F9)", &cursor))
        {
            SetCursorMode(cursor);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Freeze (F10)", &knobs_.frozen);
        ImGui::SameLine();
        if (ImGui::Button("Step"))
        {
            knobs_.frozen = true;
            ++knobs_.stepFrames;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("##timeScale", &knobs_.timeScale, 0.0f, 4.0f, "time x%.2f");
        for (const float preset : {0.25f, 1.0f, 3.0f})
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(fmt::format("{}x", preset).c_str()))
            {
                knobs_.timeScale = preset;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Snapshot"))
        {
            WriteSnapshot();
        }

        ImGui::TextDisabled("Selected:");
        ImGui::SameLine();
        ImGui::TextUnformatted(SelectionLabel().c_str());
        if (selection_.kind != ESelection::None)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
            {
                ClearSelection();
            }
        }
        if (!cursorMode_ && !facts.mouseFree)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("  F9: free the cursor to use this window / click things in the world");
        }
        else if (cursorMode_)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("  click in the world to select | PgUp/PgDn tabs");
        }
    }

    void NextDayzDebugger::DrawWarnings()
    {
        if (warnings_.empty())
        {
            return;
        }
        const size_t shown = std::min<size_t>(warnings_.size(), 6);
        for (size_t index = 0; index < shown; ++index)
        {
            const FWarning& warning = warnings_[index];
            Colored(warning.severity >= 2 ? Style::kBad : Style::kWarn,
                    fmt::format("{} {}", warning.severity >= 2 ? "ERROR" : "WARN ", warning.text));
        }
        if (warnings_.size() > shown)
        {
            ImGui::TextDisabled("... %zu more (see snapshot)", warnings_.size() - shown);
        }
    }

    void NextDayzDebugger::LayerToggles()
    {
        ImGui::SeparatorText("World overlay layers (F6 infected, F7 loot, F8 hits)");
        int column = 0;
        for (const FLayerInfo& info : AllLayers())
        {
            bool on = LayerOn(info.layer);
            if (ImGui::Checkbox(info.label, &on))
            {
                SetLayer(info.layer, on);
            }
            if (++column % 4 != 0)
            {
                ImGui::SameLine(static_cast<float>(column % 4) * 135.0f + 8.0f);
            }
        }
        if (column % 4 != 0)
        {
            ImGui::NewLine();
        }
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("##overlayDistance", &knobs_.overlayDistance, 20.0f, 600.0f, "markers < %.0f m");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("##labelDistance", &knobs_.labelDistance, 5.0f, 200.0f, "labels < %.0f m");
        ImGui::TextDisabled("cvar: nextdayz.debug.layers = \"%s\"", LayerString().c_str());
    }

    // ------------------------------------------------------------------ overview

    void NextDayzDebugger::TabOverview(const FGameFacts& facts)
    {
        if (ImGui::BeginTable("##systems", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                  ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("System", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 18.0f);
            ImGui::TableHeadersRow();
            const auto row = [](const char* name, const std::string& text, const glm::vec4& status)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(text.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(Style::V4(status), "*");
            };
            if (systems_.player && systems_.player->IsValid())
            {
                const FPlayerLocomotionState& locomotion = systems_.player->LocomotionState();
                const glm::vec3 position = systems_.player->Position();
                row("Player", fmt::format("({:.0f}, {:.1f}, {:.0f})  {} {}  {:.1f} m/s  {}", position.x, position.y,
                                          position.z, StanceName(locomotion.actualStance), GaitName(locomotion.gait),
                                          locomotion.horizontalSpeed, systems_.player->IsFirstPerson() ? "FPS" : "TPS"),
                    locomotion.onGround ? Style::kGood : Style::kInfo);
            }
            if (systems_.survival)
            {
                const FSurvivalSnapshot& state = systems_.survival->Snapshot();
                row("Survival",
                    fmt::format("HP {:.0f}  food {:.0f}  water {:.0f}  cold {:.2f}  bleed {}{}", state.health, state.hunger,
                                state.hydration, state.cold, state.bleedingWounds, systems_.survival->IsWeak() ? "  WEAK" : ""),
                    !systems_.survival->IsAlive() ? Style::kBad
                    : state.bleedingWounds > 0 || state.health < 35.0f ? Style::kWarn : Style::kGood);
            }
            if (systems_.weapons && systems_.inventory)
            {
                row("Weapons",
                    systems_.weapons->HasActiveWeapon()
                        ? fmt::format("{}  {} / {}  {}", systems_.weapons->ActiveDisplayName(), systems_.weapons->AmmoInMag(),
                                      systems_.weapons->AmmoReserve(*systems_.inventory),
                                      WeaponActionName(systems_.weapons->PresentationAction()))
                        : std::string("unarmed"),
                    systems_.weapons->HasActiveWeapon() ? Style::kGood : Style::kMuted);
            }
            if (systems_.zombies && systems_.spawns)
            {
                row("Infected",
                    fmt::format("{} / {} active  {} alerted  {} kills  paths {}+{} deferred",
                                systems_.zombies->ActiveCount(), systems_.spawns->GlobalCap(),
                                systems_.zombies->AlertedCount(), systems_.zombies->KillCount(),
                                systems_.zombies->PathSolvesLastUpdate(), systems_.zombies->PathDeferredLastUpdate()),
                    systems_.zombies->AlertedCount() > 0 ? Style::kWarn : Style::kGood);
                row("Spawns",
                    fmt::format("{} points  {} spawned  {} recycled  {} rejected visible",
                                systems_.spawns->Points().size(), systems_.spawns->SpawnCount(),
                                systems_.spawns->RecycledCount(), systems_.spawns->RejectedVisible()),
                    systems_.spawns->Points().empty() ? Style::kBad : Style::kGood);
            }
            if (systems_.loot)
            {
                const LootSystem& loot = *systems_.loot;
                row("Loot",
                    fmt::format("{} available  {} cooling  {} entries / {} zones{}{}", loot.RemainingCount(),
                                loot.CooldownCount(), loot.Entries().size(), loot.Zones().size(),
                                loot.FoodScarce() ? "  food-scarce" : "", loot.MedicalScarce() ? "  med-scarce" : ""),
                    loot.RemainingCount() > 0 ? Style::kGood : Style::kBad);
            }
            if (systems_.time && systems_.zombies)
            {
                row("World",
                    fmt::format("{:02d}:{:02d} {}  daylight {:.2f}  chill {:.2f}  sight x{:.2f}", systems_.time->HourInt(),
                                systems_.time->MinuteInt(), WeatherName(systems_.time->Weather()),
                                systems_.time->Daylight(), systems_.time->Chill(), systems_.zombies->SightScale()),
                    Style::kGood);
            }
            if (systems_.noise && systems_.effects && systems_.audio)
            {
                row("Noise/FX",
                    fmt::format("{} noises  {} particles  {} tracers  {} sounds played", systems_.noise->Events().size(),
                                systems_.effects->ActiveParticles(), systems_.effects->ActiveTracers(),
                                systems_.audio->PlayedCount()),
                    Style::kGood);
            }
            if (systems_.navGrid)
            {
                row("Nav grid",
                    systems_.navGrid->IsBuilt()
                        ? fmt::format("{} x {} cells at {:.1f} m", systems_.navGrid->GetWidth(),
                                      systems_.navGrid->GetHeight(), systems_.navGrid->GetCellSize())
                        : std::string("NOT BUILT"),
                    systems_.navGrid->IsBuilt() ? Style::kGood : Style::kBad);
            }
            row("Assets",
                fmt::format("soldier rig {}  infected rig {}  loot catalog {}", facts.soldierRigLoaded ? "ok" : "MISSING",
                            facts.zombieRigLoaded ? "ok" : "MISSING", facts.lootCatalogLoaded ? "ok" : "MISSING"),
                facts.soldierRigLoaded && facts.zombieRigLoaded && facts.lootCatalogLoaded ? Style::kGood : Style::kBad);
            row("Objective", facts.objective, Style::kInfo);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("CPU per system (ms: last / avg / peak)");
        if (ImGui::BeginTable("##timers", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Timer", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("share of 16.6 ms");
            for (size_t index = 0; index < kTimerCount; ++index)
            {
                const FTimerStat& stat = profiler_.Stat(static_cast<ETimer>(index));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(TimerName(static_cast<ETimer>(index)));
                ImGui::TableNextColumn();
                ImGui::Text("%.2f / %.2f / %.2f", stat.lastMs, stat.averageMs, stat.peakMs);
                ImGui::TableNextColumn();
                const glm::vec4 color = stat.averageMs > 4.0f ? Style::kBad : stat.averageMs > 1.5f ? Style::kWarn : Style::kGood;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Style::V4(color));
                ImGui::ProgressBar(std::clamp(stat.averageMs / 16.6f, 0.0f, 1.0f), ImVec2(-1.0f, 10.0f), "");
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Last 60 s");
        Plot("frame ms", history_.frameMs, 0.0f, 40.0f, Style::kInfo);
        Plot("tick ms", history_.tickMs, 0.0f, 8.0f, Style::kWarn);
        Plot("infected active", history_.infectedActive, 0.0f, 60.0f, Style::kGood);
        Plot("infected alerted", history_.infectedAlerted, 0.0f, 20.0f, Style::ZombieState(EZombieState::Chase));
        Plot("path solves / 0.25 s", history_.pathSolves, 0.0f, 40.0f, Style::kPath);
        Plot("loot available", history_.lootAvailable, 0.0f, std::max(10.0f, history_.lootAvailable.Max() * 1.2f),
             Style::LootCategory(ELootCategory::FoodWater));

        LayerToggles();
    }

    // ------------------------------------------------------------------ player

    void NextDayzDebugger::TabPlayer()
    {
        PlayerController* player = systems_.player;
        if (!player || !player->IsValid())
        {
            ImGui::TextDisabled("No player");
            return;
        }
        const FPlayerLocomotionState& locomotion = player->LocomotionState();
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const glm::vec3 position = player->Position();
            const glm::vec3 eye = player->EyePosition();
            KV("View", "{}{}", player->IsFirstPerson() ? "first person" : "third person", knobs_.orbit ? " (orbit debug)" : "");
            KV("Feet", "{:.2f}  {:.2f}  {:.2f}   map ({:.0f}, {:.0f})", position.x, position.y, position.z, position.x,
               -position.z);
            KV("Eye", "{:.2f}  {:.2f}  {:.2f}", eye.x, eye.y, eye.z);
            KV("Yaw / pitch", "{:.1f} / {:.1f} deg", glm::degrees(player->Yaw()), glm::degrees(player->Pitch()));
            KV("FOV", "{:.1f} deg", player->CurrentFov());
            KV("Camera recoil", "{:.2f} / {:.2f} deg{}", glm::degrees(player->CameraRecoil().x),
               glm::degrees(player->CameraRecoil().y), player->RecoilActive() ? "  active" : "");
        }
        if (ImGui::CollapsingHeader("Locomotion", ImGuiTreeNodeFlags_DefaultOpen))
        {
            KV("Stance", "{} -> {}{}", StanceName(locomotion.actualStance), StanceName(locomotion.desiredStance),
               locomotion.standBlocked ? "  STAND BLOCKED" : "");
            KV("Gait", "{}  sprint alpha {:.2f}{}", GaitName(locomotion.gait), player->SprintAlpha(),
               player->WeaponReady() ? "" : "  (weapon lowered)");
            KV("Speed", "{:.2f} m/s  vel ({:.2f}, {:.2f}, {:.2f})", locomotion.horizontalSpeed, locomotion.worldVelocity.x,
               locomotion.worldVelocity.y, locomotion.worldVelocity.z);
            KV("Move input", "{:.2f}  {:.2f}", locomotion.localMove.x, locomotion.localMove.y);
            KV("Grounded", "{}  capsule {:.2f} m", locomotion.onGround ? "yes" : "NO", player->ControllerHeight());
            KV("Jump", "{} {:.2f}", JumpPhaseName(locomotion.jumpPhase), locomotion.jumpPhaseTime01);
            KV("Traversal", "{} {:.2f}  height {:.2f} m", TraversalActionName(locomotion.traversalAction),
               locomotion.traversalTime01, locomotion.traversalHeight);
            KV("Probe", "{}", player->TraversalProbeResult());
        }
        if (systems_.rig && ImGui::CollapsingHeader("Animation"))
        {
            const PlayerRigVisual& rig = *systems_.rig;
            KV("Base clip", "{}", rig.CurrentBaseClipName().empty() ? "-" : rig.CurrentBaseClipName());
            KV("Aim weight", "{:.2f}", rig.AimWeight());
            KV("Body yaw lag", "{:.1f} deg{}",
               glm::degrees(std::remainder(player->Yaw() - rig.BodyYaw(), glm::two_pi<float>())),
               rig.TurningInPlace() ? "  turning in place" : "");
            KV("Lean roll", "{:.1f} deg", rig.LeanRollDegrees());
            KV("Grip error", "{:.3f} m", rig.GripError());
            if (systems_.actions)
            {
                KV("Action", "{} {:.2f}{}", ActionName(systems_.actions->Action()), systems_.actions->NormalizedTime(),
                   systems_.actions->IsCommitted() ? " committed" : "");
            }
            KV("Weapon layer", "{} w {:.2f}", rig.CurrentWeaponActionClipName().empty() ? "-" : rig.CurrentWeaponActionClipName(),
               rig.WeaponActionWeight());
            KV("Recoil", "rig {}  view {}", rig.RecoilActive() ? "on" : "off",
               systems_.weapons && systems_.weapons->ViewModelRecoilActive() ? "on" : "off");
        }
        if (ImGui::CollapsingHeader("Debug camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Orbit camera", &knobs_.orbit);
            ImGui::SameLine();
            ImGui::Checkbox("Orbit the focus infected", &knobs_.orbitZombie);
            ImGui::SameLine();
            if (ImGui::Button(player->IsFirstPerson() ? "Third person" : "First person"))
            {
                player->ToggleView();
            }
            ImGui::SliderFloat("Orbit yaw", &knobs_.orbitYaw, -180.0f, 180.0f, "%.0f deg");
            ImGui::SliderFloat("Orbit distance", &knobs_.orbitDistance, 0.5f, 20.0f, "%.1f m");
            ImGui::SliderFloat("Orbit height", &knobs_.orbitHeight, 0.0f, 3.0f, "%.2f m");
        }
        if (ImGui::CollapsingHeader("Cheats and teleport", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("God mode (infected blows do no damage)", &knobs_.god);
            ImGui::Checkbox("Freeze needs (no hunger, thirst, bleeding, cold)", &knobs_.freezeNeeds);
            if (ImGui::Button("Teleport to crosshair") && commands_.teleport)
            {
                if (const std::optional<FRayHit> hit = CrosshairHit(400.0f))
                {
                    commands_.teleport(hit->point + hit->normal * 0.3f);
                }
            }
            if (systems_.anchors && commands_.teleport)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::BeginCombo("##poi", "Teleport to POI..."))
                {
                    for (const FWorldAnchor& anchor : systems_.anchors->All())
                    {
                        if (anchor.type == EWorldAnchorType::Poi &&
                            ImGui::Selectable(fmt::format("{} ({})", anchor.label, anchor.profile).c_str()))
                        {
                            commands_.teleport(anchor.worldPos);
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
    }

    // ------------------------------------------------------------------ survival

    void NextDayzDebugger::TabSurvival()
    {
        SurvivalSystem* survival = systems_.survival;
        if (!survival)
        {
            return;
        }
        const FSurvivalSnapshot& state = survival->Snapshot();
        const auto rate = [this](const FSeries& series) { return RatePerMinute(series); };
        Meter("Health", state.health, 100.0f, Style::kBad,
              fmt::format("{:.1f}  ({:+.2f}/min)", state.health, rate(history_.health)));
        Meter("Hunger", state.hunger, 100.0f, Style::kWarn,
              fmt::format("{:.1f}  ({:+.2f}/min)", state.hunger, rate(history_.hunger)));
        Meter("Hydration", state.hydration, 100.0f, Style::kInfo,
              fmt::format("{:.1f}  ({:+.2f}/min)", state.hydration, rate(history_.hydration)));
        Meter("Cold", state.cold, 1.0f, Style::Channel(EChannel::World),
              fmt::format("{:.2f}  chill {:.2f} - warmth {:.2f}", state.cold, systems_.time ? systems_.time->Chill() : 0.0f,
                          systems_.inventory ? systems_.inventory->Warmth() : 0.0f));
        KVColored("Life", survival->IsAlive() ? Style::kGood : Style::kBad,
                  survival->IsAlive() ? std::string("alive") : fmt::format("DEAD ({})", state.lastDamageSource));
        KVColored("Bleeding", state.bleedingWounds > 0 ? Style::kBad : Style::kMuted,
                  fmt::format("{} wound(s)", state.bleedingWounds));
        KV("Flags", "{}{}{}", survival->IsWeak() ? "WEAK  " : "", state.healing ? "healing  " : "",
           state.painkillerSeconds > 0.0f ? fmt::format("painkillers {:.0f}s", state.painkillerSeconds) : std::string());
        KV("Armor / warmth", "{:.0f}% / {:.2f}", systems_.inventory ? systems_.inventory->Armor() * 100.0f : 0.0f,
           systems_.inventory ? systems_.inventory->Warmth() : 0.0f);
        KV("Last damage", "{} (seq {})", state.lastDamageSource.empty() ? "-" : state.lastDamageSource, state.damageSequence);

        ImGui::SeparatorText("Last 60 s");
        Plot("health", history_.health, 0.0f, 100.0f, Style::kBad);
        Plot("hunger", history_.hunger, 0.0f, 100.0f, Style::kWarn);
        Plot("hydration", history_.hydration, 0.0f, 100.0f, Style::kInfo);
        Plot("cold x100", history_.cold, 0.0f, 100.0f, Style::Channel(EChannel::World));

        ImGui::SeparatorText("Set");
        float health = state.health;
        float hunger = state.hunger;
        float hydration = state.hydration;
        bool changed = ImGui::SliderFloat("Health##set", &health, 1.0f, 100.0f, "%.0f");
        changed |= ImGui::SliderFloat("Hunger##set", &hunger, 0.0f, 100.0f, "%.0f");
        changed |= ImGui::SliderFloat("Hydration##set", &hydration, 0.0f, 100.0f, "%.0f");
        if (changed && survival->IsAlive())
        {
            survival->SetNeeds(health, hunger, hydration);
        }
        int wounds = state.bleedingWounds;
        if (ImGui::SliderInt("Bleeding wounds", &wounds, 0, 3))
        {
            survival->SetBleeding(wounds);
        }
        if (ImGui::Button("Restore all"))
        {
            survival->SetNeeds(100.0f, 100.0f, 100.0f);
            survival->SetBleeding(0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Starving"))
        {
            survival->SetNeeds(state.health, 8.0f, state.hydration);
        }
        ImGui::SameLine();
        if (ImGui::Button("Parched"))
        {
            survival->SetNeeds(state.health, state.hunger, 8.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Hurt 25"))
        {
            survival->ApplyDamage(25.0f, "debug", false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Kill"))
        {
            survival->ApplyDamage(1000.0f, "debug", false);
        }
        ImGui::SameLine();
        if (ImGui::Button("New life") && commands_.resetSession)
        {
            commands_.resetSession();
        }
        ImGui::Checkbox("God mode", &knobs_.god);
        ImGui::SameLine();
        ImGui::Checkbox("Freeze needs", &knobs_.freezeNeeds);
    }

    // ------------------------------------------------------------------ inventory

    void NextDayzDebugger::TabInventory()
    {
        Inventory* inventory = systems_.inventory;
        if (!inventory)
        {
            return;
        }
        KV("Capacity", "{} / {} used", inventory->UsedCapacity(), inventory->TotalCapacity());
        KV("Armor / warmth", "{:.0f}% / {:.2f}", inventory->Armor() * 100.0f, inventory->Warmth());
        KV("Tools", "{}{}{}", inventory->HasTool(EItemTool::Map) ? "map  " : "",
           inventory->HasTool(EItemTool::Compass) ? "compass  " : "", inventory->CanOpenCans() ? "can opener" : "");

        if (ImGui::BeginTable("##items", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                              ImVec2(0.0f, 260.0f)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Container", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Item");
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Inst", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 86.0f);
            ImGui::TableHeadersRow();
            for (const FItemInstance& instance : inventory->Instances())
            {
                ImGui::PushID(static_cast<int>(instance.instanceId));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool worn = inventory->IsWorn(instance.instanceId);
                const FInventoryContainer* container = inventory->FindContainer(instance.containerId);
                ImGui::TextUnformatted(worn ? "worn" : container ? container->displayName.c_str() : "?");
                ImGui::TableNextColumn();
                const FItemDef* definition = FindItemDef(instance.defId);
                ImGui::Text("%s%s", definition ? std::string(definition->displayName).c_str() : instance.defId.c_str(),
                            instance.loadedAmmo > 0 ? fmt::format("  [{} loaded]", instance.loadedAmmo).c_str() : "");
                ImGui::TableNextColumn();
                ImGui::Text("%d", instance.count);
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(instance.instanceId));
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Use") && commands_.useItem)
                {
                    commands_.useItem(instance.instanceId);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Drop") && commands_.dropItem)
                {
                    commands_.dropItem(instance.instanceId);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        for (const FInventoryContainer& container : inventory->Containers())
        {
            if (container.active)
            {
                ImGui::TextDisabled("%s: %d / %d%s", container.displayName.c_str(), inventory->ContainerUsed(container.containerId),
                                    container.capacity, container.weaponOnly ? " (weapons)" : "");
            }
        }

        ImGui::SeparatorText("Give");
        const std::span<const FItemDef> definitions = AllItemDefs();
        giveItemIndex_ = std::clamp(giveItemIndex_, 0, static_cast<int>(definitions.size()) - 1);
        ImGui::SetNextItemWidth(140.0f);
        InputString("##itemFilter", itemFilter_, "filter");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        const FItemDef& current = definitions[giveItemIndex_];
        if (ImGui::BeginCombo("##giveItem", fmt::format("{} ({})", current.displayName, current.id).c_str()))
        {
            for (int index = 0; index < static_cast<int>(definitions.size()); ++index)
            {
                const FItemDef& definition = definitions[index];
                if (!Contains(definition.id, itemFilter_) && !Contains(definition.displayName, itemFilter_))
                {
                    continue;
                }
                if (ImGui::Selectable(fmt::format("{} ({})", definition.displayName, definition.id).c_str(),
                                      index == giveItemIndex_))
                {
                    giveItemIndex_ = index;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::InputInt("##giveCount", &giveCount_);
        giveCount_ = std::clamp(giveCount_, 1, 500);
        ImGui::SameLine();
        if (ImGui::Button("Give") && commands_.giveItem)
        {
            commands_.giveItem(std::string(current.id), giveCount_);
        }
        const auto kit = [this](const char* label, std::initializer_list<std::pair<const char*, int>> items)
        {
            if (ImGui::Button(label) && commands_.giveItem)
            {
                for (const auto& [id, count] : items)
                {
                    commands_.giveItem(id, count);
                }
            }
        };
        kit("Backpack", {{"backpack", 1}});
        ImGui::SameLine();
        kit("Supplies", {{"food_can", 2}, {"water_bottle", 2}, {"bandage", 3}});
        ImGui::SameLine();
        kit("AK + ammo", {{"ak", 1}, {"ammo_545", 90}});
        ImGui::SameLine();
        kit("Axe", {{"axe", 1}});
        ImGui::SameLine();
        kit("Map + compass", {{"map", 1}, {"compass", 1}});
        ImGui::TextDisabled("Given items that do not fit are dropped; Use equips weapons and wears clothes.");
    }

    // ------------------------------------------------------------------ weapons

    void NextDayzDebugger::TabWeapons()
    {
        WeaponSystem* weapons = systems_.weapons;
        if (!weapons || !systems_.inventory)
        {
            return;
        }
        const FWeaponDef* active = weapons->ActiveWeapon();
        KV("Slots", "1: {}  2: {}  (active {})", weapons->SlotWeaponId(0).empty() ? "-" : weapons->SlotWeaponId(0),
           weapons->SlotWeaponId(1).empty() ? "-" : weapons->SlotWeaponId(1), weapons->ActiveSlot() + 1);
        KV("Ammo", "{} in mag / {} reserve", weapons->AmmoInMag(), weapons->AmmoReserve(*systems_.inventory));
        KV("Action", "{} {:.2f}{}{}", WeaponActionName(weapons->PresentationAction()), weapons->PresentationActionTime(),
           weapons->IsReloading() ? "  reloading" : "",
           weapons->IsSwitching() ? fmt::format("  switching to {} ({})", weapons->SwitchTargetSlot() + 1,
                                                weapons->SwitchCommitted() ? "committed" : "holstering")
                                  : std::string());
        KV("Trigger", "{}  shots fired {}", weapons->TriggerDown() ? "DOWN" : "up", weapons->LastShotSequence());
        if (active)
        {
            ImGui::SeparatorText(fmt::format("{} ({})", active->displayName, active->id).c_str());
            if (active->IsMelee())
            {
                KV("Melee", "range {:.1f} m  swing {:.2f} s", active->meleeRange, active->fireInterval);
            }
            else
            {
                KV("Magazine", "{} rounds of {}  {} rpm{}", active->magSize, AmmoDisplayName(active->ammo),
                   static_cast<int>(60.0f / std::max(active->fireInterval, 0.001f)), active->fullAuto ? "  auto" : "");
                KV("Spread", "hip {:.2f} deg  ads {:.2f} deg", glm::degrees(active->spreadHip),
                   glm::degrees(active->spreadAds));
                KV("Pellets", "{}", active->pellets);
            }
            KV("Damage", "{:.0f}  falloff {:.0f} m  impact x{:.2f}", active->baseDamage, active->damageFalloffDistance,
               active->impactScale);
            KV("Noise", "{:.0f} m", active->noiseRadius);
            if (!active->IsMelee() && ImGui::Button("Give 2 magazines of ammo") && commands_.giveItem)
            {
                commands_.giveItem(std::string(AmmoItemId(active->ammo)), active->magSize * 2);
            }
        }
        ImGui::SeparatorText("Equip");
        for (const FWeaponDef& definition : kWeapons)
        {
            if (ImGui::SmallButton(std::string(definition.displayName).c_str()) && commands_.giveItem && commands_.useItem)
            {
                commands_.giveItem(std::string(definition.id), 1);
                if (const FItemInstance* instance = systems_.inventory->FindFirst(definition.id))
                {
                    commands_.useItem(instance->instanceId);
                }
                if (!definition.IsMelee())
                {
                    commands_.giveItem(std::string(AmmoItemId(definition.ammo)), definition.magSize * 3);
                }
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();

        ImGui::SeparatorText("Recent traces (F8 draws them)");
        if (ImGui::BeginTable("##traces", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                                 ImGuiTableFlags_ScrollY, ImVec2(0.0f, 220.0f)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Shot", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupColumn("Hit");
            ImGui::TableHeadersRow();
            for (auto it = traces_.rbegin(); it != traces_.rend(); ++it)
            {
                const FWeaponTraceEvent& trace = it->trace;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%llu.%d", static_cast<unsigned long long>(trace.sequence), trace.pellet);
                ImGui::TableNextColumn();
                Colored(trace.hitZombie ? Style::kGood : trace.hit ? Style::kBad : Style::kWarn,
                        trace.hitZombie ? "infected" : trace.hit ? "world" : "miss");
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", glm::distance(trace.origin, trace.endPoint));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(trace.hit && !trace.hitZombie && commands_.nodeName
                                           ? commands_.nodeName(trace.hitInstanceId).c_str()
                                           : trace.melee ? "melee" : "");
                if (ImGui::IsItemHovered())
                {
                    hoveredWorldPoint_ = trace.endPoint;
                }
            }
            ImGui::EndTable();
        }
    }

    // ------------------------------------------------------------------ infected

    void NextDayzDebugger::TabInfected()
    {
        ZombieSystem* zombies = systems_.zombies;
        if (!zombies)
        {
            return;
        }
        const glm::vec3 feet = PlayerFeet();
        KV("Active", "{} / cap {}  ({} slots)", zombies->ActiveCount(), systems_.spawns ? systems_.spawns->GlobalCap() : 0,
           zombies->Capacity());
        KV("Alerted / kills", "{} / {}  headshots {}  staggers {}", zombies->AlertedCount(), zombies->KillCount(),
           systems_.combat ? systems_.combat->HeadshotCount() : 0, zombies->StaggerCount());
        KV("Perception", "sight x{:.2f} (night, crouch, fog, lantern)", zombies->SightScale());
        KV("Routing", "{} solves, {} deferred last update (budget {}, {} expansions)", zombies->PathSolvesLastUpdate(),
           zombies->PathDeferredLastUpdate(), zombies->Config().MaxPathSolvesPerUpdate,
           systems_.config ? systems_.config->Zombie.MaxPathExpansions : 0);

        // State histogram.
        std::array<int, kZombieStateCount> states{};
        for (const FZombieRuntime& zombie : zombies->Slots())
        {
            states[static_cast<size_t>(zombie.state)] += zombie.active ? 1 : 0;
        }
        for (size_t index = 0; index < states.size(); ++index)
        {
            if (index > 0)
            {
                ImGui::SameLine();
            }
            Colored(Style::ZombieState(static_cast<EZombieState>(index)),
                    fmt::format("{} {}", ZombieStateName(static_cast<EZombieState>(index)), states[index]));
        }

        ImGui::SeparatorText("Control");
        ImGui::SetNextItemWidth(170.0f);
        ImGui::SliderFloat("##zombieTime", &knobs_.zombieTimeScale, 0.0f, 2.0f, "AI time x%.2f");
        ImGui::SameLine();
        if (ImGui::SmallButton(knobs_.zombieTimeScale > 0.0f ? "Freeze AI" : "Resume AI"))
        {
            knobs_.zombieTimeScale = knobs_.zombieTimeScale > 0.0f ? 0.0f : 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Wake all (noise)") && systems_.noise)
        {
            systems_.noise->Emit(feet, 400.0f, 2.0f, ENoiseType::Rifle, 1.5f);
        }
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo("##profile", &spawnProfile_, kProfiles.data(), static_cast<int>(kProfiles.size()));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::SliderInt("##count", &spawnCount_, 1, 12, "x%d");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("##distance", &spawnDistance_, 1.5f, 60.0f, "%.0f m ahead");
        ImGui::SameLine();
        const auto spawnAt = [this](const glm::vec3& center)
        {
            if (!commands_.spawnZombie)
            {
                return;
            }
            FZombieHandle last{};
            for (int index = 0; index < spawnCount_; ++index)
            {
                const float angle = static_cast<float>(index) * 2.39996f;
                const float radius = index == 0 ? 0.0f : 0.9f * std::sqrt(static_cast<float>(index));
                const glm::vec3 offset(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
                const FZombieHandle handle =
                    commands_.spawnZombie(static_cast<EZombieProfile>(spawnProfile_), center + offset);
                last = handle.IsValid() ? handle : last;
            }
            if (last.IsValid())
            {
                SelectZombie(last);
            }
        };
        if (ImGui::Button("Spawn ahead"))
        {
            glm::vec3 ahead = PlayerForward();
            ahead.y = 0.0f;
            ahead = glm::length(ahead) > 1.0e-3f ? glm::normalize(ahead) : glm::vec3(0.0f, 0.0f, 1.0f);
            spawnAt(feet + ahead * spawnDistance_);
        }
        ImGui::SameLine();
        if (ImGui::Button("At crosshair"))
        {
            if (const std::optional<FRayHit> hit = CrosshairHit(300.0f))
            {
                spawnAt(hit->point);
            }
        }
        if (ImGui::Button("Kill all") && commands_.killZombie)
        {
            for (const FZombieRuntime& zombie : zombies->Slots())
            {
                if (zombie.active && zombie.state != EZombieState::Dead)
                {
                    commands_.killZombie(zombie.handle);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Despawn all"))
        {
            for (const FZombieRuntime& zombie : zombies->Slots())
            {
                if (zombie.active)
                {
                    zombies->Recycle(zombie.handle);
                }
            }
            if (commands_.syncZombieVisuals) commands_.syncZombieVisuals();
        }
        ImGui::SameLine();
        ImGui::Checkbox("List corpses", &infectedShowDead_);

        if (const FZombieRuntime* selected = SelectedZombie())
        {
            ImGui::SeparatorText(fmt::format("Selected {}", ZombieTag(*selected)).c_str());
            InfectedDetail(*selected);
        }

        ImGui::SeparatorText("Infected by distance (click to select, hover to locate)");
        std::vector<const FZombieRuntime*> listed;
        for (const FZombieRuntime& zombie : zombies->Slots())
        {
            if (zombie.active && (infectedShowDead_ || zombie.state != EZombieState::Dead))
            {
                listed.push_back(&zombie);
            }
        }
        std::sort(listed.begin(), listed.end(), [feet](const FZombieRuntime* a, const FZombieRuntime* b)
        {
            return glm::distance(a->position, feet) < glm::distance(b->position, feet);
        });
        if (ImGui::BeginTable("##infected", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                                   ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                              ImVec2(0.0f, 320.0f)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Flags");
            ImGui::TableHeadersRow();
            for (const FZombieRuntime* zombie : listed)
            {
                ImGui::PushID(static_cast<int>(zombie->handle.index));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool isSelected = selection_.kind == ESelection::Infected && selection_.zombie == zombie->handle;
                if (ImGui::Selectable(fmt::format("Z{}:{}", zombie->handle.index, zombie->handle.generation).c_str(),
                                      isSelected, ImGuiSelectableFlags_SpanAllColumns))
                {
                    SelectZombie(zombie->handle);
                }
                if (ImGui::IsItemHovered())
                {
                    hoveredWorldPoint_ = zombie->position;
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(std::string(ZombieDef(zombie->profile).id).c_str());
                ImGui::TableNextColumn();
                Colored(Style::ZombieState(zombie->state), ZombieStateName(zombie->state));
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", zombie->health);
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", glm::distance(zombie->position, feet));
                ImGui::TableNextColumn();
                const size_t left = zombie->pathWaypoint < zombie->path.size() ? zombie->path.size() - zombie->pathWaypoint : 0;
                ImGui::Text("%zu%s", left, zombie->pathFailed ? "!" : "");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(fmt::format("{}{}{}{}{}", zombie->seesPlayer ? "sees " : "",
                                                   zombie->heardNoise > 0.0f ? "hears " : "", zombie->aware ? "aware " : "",
                                                   zombie->Lame() ? "lame " : "",
                                                   zombie->stuckSeconds > 1.0f ? "STUCK" : "").c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void NextDayzDebugger::InfectedDetail(const FZombieRuntime& zombie)
    {
        const glm::vec3 feet = PlayerFeet();
        const FZombieDef& definition = ZombieDef(zombie.profile);
        KVColored("State", Style::ZombieState(zombie.state),
                  fmt::format("{} for {:.1f} s{}", ZombieStateName(zombie.state), zombie.stateSeconds,
                              zombie.state == EZombieState::Dead ? fmt::format("  ({}, corpse {:.0f} s)",
                                                                               ZombieDeathName(zombie.death),
                                                                               zombie.corpseSeconds)
                                                                 : std::string()));
        KV("Health", "{:.0f} / {:.0f}  poise {:.0f}/{:.0f}  leg {:.0f}{}", zombie.health, definition.maxHealth, zombie.poise,
           systems_.config ? systems_.config->Zombie.PoiseMax : 0.0f, zombie.legDamage, zombie.crippled ? "  CRIPPLED" : "");
        KV("Position", "({:.1f}, {:.1f}, {:.1f})  {:.1f} m away", zombie.position.x, zombie.position.y, zombie.position.z,
           glm::distance(zombie.position, feet));
        KV("Motion", "speed {:.2f} m/s (wander {:.2f}, chase {:.2f}) x{:.2f}", zombie.Speed(), definition.wanderSpeed,
           definition.chaseSpeed, zombie.variant.speedScale);
        KV("Perception", "{}  sight {:.0f} m  fov {:.0f}  heard {:.2f}  aware {}",
           zombie.seesPlayer ? "SEES PLAYER" : "no sight", definition.sightDistance * systems_.zombies->SightScale(),
           definition.fieldOfViewDegrees, zombie.heardNoise, zombie.aware ? "yes" : "no");
        KV("Lost sight", "{:.2f} s  last known {:.1f} m away", zombie.lostSightSeconds,
           glm::distance(zombie.lastKnownPlayerPosition, zombie.position));
        const size_t left = zombie.pathWaypoint < zombie.path.size() ? zombie.path.size() - zombie.pathWaypoint : 0;
        KV("Route", "{} of {} waypoints left  {} solves{}  repath {:.2f} s  stuck {:.2f} s", left, zombie.path.size(),
           zombie.pathSolves, zombie.pathFailed ? "  LAST FAILED" : "", zombie.repathSeconds, zombie.stuckSeconds);
        KV("Attack", "{} timer {:.2f}  cooldown {:.2f}  count {}{}",
           zombie.attack == EZombieAttack::Lunge ? "lunge" : "swipe", zombie.attackTimer, zombie.attackCooldown,
           zombie.attackCount, zombie.attackCommitted ? "  committed" : "");
        KV("Reaction", "flinch {:.2f} s  stagger immunity {:.2f} s", zombie.flinchSeconds, zombie.staggerImmunity);
        KV("Variant", "hunch {:.0f}  tilt {:.0f}  reach {:.2f}  phase {:.2f}{}", zombie.variant.hunchDegrees,
           zombie.variant.headTiltDegrees, zombie.variant.reach, zombie.variant.phase, zombie.variant.lame ? "  born lame" : "");
        const int point = SpawnPointOf(zombie.handle);
        KV("Spawn", "{}", point >= 0 ? fmt::format("S{}", point) : std::string("by hand"));
        if (systems_.zombieVisuals && zombie.handle.index < systems_.zombieVisuals->size())
        {
            const ZombieRigVisual& visual = (*systems_.zombieVisuals)[zombie.handle.index];
            KV("Rig", "{} / {}  flinch {:.1f} deg  hitboxes {}", visual.BaseClipName() ? visual.BaseClipName() : "-",
               visual.ActionClipName() ? visual.ActionClipName() : "-", visual.FlinchDegrees(),
               systems_.combat ? systems_.combat->Hitboxes(zombie.handle).size() : 0);
        }
        if (ImGui::Button("Kill") && commands_.killZombie)
        {
            commands_.killZombie(zombie.handle);
        }
        ImGui::SameLine();
        if (ImGui::Button("Despawn"))
        {
            systems_.zombies->Recycle(zombie.handle);
            if (commands_.syncZombieVisuals) commands_.syncZombieVisuals();
        }
        ImGui::SameLine();
        if (ImGui::Button(knobs_.orbit && knobs_.orbitZombie ? "Stop orbit" : "Orbit camera"))
        {
            const bool on = !(knobs_.orbit && knobs_.orbitZombie);
            knobs_.orbit = on;
            knobs_.orbitZombie = on;
        }
        ImGui::SameLine();
        if (ImGui::Button("Teleport next to it") && commands_.teleport)
        {
            commands_.teleport(zombie.position - zombie.forward * 3.0f);
        }
    }

    // ------------------------------------------------------------------ spawns

    void NextDayzDebugger::TabSpawns()
    {
        ZombieSpawnDirector* spawns = systems_.spawns;
        if (!spawns)
        {
            return;
        }
        const FZombieSpawnTuning& tuning = spawns->Tuning();
        KV("Points", "{}  cap {}  next pass in {:.1f} s", spawns->Points().size(), spawns->GlobalCap(),
           spawns->NextEvaluationSeconds());
        KV("Totals", "{} spawned  {} recycled  {} rejected (visible)", spawns->SpawnCount(), spawns->RecycledCount(),
           spawns->RejectedVisible());
        KV("Window", "{:.0f} .. {:.0f} m from the player, out of view", tuning.minimumDistance, tuning.maximumDistance);
        KV("Recycle", "calm for {:.0f} s beyond {:.0f} m", tuning.recycleCalmSeconds, tuning.recycleDistance);
        KV("Respawn", "{:.0f} s + up to {:.0f} s after a group is gone", tuning.respawnMinSeconds, tuning.respawnJitterSeconds);

        std::array<int, 10> verdicts{};
        for (const FZombieSpawnPoint& point : spawns->Points())
        {
            ++verdicts[static_cast<size_t>(point.verdict)];
        }
        bool first = true;
        for (size_t index = 0; index < verdicts.size(); ++index)
        {
            if (verdicts[index] == 0)
            {
                continue;
            }
            if (!first)
            {
                ImGui::SameLine();
            }
            first = false;
            Colored(Style::SpawnVerdict(static_cast<ESpawnVerdict>(index)),
                    fmt::format("{} {}", SpawnVerdictName(static_cast<ESpawnVerdict>(index)), verdicts[index]));
        }
        bool on = LayerOn(LayerSpawns);
        if (ImGui::Checkbox("Show spawn points and activation rings in the world", &on))
        {
            SetLayer(LayerSpawns, on);
        }

        const glm::vec3 feet = PlayerFeet();
        std::vector<int> order(spawns->Points().size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b)
        {
            return spawns->Points()[a].playerDistance < spawns->Points()[b].playerDistance;
        });
        if (ImGui::BeginTable("##spawnPoints", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                              ImVec2(0.0f, 360.0f)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Point", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableSetupColumn("Out", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("Last verdict", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Cooldown / calm");
            ImGui::TableHeadersRow();
            const double now = spawns->WorldSeconds();
            for (const int index : order)
            {
                const FZombieSpawnPoint& point = spawns->Points()[index];
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool isSelected = selection_.kind == ESelection::SpawnPoint && selection_.index == index;
                if (ImGui::Selectable(fmt::format("S{}", index).c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns))
                {
                    SelectSpawnPoint(index);
                }
                if (ImGui::IsItemHovered())
                {
                    hoveredWorldPoint_ = point.position;
                }
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Teleport here") && commands_.teleport)
                    {
                        commands_.teleport(point.position + glm::vec3(2.0f, 0.0f, 2.0f));
                    }
                    ImGui::EndPopup();
                }
                int out = 0;
                for (const FZombieHandle& handle : point.active) out += handle.IsValid() ? 1 : 0;
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(std::string(ZombieDef(point.profile).id).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d/%d", out, point.groupSize);
                ImGui::TableNextColumn();
                Colored(Style::SpawnVerdict(point.verdict), SpawnVerdictName(point.verdict));
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", point.playerDistance);
                ImGui::TableNextColumn();
                const double cooldown = std::max(0.0, point.cooldownUntil - now);
                ImGui::Text("%s%s", cooldown > 0.0 ? fmt::format("{:.0f} s  ", cooldown).c_str() : "",
                            point.farCalmSeconds > 0.0f ? fmt::format("calm {:.0f} s", point.farCalmSeconds).c_str() : "");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Right-click a row to teleport. Verdicts update once per pass.");
    }

    // ------------------------------------------------------------------ loot

    void NextDayzDebugger::TabLoot()
    {
        LootSystem* loot = systems_.loot;
        if (!loot || !systems_.engine)
        {
            return;
        }
        const FLootRespawnTuning& tuning = loot->Director().Tuning();
        KV("Entries", "{}  ({} spots, {} piles, {} zones)", loot->Entries().size(), loot->SpotCount(), loot->PileCount(),
           loot->Zones().size());
        int reserved = 0;
        for (const FLootEntry& entry : loot->Entries()) reserved += entry.state == FLootEntry::EState::Reserved ? 1 : 0;
        KV("State", "{} available  {} reserved  {} cooling", loot->RemainingCount(), reserved, loot->CooldownCount());
        KV("Restock rule", "away {:.0f} m from the zone, unseen {:.0f} s, category cooldown", tuning.minimumPlayerDistance,
           tuning.minimumOffscreenSeconds);
        KVColored("Scarcity", loot->FoodScarce() || loot->MedicalScarce() ? Style::kWarn : Style::kMuted,
                  fmt::format("food {}  medical {}", loot->FoodScarce() ? "SCARCE" : "ok",
                              loot->MedicalScarce() ? "SCARCE" : "ok"));

        if (ImGui::BeginTable("##categories", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Category");
            ImGui::TableSetupColumn("Avail", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn("Cooling", ImGuiTableColumnFlags_WidthFixed, 52.0f);
            ImGui::TableSetupColumn("Cap", ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn("Cooldown", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableHeadersRow();
            for (size_t index = 0; index < static_cast<size_t>(ELootCategory::Count); ++index)
            {
                const ELootCategory category = static_cast<ELootCategory>(index);
                int available = 0;
                int total = 0;
                int cooling = 0;
                for (const FLootEntry& entry : loot->Entries())
                {
                    if (entry.state == FLootEntry::EState::Empty || entry.category != category) continue;
                    ++total;
                    available += entry.state == FLootEntry::EState::Available ? 1 : 0;
                    cooling += entry.state == FLootEntry::EState::Cooldown ? 1 : 0;
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                Colored(available == 0 ? Style::kBad : Style::LootCategory(category),
                        fmt::format("{}{}", LootCategoryName(category), available == 0 ? "  MISSING" : ""));
                ImGui::TableNextColumn();
                ImGui::Text("%d", available);
                ImGui::TableNextColumn();
                ImGui::Text("%d", total);
                ImGui::TableNextColumn();
                ImGui::Text("%d", cooling);
                ImGui::TableNextColumn();
                ImGui::Text("%d", loot->CategoryCap(category));
                ImGui::TableNextColumn();
                ImGui::Text("%.0f s x%.2f", RespawnCooldown(category, tuning), loot->Director().CooldownScale(category));
            }
            ImGui::EndTable();
        }

        // Critical coverage: can a fresh life find the basics anywhere?
        int food = 0, medical = 0, backpacks = 0, guns = 0, ammo = 0;
        for (const FLootEntry& entry : loot->Entries())
        {
            if (entry.state != FLootEntry::EState::Available) continue;
            for (const FLootStack& stack : entry.contents)
            {
                const FItemDef* item = FindItemDef(stack.itemId);
                if (!item) continue;
                food += item->use == EItemUse::Eat ? stack.count : 0;
                medical += item->use == EItemUse::Bandage || item->use == EItemUse::FirstAid ? stack.count : 0;
                backpacks += item->equipSlot == EEquipSlot::Back ? stack.count : 0;
                guns += IsWieldable(*item) ? stack.count : 0;
                ammo += item->kind == EItemKind::Ammo ? stack.count : 0;
            }
        }
        const int wells = systems_.anchors ? static_cast<int>(systems_.anchors->Count(EWorldAnchorType::Well)) : 0;
        const auto coverage = [](const char* name, int count)
        {
            Colored(count > 0 ? Style::kGood : Style::kBad, fmt::format("{} {}", name, count));
            ImGui::SameLine();
        };
        ImGui::TextDisabled("Coverage:");
        ImGui::SameLine();
        coverage("food", food);
        coverage("medical", medical);
        coverage("backpacks", backpacks);
        coverage("weapons", guns);
        coverage("ammo", ammo);
        coverage("wells", wells);
        ImGui::NewLine();

        if (ImGui::Button("Restock everything now"))
        {
            Log(EChannel::Loot, 1, fmt::format("Debug restock: {} entries", loot->DebugRestockAll(*systems_.engine)));
        }
        ImGui::SameLine();
        bool zones = LayerOn(LayerLootZones);
        if (ImGui::Checkbox("Zones", &zones)) SetLayer(LayerLootZones, zones);
        ImGui::SameLine();
        bool markers = LayerOn(LayerLoot);
        if (ImGui::Checkbox("Markers (F7)", &markers)) SetLayer(LayerLoot, markers);

        // Selected entry.
        if (selection_.kind == ESelection::Loot && selection_.index >= 0 &&
            selection_.index < static_cast<int>(loot->Entries().size()))
        {
            const FLootEntry& entry = loot->Entries()[selection_.index];
            const FLootSlot* slot = loot->ResolveSlot(entry.directorHandle);
            ImGui::SeparatorText(fmt::format("Selected L{}: {}", selection_.index, entry.displayName).c_str());
            KV("Source", "{}  {} / {}  zone {}", entry.source == ELootSource::Spot ? "spot"
                                                 : entry.source == ELootSource::Pile ? "pile" : "authored",
               LootProfileName(entry.profile), LootCategoryName(entry.category), entry.zone);
            for (const FLootStack& stack : entry.contents)
            {
                KV("  contains", "{} x{}{}", stack.itemId, stack.count,
                   stack.loadedAmmo > 0 ? fmt::format(" ({} loaded)", stack.loadedAmmo) : std::string());
            }
            if (slot && entry.state == FLootEntry::EState::Cooldown)
            {
                KVColored("Restock", Style::LootBlock(slot->block),
                          fmt::format("blocked by {}  ready in {:.0f} s  unseen since {}", LootRespawnBlockName(slot->block),
                                      std::max(0.0, loot->Director().ReadyAt(entry.directorHandle) - loot->WorldSeconds()),
                                      slot->offscreenSince < 0.0 ? std::string("-")
                                                                 : fmt::format("{:.0f} s", loot->WorldSeconds() - slot->offscreenSince)));
                if (ImGui::Button("Restock this"))
                {
                    loot->DebugRestock(selection_.index, *systems_.engine);
                }
                ImGui::SameLine();
            }
            if (ImGui::Button("Teleport next to it") && commands_.teleport)
            {
                commands_.teleport(entry.worldPos + glm::vec3(0.8f, 0.0f, 0.8f));
            }
        }

        ImGui::SeparatorText("Entries by distance");
        static constexpr std::array<const char*, 7> categoryItems = {"any category", "Food/Water", "Medical", "Ammo",
                                                                     "Weapon", "Clothing", "Misc"};
        static constexpr std::array<const char*, 5> stateItems = {"any state", "Available", "Reserved", "Cooldown", "Empty"};
        int categoryChoice = lootCategoryFilter_ + 1;
        int stateChoice = lootStateFilter_ + 1;
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::Combo("##lootCategory", &categoryChoice, categoryItems.data(), static_cast<int>(categoryItems.size())))
        {
            lootCategoryFilter_ = categoryChoice - 1;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::Combo("##lootState", &stateChoice, stateItems.data(), static_cast<int>(stateItems.size())))
        {
            lootStateFilter_ = stateChoice - 1;
        }
        const glm::vec3 feet = PlayerFeet();
        std::vector<int> order;
        for (int index = 0; index < static_cast<int>(loot->Entries().size()); ++index)
        {
            const FLootEntry& entry = loot->Entries()[index];
            if ((lootCategoryFilter_ < 0 || static_cast<int>(entry.category) == lootCategoryFilter_) &&
                (lootStateFilter_ < 0 ? entry.state != FLootEntry::EState::Empty
                                      : static_cast<int>(entry.state) == lootStateFilter_))
            {
                order.push_back(index);
            }
        }
        std::sort(order.begin(), order.end(), [&](int a, int b)
        {
            return glm::distance(loot->Entries()[a].worldPos, feet) < glm::distance(loot->Entries()[b].worldPos, feet);
        });
        if (ImGui::BeginTable("##lootEntries", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                              ImVec2(0.0f, 320.0f)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("Contents");
            ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthFixed, 76.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("Restock", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(order.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const int index = order[row];
                    const FLootEntry& entry = loot->Entries()[index];
                    ImGui::PushID(index);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const bool isSelected = selection_.kind == ESelection::Loot && selection_.index == index;
                    if (ImGui::Selectable(fmt::format("L{}", index).c_str(), isSelected,
                                          ImGuiSelectableFlags_SpanAllColumns))
                    {
                        SelectLoot(index);
                    }
                    if (ImGui::IsItemHovered())
                    {
                        hoveredWorldPoint_ = entry.worldPos;
                    }
                    ImGui::TableNextColumn();
                    Colored(Style::LootCategory(entry.category), entry.displayName.empty() ? "-" : entry.displayName);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(LootProfileName(entry.profile));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(entry.state == FLootEntry::EState::Available  ? "Available"
                                           : entry.state == FLootEntry::EState::Reserved ? "Reserved"
                                           : entry.state == FLootEntry::EState::Cooldown ? "Cooldown"
                                                                                          : "Empty");
                    ImGui::TableNextColumn();
                    if (entry.state == FLootEntry::EState::Cooldown)
                    {
                        const FLootSlot* slot = loot->ResolveSlot(entry.directorHandle);
                        const double readyIn =
                            std::max(0.0, loot->Director().ReadyAt(entry.directorHandle) - loot->WorldSeconds());
                        Colored(slot ? Style::LootBlock(slot->block) : Style::kMuted,
                                fmt::format("{} {:.0f}s", slot ? LootRespawnBlockName(slot->block) : "-", readyIn));
                    }
                    ImGui::TableNextColumn();
                    ImGui::Text("%.0f", glm::distance(entry.worldPos, feet));
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }

    // ------------------------------------------------------------------ world

    void NextDayzDebugger::TabWorld(const FGameFacts& facts)
    {
        TimeSystem* time = systems_.time;
        if (time)
        {
            KV("Clock", "{:02d}:{:02d}  ({:.0f} game min/real s)", time->HourInt(), time->MinuteInt(), time->TimeScale());
            KV("Light", "daylight {:.2f}  storm {:.2f}  chill {:.2f}", time->Daylight(), time->Storm(), time->Chill());
            float hour = static_cast<float>(time->Hour());
            if (ImGui::SliderFloat("Hour", &hour, 0.0f, 23.99f, "%.2f"))
            {
                time->SetHour(hour);
            }
            float scale = static_cast<float>(time->TimeScale());
            if (ImGui::SliderFloat("Clock speed", &scale, 0.0f, 600.0f, "%.0f game min/real s"))
            {
                time->TimeScaleRef() = scale;
            }
            for (const auto& [label, minutes] : {std::pair{"+1 h", 60.0}, std::pair{"+6 h", 360.0}, std::pair{"+12 h", 720.0}})
            {
                if (ImGui::SmallButton(label))
                {
                    time->Skip(minutes);
                }
                ImGui::SameLine();
            }
            ImGui::TextDisabled("|");
            for (const EWeather weather : {EWeather::Clear, EWeather::Overcast, EWeather::Fog})
            {
                ImGui::SameLine();
                if (ImGui::RadioButton(WeatherName(weather), time->Weather() == weather))
                {
                    time->SetWeather(weather);
                }
            }
        }
        KV("Lantern", "{}", facts.lanternLit ? fmt::format("lit, {:.1f} min of fuel", facts.lanternSeconds / 60.0f)
                                             : std::string("off"));
        KV("Supply cache", "{}", facts.cacheMarker ? fmt::format("{:.0f} m away", glm::distance(*facts.cacheMarker, PlayerFeet()))
                                                   : std::string("none"));
        KV("Firecrackers", "{} live", facts.firecrackers.size());

        if (systems_.anchors)
        {
            const WorldAnchorRegistry& anchors = *systems_.anchors;
            ImGui::SeparatorText("Map anchors");
            KV("Counts", "{} POIs  {} loot  {} infected  {} wells  {} safe spawns", anchors.Count(EWorldAnchorType::Poi),
               anchors.Count(EWorldAnchorType::Loot), anchors.Count(EWorldAnchorType::Zombie),
               anchors.Count(EWorldAnchorType::Well), anchors.Count(EWorldAnchorType::PlayerSafe));
            bool on = LayerOn(LayerAnchors);
            if (ImGui::Checkbox("Show POIs, wells and markers in the world", &on))
            {
                SetLayer(LayerAnchors, on);
            }
            const glm::vec3 feet = PlayerFeet();
            if (ImGui::BeginTable("##pois", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                                   ImGuiTableFlags_ScrollY, ImVec2(0.0f, 220.0f)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("POI");
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 34.0f);
                ImGui::TableHeadersRow();
                int row = 0;
                for (const FWorldAnchor& anchor : anchors.All())
                {
                    if (anchor.type != EWorldAnchorType::Poi)
                    {
                        continue;
                    }
                    ImGui::PushID(row++);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(anchor.label.c_str());
                    if (ImGui::IsItemHovered())
                    {
                        hoveredWorldPoint_ = anchor.worldPos;
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(anchor.profile.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%.0f", glm::distance(anchor.worldPos, feet));
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("Go") && commands_.teleport)
                    {
                        commands_.teleport(anchor.worldPos);
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }

        if (const NextGameplay::FNavGrid* grid = systems_.navGrid)
        {
            ImGui::SeparatorText("Infected navigation");
            KV("Grid", "{}", grid->IsBuilt() ? fmt::format("{} x {} cells at {:.1f} m", grid->GetWidth(), grid->GetHeight(),
                                                           grid->GetCellSize())
                                             : std::string("NOT BUILT"));
            const glm::vec3 feet = PlayerFeet();
            KV("Under you", "{}", grid->IsBuilt() ? (grid->IsWalkable(feet) ? "walkable" : "NOT walkable") : "-");
            bool on = LayerOn(LayerNav);
            if (ImGui::Checkbox("Show cells", &on))
            {
                SetLayer(LayerNav, on);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderFloat("##navRadius", &knobs_.navRadius, 6.0f, 80.0f, "radius %.0f m");
            ImGui::SameLine();
            if (ImGui::Button("Route to crosshair") && grid->IsBuilt())
            {
                if (const std::optional<FRayHit> hit = CrosshairHit(300.0f))
                {
                    const auto start = std::chrono::steady_clock::now();
                    navProbePath_ = grid->FindPath(feet, hit->point, feet.y,
                                                   systems_.config ? systems_.config->Zombie.MaxPathExpansions : 0);
                    const float ms =
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
                    Log(EChannel::World, 0,
                        fmt::format("Nav probe: {} waypoints to {:.0f} m in {:.2f} ms", navProbePath_->size(),
                                    glm::distance(feet, hit->point), ms));
                    SetLayer(LayerNav, true);
                }
            }
            if (navProbePath_)
            {
                ImGui::SameLine();
                ImGui::Text("%zu waypoints", navProbePath_->size());
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##probe"))
                {
                    navProbePath_.reset();
                }
            }
        }
    }

    // ------------------------------------------------------------------ combat

    void NextDayzDebugger::TabCombat()
    {
        if (systems_.combat && systems_.zombies)
        {
            const FZombieHandle last = systems_.combat->LastHitZombie();
            KV("Kills", "{}  headshots {}  staggers {}", systems_.zombies->KillCount(), systems_.combat->HeadshotCount(),
               systems_.zombies->StaggerCount());
            KV("Last hit", "{}", last.IsValid() ? fmt::format("Z{}:{} {}", last.index, last.generation,
                                                              HitZoneName(systems_.combat->LastHitZone()))
                                                : std::string("-"));
            if (systems_.config)
            {
                const FZombieConfig& zombie = systems_.config->Zombie;
                KV("Zone damage", "head x{:.1f}  torso x{:.1f}  arm x{:.1f}  leg x{:.1f}", zombie.HeadMultiplier,
                   zombie.TorsoMultiplier, zombie.LimbMultiplier, zombie.LegMultiplier);
                KV("Poise", "{:.0f} max  {:.0f}/s recovery  cripple at {:.0f} leg damage", zombie.PoiseMax,
                   zombie.PoiseRecoverPerSecond, zombie.CrippleLegDamage);
            }
        }
        if (systems_.effects && systems_.audio)
        {
            KV("Effects", "{} particles  {} tracers  flash {}", systems_.effects->ActiveParticles(),
               systems_.effects->ActiveTracers(), systems_.effects->FlashVisible() ? "on" : "off");
            KV("Audio", "{} sounds played", systems_.audio->PlayedCount());
        }
        ImGui::SetNextItemWidth(170.0f);
        ImGui::SliderFloat("##fxTime", &knobs_.fxTimeScale, 0.0f, 2.0f, "FX time x%.2f");
        ImGui::SameLine();
        bool hits = LayerOn(LayerHitboxes);
        if (ImGui::Checkbox("Hitboxes", &hits)) SetLayer(LayerHitboxes, hits);
        ImGui::SameLine();
        bool traces = LayerOn(LayerTraces);
        if (ImGui::Checkbox("Traces", &traces)) SetLayer(LayerTraces, traces);
        ImGui::SameLine();
        bool noise = LayerOn(LayerNoise);
        if (ImGui::Checkbox("Noise", &noise)) SetLayer(LayerNoise, noise);

        if (systems_.noise)
        {
            ImGui::SeparatorText(fmt::format("Live noise ({})", systems_.noise->Events().size()).c_str());
            const glm::vec3 feet = PlayerFeet();
            for (const FNoiseEvent& event : systems_.noise->Events())
            {
                ImGui::Text("#%llu %-8s r %3.0f m  x%.1f  %.2f s left  %.0f m away",
                            static_cast<unsigned long long>(event.sequence), NoiseTypeName(event.type), event.radius,
                            event.strength, event.remainingSeconds, glm::distance(event.position, feet));
                if (ImGui::IsItemHovered())
                {
                    hoveredWorldPoint_ = event.position;
                }
            }
        }

        ImGui::SeparatorText("Damage feed (newest first)");
        int shown = 0;
        for (size_t index = log_.Size(); index-- > 0 && shown < 40;)
        {
            const FEvent& event = log_.At(index);
            if (event.channel != EChannel::Combat && !(event.channel == EChannel::Survival && event.severity >= 2))
            {
                continue;
            }
            ++shown;
            Colored(event.channel == EChannel::Combat ? Style::Channel(EChannel::Combat) : Style::kBad,
                    fmt::format("{:7.1f}  {}{}", event.time, event.text,
                                event.repeats > 1 ? fmt::format("  x{}", event.repeats) : std::string()));
            if (event.hasPosition && ImGui::IsItemHovered())
            {
                hoveredWorldPoint_ = event.position;
            }
        }
        if (shown == 0)
        {
            ImGui::TextDisabled("No hits yet");
        }
    }

    // ------------------------------------------------------------------ log

    void NextDayzDebugger::TabLog()
    {
        for (size_t index = 0; index < kChannelCount; ++index)
        {
            const EChannel channel = static_cast<EChannel>(index);
            ImGui::PushStyleColor(ImGuiCol_Text, Style::V4(Style::Channel(channel)));
            ImGui::Checkbox(fmt::format("{} {}##ch{}", ChannelName(channel), log_.ChannelCount(channel), index).c_str(),
                            &channelFilter_[index]);
            ImGui::PopStyleColor();
            if ((index + 1) % 6 != 0)
            {
                ImGui::SameLine();
            }
        }
        ImGui::NewLine();
        if (ImGui::SmallButton("All"))
        {
            channelFilter_.fill(true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("None"))
        {
            channelFilter_.fill(false);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        InputString("##logFilter", logFilter_, "text filter");
        ImGui::SameLine();
        ImGui::Checkbox("Follow", &logFollow_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy"))
        {
            std::string text;
            for (size_t index = 0; index < log_.Size(); ++index)
            {
                const FEvent& event = log_.At(index);
                if (channelFilter_[static_cast<size_t>(event.channel)] && Contains(event.text, logFilter_))
                {
                    text += fmt::format("{:.2f} [{}] {}{}\n", event.time, ChannelName(event.channel), event.text,
                                        event.repeats > 1 ? fmt::format(" x{}", event.repeats) : std::string());
                }
            }
            ImGui::SetClipboardText(text.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))
        {
            log_.Clear();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu kept / %llu total", log_.Size(), static_cast<unsigned long long>(log_.TotalPushed()));

        std::vector<int> visible;
        visible.reserve(log_.Size());
        for (size_t index = 0; index < log_.Size(); ++index)
        {
            const FEvent& event = log_.At(index);
            if (channelFilter_[static_cast<size_t>(event.channel)] && Contains(event.text, logFilter_))
            {
                visible.push_back(static_cast<int>(index));
            }
        }
        if (ImGui::BeginTable("##log", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                              ImGuiTableFlags_ScrollY, ImVec2(0.0f, 0.0f)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupColumn("Event");
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()));
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const FEvent& event = log_.At(static_cast<size_t>(visible[row]));
                    ImGui::PushID(static_cast<int>(event.serial));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f", event.time);
                    ImGui::TableNextColumn();
                    Colored(Style::Channel(event.channel), ChannelName(event.channel));
                    ImGui::TableNextColumn();
                    const std::string text = event.repeats > 1 ? fmt::format("{}  x{}", event.text, event.repeats)
                                                               : event.text;
                    ImGui::PushStyleColor(ImGuiCol_Text, Style::V4(Style::Severity(event.severity)));
                    ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_SpanAllColumns |
                                                               ImGuiSelectableFlags_AllowDoubleClick);
                    ImGui::PopStyleColor();
                    if (event.hasPosition && ImGui::IsItemHovered())
                    {
                        hoveredWorldPoint_ = event.position;
                        ImGui::SetTooltip("(%.1f, %.1f, %.1f)  double-click: teleport there", event.position.x,
                                          event.position.y, event.position.z);
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && commands_.teleport)
                        {
                            commands_.teleport(event.position + glm::vec3(1.0f, 0.0f, 1.0f));
                        }
                    }
                    ImGui::PopID();
                }
            }
            if (logFollow_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f)
            {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndTable();
        }
    }
}
