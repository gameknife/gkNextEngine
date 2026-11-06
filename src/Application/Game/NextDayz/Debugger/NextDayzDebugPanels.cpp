#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/NextDayz/UI/Localization.hpp"

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
            ImGui::TextDisabled("%s", L10n::Tr(key));
            ImGui::SameLine(kKeyColumn);
        }

        template <typename... Args>
        void KV(const char* key, fmt::format_string<Args...> format, Args&&... args)
        {
            Key(key);
            ImGui::TextUnformatted(NextDayz::L10n::Format(format, std::forward<Args>(args)...).c_str());
        }

        void KVColored(const char* key, const glm::vec4& color, const std::string& text)
        {
            Key(key);
            ImGui::TextColored(Style::V4(color), "%s", L10n::Tr(text));
        }

        void Colored(const glm::vec4& color, const std::string& text)
        {
            ImGui::TextColored(Style::V4(color), "%s", L10n::Tr(text));
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
            const std::string overlay = NextDayz::L10n::Format("{} {:.1f}", label, series.Latest());
            ImGui::PushStyleColor(ImGuiCol_PlotLines, Style::V4(color));
            ImGui::PlotLines(NextDayz::L10n::Format("##{}", label).c_str(), series.PlotValues(), series.PlotCount(),
                             series.PlotOffset(), overlay.c_str(), minimum, maximum, ImVec2(-1.0f, height));
            ImGui::PopStyleColor();
        }

        std::string MinutesSeconds(double seconds)
        {
            const int total = static_cast<int>(seconds);
            return NextDayz::L10n::Format("{:02d}:{:02d}", total / 60, total % 60);
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
        if (ImGui::Begin(NextDayz::L10n::Label("NextDayz Debug###NextDayzDebugger"), nullptr, flags))
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
                    if (!ImGui::BeginTabItem(L10n::Label(TabName(tab)), nullptr, tabFlags))
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
        L10n::DrawLanguageSelector();
        const ImGuiIO& io = ImGui::GetIO();
        const FTimerStat& tick = profiler_.Stat(ETimer::Tick);
        const std::string gameClock = systems_.time
            ? NextDayz::L10n::Format("{:02d}:{:02d} {}", systems_.time->HourInt(), systems_.time->MinuteInt(),
                          NextDayz::L10n::Tr(WeatherName(systems_.time->Weather())))
            : std::string("-");
        ImGui::Text(NextDayz::L10n::Tr("%.0f fps  %.1f ms | tick %.2f ms | sim %.1f s | life %s | %s"), io.Framerate,
                    io.DeltaTime * 1000.0f, tick.averageMs, clock_, MinutesSeconds(facts.survivalSeconds).c_str(), gameClock.c_str());
        if (knobs_.frozen || facts.paused)
        {
            ImGui::SameLine();
            Colored(Style::kWarn, knobs_.frozen ? NextDayz::L10n::Tr("  FROZEN") : NextDayz::L10n::Tr("  PAUSED"));
        }

        bool cursor = cursorMode_;
        if (ImGui::Checkbox(NextDayz::L10n::Label("Cursor (F9)"), &cursor))
        {
            SetCursorMode(cursor);
        }
        ImGui::SameLine();
        ImGui::Checkbox(NextDayz::L10n::Label("Freeze (F10)"), &knobs_.frozen);
        ImGui::SameLine();
        if (ImGui::Button(NextDayz::L10n::Label("Step")))
        {
            knobs_.frozen = true;
            ++knobs_.stepFrames;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("##timeScale", &knobs_.timeScale, 0.0f, 4.0f, NextDayz::L10n::Tr("time x%.2f"));
        for (const float preset : {0.25f, 1.0f, 3.0f})
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(NextDayz::L10n::Format("{}x", preset).c_str()))
            {
                knobs_.timeScale = preset;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(NextDayz::L10n::Label("Snapshot")))
        {
            WriteSnapshot();
        }

        if (facts.campaignEnabled)
        {
            ImGui::BeginDisabled(!facts.inField || facts.expeditionBusy || facts.saveBlocked);
            if (ImGui::Button(NextDayz::L10n::Label("Return to safehouse")) && commands_.expeditionAction)
                commands_.expeditionAction(EExpeditionAction::ReturnToSafehouse);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(NextDayz::L10n::Tr("Stores carried gear, cancels rescue and saves. No extraction credit."));
            ImGui::SameLine();
            ImGui::TextDisabled("%s | %s", NextDayz::L10n::Tr(facts.regionName), facts.inField ? NextDayz::L10n::Tr("IN FIELD") : NextDayz::L10n::Tr("SAFEHOUSE"));
        }

        ImGui::TextDisabled(NextDayz::L10n::Tr("Selected:"));
        ImGui::SameLine();
        ImGui::TextUnformatted(L10n::Tr(SelectionLabel()));
        if (selection_.kind != ESelection::None)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(NextDayz::L10n::Label("Clear selection")))
            {
                ClearSelection();
            }
        }
        if (!cursorMode_ && !facts.mouseFree)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(NextDayz::L10n::Tr("  F9: free the cursor to use this window / click things in the world"));
        }
        else if (cursorMode_)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(NextDayz::L10n::Tr("  click in the world to select | PgUp/PgDn tabs"));
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
                    NextDayz::L10n::Format("{} {}", warning.severity >= 2 ? NextDayz::L10n::Tr("ERROR") : NextDayz::L10n::Tr("WARN "), warning.text));
        }
        if (warnings_.size() > shown)
        {
            ImGui::TextDisabled(NextDayz::L10n::Tr("... %zu more (see snapshot)"), warnings_.size() - shown);
        }
    }

    void NextDayzDebugger::LayerToggles()
    {
        ImGui::SeparatorText(NextDayz::L10n::Tr("World overlay layers (F6 infected, F7 loot, F8 hits)"));
        int column = 0;
        for (const FLayerInfo& info : AllLayers())
        {
            bool on = LayerOn(info.layer);
            if (ImGui::Checkbox(L10n::Label(info.label), &on))
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
        ImGui::SliderFloat("##overlayDistance", &knobs_.overlayDistance, 20.0f, 600.0f, NextDayz::L10n::Tr("markers < %.0f m"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("##labelDistance", &knobs_.labelDistance, 5.0f, 200.0f, NextDayz::L10n::Tr("labels < %.0f m"));
        ImGui::TextDisabled("cvar: nextdayz.debug.layers = \"%s\"", LayerString().c_str());
    }

    // ------------------------------------------------------------------ overview

    void NextDayzDebugger::TabOverview(const FGameFacts& facts)
    {
        if (facts.campaignEnabled)
        {
            ImGui::SeparatorText(NextDayz::L10n::Tr("Expedition shortcuts"));
            ImGui::TextDisabled(NextDayz::L10n::Tr("Rescue: %s"), facts.flightPhase.c_str());
            ImGui::BeginDisabled(facts.expeditionBusy || facts.saveBlocked);
            if (ImGui::Button(NextDayz::L10n::Label("Save progress")) && commands_.expeditionAction)
                commands_.expeditionAction(EExpeditionAction::Save);
            ImGui::SameLine();
            if (ImGui::Button(NextDayz::L10n::Label("Reload map (keep progress)")) && commands_.expeditionAction)
                commands_.expeditionAction(EExpeditionAction::ReloadMap);
            ImGui::BeginDisabled(!facts.inField);
            if (ImGui::Button(NextDayz::L10n::Label("Safe spawn")) && commands_.expeditionAction)
                commands_.expeditionAction(EExpeditionAction::SafeSpawn);
            const int pads = systems_.anchors ? static_cast<int>(systems_.anchors->Count(EWorldAnchorType::Extraction)) : 0;
            for (int index = 0; index < 2; ++index)
            {
                ImGui::SameLine();
                ImGui::BeginDisabled(pads <= index);
                if (ImGui::Button(index == 0 ? NextDayz::L10n::Tr("Extraction ALPHA") : NextDayz::L10n::Tr("Extraction BRAVO")) && commands_.expeditionAction)
                    commands_.expeditionAction(index == 0 ? EExpeditionAction::ExtractionAlpha : EExpeditionAction::ExtractionBravo);
                ImGui::EndDisabled();
            }
            if (ImGui::Button(NextDayz::L10n::Label("Rescue supplies")) && commands_.expeditionAction)
                commands_.expeditionAction(EExpeditionAction::RescueSupplies);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(NextDayz::L10n::Tr("2 signal flare guns, 2 bandages, 2 water bottles. Overflow drops nearby."));
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (facts.saveBlocked) Colored(Style::kBad, NextDayz::L10n::Tr("Save blocked; resolve the save error before expedition actions."));
            ImGui::SeparatorText(NextDayz::L10n::Tr("Systems"));
        }
        if (ImGui::BeginTable("##systems", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                  ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("System"), ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("State"));
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
                row(NextDayz::L10n::Tr("Player"), NextDayz::L10n::Format("({:.0f}, {:.1f}, {:.0f})  {} {}  {:.1f} m/s  {}", position.x, position.y,
                                          position.z, NextDayz::L10n::Tr(StanceName(locomotion.actualStance)), NextDayz::L10n::Tr(GaitName(locomotion.gait)),
                                          locomotion.horizontalSpeed, systems_.player->IsFirstPerson() ? NextDayz::L10n::Tr("FPS") : NextDayz::L10n::Tr("TPS")),
                    locomotion.onGround ? Style::kGood : Style::kInfo);
            }
            if (systems_.survival)
            {
                const FSurvivalSnapshot& state = systems_.survival->Snapshot();
                row(NextDayz::L10n::Tr("Survival"),
                    NextDayz::L10n::Format("HP {:.0f}  food {:.0f}  water {:.0f}  cold {:.2f}  bleed {}{}", state.health, state.hunger,
                                state.hydration, state.cold, state.bleedingWounds, systems_.survival->IsWeak() ? NextDayz::L10n::Tr("  WEAK") : ""),
                    !systems_.survival->IsAlive() ? Style::kBad
                    : state.bleedingWounds > 0 || state.health < 35.0f ? Style::kWarn : Style::kGood);
            }
            if (systems_.weapons && systems_.inventory)
            {
                row(NextDayz::L10n::Tr("Weapons"),
                    systems_.weapons->HasActiveWeapon()
                        ? NextDayz::L10n::Format("{}  {} / {}  {}", systems_.weapons->ActiveDisplayName(), systems_.weapons->AmmoInMag(),
                                      systems_.weapons->AmmoReserve(*systems_.inventory),
                                      NextDayz::L10n::Tr(WeaponActionName(systems_.weapons->PresentationAction())))
                        : std::string(NextDayz::L10n::Tr("unarmed")),
                    systems_.weapons->HasActiveWeapon() ? Style::kGood : Style::kMuted);
            }
            if (systems_.zombies && systems_.spawns)
            {
                row(NextDayz::L10n::Tr("Infected"),
                    NextDayz::L10n::Format("{} / {} active  {} alerted  {} kills  paths {}+{} deferred",
                                systems_.zombies->ActiveCount(), systems_.spawns->GlobalCap(),
                                systems_.zombies->AlertedCount(), systems_.zombies->KillCount(),
                                systems_.zombies->PathSolvesLastUpdate(), systems_.zombies->PathDeferredLastUpdate()),
                    systems_.zombies->AlertedCount() > 0 ? Style::kWarn : Style::kGood);
                row(NextDayz::L10n::Tr("Spawns"),
                    NextDayz::L10n::Format("{} points  {} spawned  {} recycled  {} rejected visible",
                                systems_.spawns->Points().size(), systems_.spawns->SpawnCount(),
                                systems_.spawns->RecycledCount(), systems_.spawns->RejectedVisible()),
                    systems_.spawns->Points().empty() ? Style::kBad : Style::kGood);
            }
            if (systems_.loot)
            {
                const LootSystem& loot = *systems_.loot;
                row(NextDayz::L10n::Tr("Loot"),
                    NextDayz::L10n::Format("{} available  {} cooling  {} entries / {} zones{}{}", loot.RemainingCount(),
                                loot.CooldownCount(), loot.Entries().size(), loot.Zones().size(),
                                loot.FoodScarce() ? NextDayz::L10n::Tr("  food-scarce") : "", loot.MedicalScarce() ? NextDayz::L10n::Tr("  med-scarce") : ""),
                    loot.RemainingCount() > 0 ? Style::kGood : Style::kBad);
            }
            if (systems_.time && systems_.zombies)
            {
                row(NextDayz::L10n::Tr("World"),
                    NextDayz::L10n::Format("{:02d}:{:02d} {}  daylight {:.2f}  chill {:.2f}  sight x{:.2f}", systems_.time->HourInt(),
                                systems_.time->MinuteInt(), NextDayz::L10n::Tr(WeatherName(systems_.time->Weather())),
                                systems_.time->Daylight(), systems_.time->Chill(), systems_.zombies->SightScale()),
                    Style::kGood);
            }
            if (systems_.noise && systems_.effects && systems_.audio)
            {
                row(NextDayz::L10n::Tr("Noise/FX"),
                    NextDayz::L10n::Format("{} noises  {} particles  {} tracers  {} sounds played", systems_.noise->Events().size(),
                                systems_.effects->ActiveParticles(), systems_.effects->ActiveTracers(),
                                systems_.audio->PlayedCount()),
                    Style::kGood);
            }
            if (systems_.navGrid)
            {
                row(NextDayz::L10n::Tr("Nav grid"),
                    systems_.navGrid->IsBuilt()
                        ? NextDayz::L10n::Format("{} x {} cells at {:.1f} m", systems_.navGrid->GetWidth(),
                                      systems_.navGrid->GetHeight(), systems_.navGrid->GetCellSize())
                        : std::string(NextDayz::L10n::Tr("NOT BUILT")),
                    systems_.navGrid->IsBuilt() ? Style::kGood : Style::kBad);
            }
            row(NextDayz::L10n::Tr("Assets"),
                NextDayz::L10n::Format("soldier rig {}  infected rig {}  loot catalog {}", facts.soldierRigLoaded ? NextDayz::L10n::Tr("ok") : NextDayz::L10n::Tr("MISSING"),
                            facts.zombieRigLoaded ? NextDayz::L10n::Tr("ok") : NextDayz::L10n::Tr("MISSING"), facts.lootCatalogLoaded ? NextDayz::L10n::Tr("ok") : NextDayz::L10n::Tr("MISSING")),
                facts.soldierRigLoaded && facts.zombieRigLoaded && facts.lootCatalogLoaded ? Style::kGood : Style::kBad);
            row(NextDayz::L10n::Tr("Objective"), facts.objective, Style::kInfo);
            ImGui::EndTable();
        }

        ImGui::SeparatorText(NextDayz::L10n::Tr("CPU per system (ms: last / avg / peak)"));
        if (ImGui::BeginTable("##timers", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Timer"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("share of 16.6 ms"));
            for (size_t index = 0; index < kTimerCount; ++index)
            {
                const FTimerStat& stat = profiler_.Stat(static_cast<ETimer>(index));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(NextDayz::L10n::Tr(TimerName(static_cast<ETimer>(index))));
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

        ImGui::SeparatorText(NextDayz::L10n::Tr("Last 60 s"));
        Plot(NextDayz::L10n::Tr("frame ms"), history_.frameMs, 0.0f, 40.0f, Style::kInfo);
        Plot(NextDayz::L10n::Tr("tick ms"), history_.tickMs, 0.0f, 8.0f, Style::kWarn);
        Plot(NextDayz::L10n::Tr("infected active"), history_.infectedActive, 0.0f, 60.0f, Style::kGood);
        Plot(NextDayz::L10n::Tr("infected alerted"), history_.infectedAlerted, 0.0f, 20.0f, Style::ZombieState(EZombieState::Chase));
        Plot(NextDayz::L10n::Tr("path solves / 0.25 s"), history_.pathSolves, 0.0f, 40.0f, Style::kPath);
        Plot(NextDayz::L10n::Tr("loot available"), history_.lootAvailable, 0.0f, std::max(10.0f, history_.lootAvailable.Max() * 1.2f),
             Style::LootCategory(ELootCategory::FoodWater));

        LayerToggles();
    }

    // ------------------------------------------------------------------ player

    void NextDayzDebugger::TabPlayer()
    {
        PlayerController* player = systems_.player;
        if (!player || !player->IsValid())
        {
            ImGui::TextDisabled(NextDayz::L10n::Tr("No player"));
            return;
        }
        const FPlayerLocomotionState& locomotion = player->LocomotionState();
        if (ImGui::CollapsingHeader(NextDayz::L10n::Label("Camera"), ImGuiTreeNodeFlags_DefaultOpen))
        {
            const glm::vec3 position = player->Position();
            const glm::vec3 eye = player->EyePosition();
            KV(NextDayz::L10n::Tr("View"), "{}{}", player->IsFirstPerson() ? NextDayz::L10n::Tr("first person") : NextDayz::L10n::Tr("third person"), knobs_.orbit ? NextDayz::L10n::Tr(" (orbit debug)") : "");
            KV(NextDayz::L10n::Tr("Feet"), "{:.2f}  {:.2f}  {:.2f}   map ({:.0f}, {:.0f})", position.x, position.y, position.z, position.x,
               -position.z);
            KV(NextDayz::L10n::Tr("Eye"), "{:.2f}  {:.2f}  {:.2f}", eye.x, eye.y, eye.z);
            KV(NextDayz::L10n::Tr("Yaw / pitch"), "{:.1f} / {:.1f} deg", glm::degrees(player->Yaw()), glm::degrees(player->Pitch()));
            KV(NextDayz::L10n::Tr("FOV"), "{:.1f} deg", player->CurrentFov());
            KV(NextDayz::L10n::Tr("Camera recoil"), "{:.2f} / {:.2f} deg{}", glm::degrees(player->CameraRecoil().x),
               glm::degrees(player->CameraRecoil().y), player->RecoilActive() ? NextDayz::L10n::Tr("  active") : "");
        }
        if (ImGui::CollapsingHeader(NextDayz::L10n::Label("Locomotion"), ImGuiTreeNodeFlags_DefaultOpen))
        {
            KV(NextDayz::L10n::Tr("Stance"), "{} -> {}{}", NextDayz::L10n::Tr(StanceName(locomotion.actualStance)), NextDayz::L10n::Tr(StanceName(locomotion.desiredStance)),
               locomotion.standBlocked ? NextDayz::L10n::Tr("  STAND BLOCKED") : "");
            KV(NextDayz::L10n::Tr("Gait"), "{}  sprint alpha {:.2f}{}", NextDayz::L10n::Tr(GaitName(locomotion.gait)), player->SprintAlpha(),
               player->WeaponReady() ? "" : NextDayz::L10n::Tr("  (weapon lowered)"));
            KV(NextDayz::L10n::Tr("Speed"), "{:.2f} m/s  vel ({:.2f}, {:.2f}, {:.2f})", locomotion.horizontalSpeed, locomotion.worldVelocity.x,
               locomotion.worldVelocity.y, locomotion.worldVelocity.z);
            KV(NextDayz::L10n::Tr("Move input"), "{:.2f}  {:.2f}", locomotion.localMove.x, locomotion.localMove.y);
            KV(NextDayz::L10n::Tr("Grounded"), "{}  capsule {:.2f} m", locomotion.onGround ? NextDayz::L10n::Tr("yes") : NextDayz::L10n::Tr("NO"), player->ControllerHeight());
            KV(NextDayz::L10n::Tr("Jump"), "{} {:.2f}", NextDayz::L10n::Tr(JumpPhaseName(locomotion.jumpPhase)), locomotion.jumpPhaseTime01);
            KV(NextDayz::L10n::Tr("Traversal"), "{} {:.2f}  height {:.2f} m", NextDayz::L10n::Tr(TraversalActionName(locomotion.traversalAction)),
               locomotion.traversalTime01, locomotion.traversalHeight);
            KV(NextDayz::L10n::Tr("Probe"), "{}", player->TraversalProbeResult());
        }
        if (systems_.rig && ImGui::CollapsingHeader(NextDayz::L10n::Label("Animation")))
        {
            const PlayerRigVisual& rig = *systems_.rig;
            KV(NextDayz::L10n::Tr("Base clip"), "{}", rig.CurrentBaseClipName().empty() ? "-" : rig.CurrentBaseClipName());
            KV(NextDayz::L10n::Tr("Aim weight"), "{:.2f}", rig.AimWeight());
            KV(NextDayz::L10n::Tr("Body yaw lag"), "{:.1f} deg{}",
               glm::degrees(std::remainder(player->Yaw() - rig.BodyYaw(), glm::two_pi<float>())),
               rig.TurningInPlace() ? NextDayz::L10n::Tr("  turning in place") : "");
            KV(NextDayz::L10n::Tr("Lean roll"), "{:.1f} deg", rig.LeanRollDegrees());
            KV(NextDayz::L10n::Tr("Grip error"), "{:.3f} m", rig.GripError());
            if (systems_.actions)
            {
                KV(NextDayz::L10n::Tr("Action"), "{} {:.2f}{}", NextDayz::L10n::Tr(ActionName(systems_.actions->Action())), systems_.actions->NormalizedTime(),
                   systems_.actions->IsCommitted() ? NextDayz::L10n::Tr(" committed") : "");
            }
            KV(NextDayz::L10n::Tr("Weapon layer"), "{} w {:.2f}", rig.CurrentWeaponActionClipName().empty() ? "-" : rig.CurrentWeaponActionClipName(),
               rig.WeaponActionWeight());
            KV(NextDayz::L10n::Tr("Recoil"), "rig {}  view {}", rig.RecoilActive() ? NextDayz::L10n::Tr("on") : NextDayz::L10n::Tr("off"),
               systems_.weapons && systems_.weapons->ViewModelRecoilActive() ? NextDayz::L10n::Tr("on") : NextDayz::L10n::Tr("off"));
        }
        if (ImGui::CollapsingHeader(NextDayz::L10n::Label("Debug camera"), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox(NextDayz::L10n::Label("Orbit camera"), &knobs_.orbit);
            ImGui::SameLine();
            ImGui::Checkbox(NextDayz::L10n::Label("Orbit the focus infected"), &knobs_.orbitZombie);
            ImGui::SameLine();
            if (ImGui::Button(player->IsFirstPerson() ? NextDayz::L10n::Tr("Third person") : NextDayz::L10n::Tr("First person")))
            {
                player->ToggleView();
            }
            ImGui::SliderFloat(NextDayz::L10n::Label("Orbit yaw"), &knobs_.orbitYaw, -180.0f, 180.0f, NextDayz::L10n::Tr("%.0f deg"));
            ImGui::SliderFloat(NextDayz::L10n::Label("Orbit distance"), &knobs_.orbitDistance, 0.5f, 20.0f, "%.1f m");
            ImGui::SliderFloat(NextDayz::L10n::Label("Orbit height"), &knobs_.orbitHeight, 0.0f, 3.0f, "%.2f m");
        }
        if (ImGui::CollapsingHeader(NextDayz::L10n::Label("Cheats and teleport"), ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox(NextDayz::L10n::Label("God mode (infected blows do no damage)"), &knobs_.god);
            ImGui::Checkbox(NextDayz::L10n::Label("Freeze needs (no hunger, thirst, bleeding, cold)"), &knobs_.freezeNeeds);
            if (ImGui::Button(NextDayz::L10n::Label("Teleport to crosshair")) && commands_.teleport)
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
                if (ImGui::BeginCombo("##poi", NextDayz::L10n::Tr("Teleport to POI...")))
                {
                    for (const FWorldAnchor& anchor : systems_.anchors->All())
                    {
                        if (anchor.type == EWorldAnchorType::Poi &&
                            ImGui::Selectable(NextDayz::L10n::Format("{} ({})", anchor.label, anchor.profile).c_str()))
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
        Meter(NextDayz::L10n::Tr("Health"), state.health, 100.0f, Style::kBad,
              NextDayz::L10n::Format("{:.1f}  ({:+.2f}/min)", state.health, rate(history_.health)));
        Meter(NextDayz::L10n::Tr("Hunger"), state.hunger, 100.0f, Style::kWarn,
              NextDayz::L10n::Format("{:.1f}  ({:+.2f}/min)", state.hunger, rate(history_.hunger)));
        Meter(NextDayz::L10n::Tr("Hydration"), state.hydration, 100.0f, Style::kInfo,
              NextDayz::L10n::Format("{:.1f}  ({:+.2f}/min)", state.hydration, rate(history_.hydration)));
        Meter(NextDayz::L10n::Tr("Cold"), state.cold, 1.0f, Style::Channel(EChannel::World),
              NextDayz::L10n::Format("{:.2f}  chill {:.2f} - warmth {:.2f}", state.cold, systems_.time ? systems_.time->Chill() : 0.0f,
                          systems_.inventory ? systems_.inventory->Warmth() : 0.0f));
        KVColored(NextDayz::L10n::Tr("Life"), survival->IsAlive() ? Style::kGood : Style::kBad,
                  survival->IsAlive() ? std::string(NextDayz::L10n::Tr("alive")) : NextDayz::L10n::Format("DEAD ({})", state.lastDamageSource));
        KVColored(NextDayz::L10n::Tr("Bleeding"), state.bleedingWounds > 0 ? Style::kBad : Style::kMuted,
                  NextDayz::L10n::Format("{} wound(s)", state.bleedingWounds));
        KV(NextDayz::L10n::Tr("Flags"), "{}{}{}", survival->IsWeak() ? NextDayz::L10n::Tr("WEAK  ") : "", state.healing ? NextDayz::L10n::Tr("healing  ") : "",
           state.painkillerSeconds > 0.0f ? NextDayz::L10n::Format("painkillers {:.0f}s", state.painkillerSeconds) : std::string());
        KV(NextDayz::L10n::Tr("Armor / warmth"), "{:.0f}% / {:.2f}", systems_.inventory ? systems_.inventory->Armor() * 100.0f : 0.0f,
           systems_.inventory ? systems_.inventory->Warmth() : 0.0f);
        KV(NextDayz::L10n::Tr("Last damage"), "{} (seq {})", state.lastDamageSource.empty() ? "-" : state.lastDamageSource, state.damageSequence);

        ImGui::SeparatorText(NextDayz::L10n::Tr("Last 60 s"));
        Plot(NextDayz::L10n::Tr("health"), history_.health, 0.0f, 100.0f, Style::kBad);
        Plot(NextDayz::L10n::Tr("hunger"), history_.hunger, 0.0f, 100.0f, Style::kWarn);
        Plot(NextDayz::L10n::Tr("hydration"), history_.hydration, 0.0f, 100.0f, Style::kInfo);
        Plot(NextDayz::L10n::Tr("cold x100"), history_.cold, 0.0f, 100.0f, Style::Channel(EChannel::World));

        ImGui::SeparatorText(NextDayz::L10n::Tr("Set"));
        float health = state.health;
        float hunger = state.hunger;
        float hydration = state.hydration;
        bool changed = ImGui::SliderFloat(NextDayz::L10n::Label("Health##set"), &health, 1.0f, 100.0f, "%.0f");
        changed |= ImGui::SliderFloat(NextDayz::L10n::Label("Hunger##set"), &hunger, 0.0f, 100.0f, "%.0f");
        changed |= ImGui::SliderFloat(NextDayz::L10n::Label("Hydration##set"), &hydration, 0.0f, 100.0f, "%.0f");
        if (changed && survival->IsAlive())
        {
            survival->SetNeeds(health, hunger, hydration);
        }
        int wounds = state.bleedingWounds;
        if (ImGui::SliderInt(NextDayz::L10n::Label("Bleeding wounds"), &wounds, 0, 3))
        {
            survival->SetBleeding(wounds);
        }
        if (ImGui::Button(NextDayz::L10n::Label("Restore all")))
        {
            survival->SetNeeds(100.0f, 100.0f, 100.0f);
            survival->SetBleeding(0);
        }
        ImGui::SameLine();
        if (ImGui::Button(NextDayz::L10n::Label("Starving")))
        {
            survival->SetNeeds(state.health, 8.0f, state.hydration);
        }
        ImGui::SameLine();
        if (ImGui::Button(NextDayz::L10n::Label("Parched")))
        {
            survival->SetNeeds(state.health, state.hunger, 8.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button(NextDayz::L10n::Label("Hurt 25")))
        {
            survival->ApplyDamage(25.0f, "debug", false);
        }
        ImGui::SameLine();
        if (ImGui::Button(NextDayz::L10n::Label("Kill")))
        {
            survival->ApplyDamage(1000.0f, "debug", false);
        }
        ImGui::SameLine();
        if (ImGui::Button(NextDayz::L10n::Label("New life")) && commands_.resetSession)
        {
            commands_.resetSession();
        }
        ImGui::Checkbox(NextDayz::L10n::Label("God mode"), &knobs_.god);
        ImGui::SameLine();
        ImGui::Checkbox(NextDayz::L10n::Label("Freeze needs"), &knobs_.freezeNeeds);
    }

    // ------------------------------------------------------------------ inventory

    void NextDayzDebugger::TabInventory()
    {
        Inventory* inventory = systems_.inventory;
        if (!inventory)
        {
            return;
        }
        KV(NextDayz::L10n::Tr("Capacity"), "{} / {} used", inventory->UsedCapacity(), inventory->TotalCapacity());
        KV(NextDayz::L10n::Tr("Armor / warmth"), "{:.0f}% / {:.2f}", inventory->Armor() * 100.0f, inventory->Warmth());
        KV(NextDayz::L10n::Tr("Tools"), "{}{}{}", inventory->HasTool(EItemTool::Map) ? NextDayz::L10n::Tr("map  ") : "",
           inventory->HasTool(EItemTool::Compass) ? NextDayz::L10n::Tr("compass  ") : "", inventory->CanOpenCans() ? NextDayz::L10n::Tr("can opener") : "");

        if (ImGui::BeginTable("##items", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                              ImVec2(0.0f, 260.0f)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Container"), ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Item"));
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Count"), ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Inst"), ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 86.0f);
            ImGui::TableHeadersRow();
            for (const FItemInstance& instance : inventory->Instances())
            {
                ImGui::PushID(static_cast<int>(instance.instanceId));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool worn = inventory->IsWorn(instance.instanceId);
                const FInventoryContainer* container = inventory->FindContainer(instance.containerId);
                ImGui::TextUnformatted(worn ? NextDayz::L10n::Tr("worn") : container ? NextDayz::L10n::Tr(container->displayName) : "?");
                ImGui::TableNextColumn();
                const FItemDef* definition = FindItemDef(instance.defId);
                ImGui::Text("%s%s", definition ? std::string(NextDayz::L10n::Tr(definition->displayName)).c_str() : instance.defId.c_str(),
                            instance.loadedAmmo > 0 ? NextDayz::L10n::Format("  [{} loaded]", instance.loadedAmmo).c_str() : "");
                ImGui::TableNextColumn();
                ImGui::Text("%d", instance.count);
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(instance.instanceId));
                ImGui::TableNextColumn();
                if (ImGui::SmallButton(NextDayz::L10n::Label("Use")) && commands_.useItem)
                {
                    commands_.useItem(instance.instanceId);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(NextDayz::L10n::Label("Drop")) && commands_.dropItem)
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
                ImGui::TextDisabled("%s: %d / %d%s", NextDayz::L10n::Tr(container.displayName), inventory->ContainerUsed(container.containerId),
                                    container.Capacity(), container.weaponOnly ? NextDayz::L10n::Tr(" (weapons)") : "");
            }
        }

        ImGui::SeparatorText(NextDayz::L10n::Tr("Give"));
        const std::span<const FItemDef> definitions = AllItemDefs();
        giveItemIndex_ = std::clamp(giveItemIndex_, 0, static_cast<int>(definitions.size()) - 1);
        ImGui::SetNextItemWidth(140.0f);
        InputString("##itemFilter", itemFilter_, NextDayz::L10n::Tr("filter"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        const FItemDef& current = definitions[giveItemIndex_];
        if (ImGui::BeginCombo("##giveItem", NextDayz::L10n::Format("{} ({})", current.displayName, current.id).c_str()))
        {
            for (int index = 0; index < static_cast<int>(definitions.size()); ++index)
            {
                const FItemDef& definition = definitions[index];
                if (!Contains(definition.id, itemFilter_) && !Contains(definition.displayName, itemFilter_))
                {
                    continue;
                }
                if (ImGui::Selectable(NextDayz::L10n::Format("{} ({})", definition.displayName, definition.id).c_str(),
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
        if (ImGui::Button(NextDayz::L10n::Label("Give")) && commands_.giveItem)
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
        kit(NextDayz::L10n::Tr("Backpack"), {{"backpack", 1}});
        ImGui::SameLine();
        kit(NextDayz::L10n::Tr("Supplies"), {{"food_can", 2}, {"water_bottle", 2}, {"bandage", 3}});
        ImGui::SameLine();
        kit(NextDayz::L10n::Tr("AK + ammo"), {{"ak", 1}, {"ammo_545", 90}});
        ImGui::SameLine();
        kit(NextDayz::L10n::Tr("Axe"), {{"axe", 1}});
        ImGui::SameLine();
        kit(NextDayz::L10n::Tr("Map + compass"), {{"map", 1}, {"compass", 1}});
        ImGui::TextDisabled(NextDayz::L10n::Tr("Given items that do not fit are dropped; Use equips weapons and wears clothes."));
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
        KV(NextDayz::L10n::Tr("Slots"), "1: {}  2: {}  3: {}  (active {})", weapons->SlotWeaponId(0).empty() ? "-" : weapons->SlotWeaponId(0),
           weapons->SlotWeaponId(1).empty() ? "-" : weapons->SlotWeaponId(1),
           weapons->SlotWeaponId(2).empty() ? "-" : weapons->SlotWeaponId(2), weapons->ActiveSlot() + 1);
        KV(NextDayz::L10n::Tr("Ammo"), "{} in mag / {} reserve", weapons->AmmoInMag(), weapons->AmmoReserve(*systems_.inventory));
        KV(NextDayz::L10n::Tr("Action"), "{} {:.2f}{}{}", NextDayz::L10n::Tr(WeaponActionName(weapons->PresentationAction())), weapons->PresentationActionTime(),
           weapons->IsReloading() ? NextDayz::L10n::Tr("  reloading") : "",
           weapons->IsSwitching() ? NextDayz::L10n::Format("  switching to {} ({})", weapons->SwitchTargetSlot() + 1,
                                                weapons->SwitchCommitted() ? NextDayz::L10n::Tr("committed") : NextDayz::L10n::Tr("holstering"))
                                  : std::string());
        KV(NextDayz::L10n::Tr("Trigger"), "{}  shots fired {}", weapons->TriggerDown() ? NextDayz::L10n::Tr("DOWN") : NextDayz::L10n::Tr("up"), weapons->LastShotSequence());
        if (active)
        {
            ImGui::SeparatorText(NextDayz::L10n::Format("{} ({})", active->displayName, active->id).c_str());
            if (active->IsMelee())
            {
                KV(NextDayz::L10n::Tr("Melee"), "range {:.1f} m  swing {:.2f} s", active->meleeRange, active->fireInterval);
            }
            else
            {
                KV(NextDayz::L10n::Tr("Magazine"), "{} rounds of {}  {} rpm{}", active->magSize, AmmoDisplayName(active->ammo),
                   static_cast<int>(60.0f / std::max(active->fireInterval, 0.001f)), active->fullAuto ? NextDayz::L10n::Tr("  auto") : "");
                KV(NextDayz::L10n::Tr("Spread"), "hip {:.2f} deg  ads {:.2f} deg", glm::degrees(active->spreadHip),
                   glm::degrees(active->spreadAds));
                KV(NextDayz::L10n::Tr("Pellets"), "{}", active->pellets);
            }
            KV(NextDayz::L10n::Tr("Damage"), "{:.0f}  falloff {:.0f} m  impact x{:.2f}", active->baseDamage, active->damageFalloffDistance,
               active->impactScale);
            KV(NextDayz::L10n::Tr("Noise"), "{:.0f} m", active->noiseRadius);
            if (!active->IsMelee() && ImGui::Button(NextDayz::L10n::Label("Give 2 magazines of ammo")) && commands_.giveItem)
            {
                commands_.giveItem(std::string(AmmoItemId(active->ammo)), active->magSize * 2);
            }
        }
        ImGui::SeparatorText(NextDayz::L10n::Tr("Equip"));
        for (const FWeaponDef& definition : kWeapons)
        {
            if (ImGui::SmallButton(std::string(NextDayz::L10n::Tr(definition.displayName)).c_str()) && commands_.giveItem && commands_.useItem)
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

        ImGui::SeparatorText(NextDayz::L10n::Tr("Recent traces (F8 draws them)"));
        if (ImGui::BeginTable("##traces", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                                 ImGuiTableFlags_ScrollY, ImVec2(0.0f, 220.0f)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Shot"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Result"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Dist"), ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Hit"));
            ImGui::TableHeadersRow();
            for (auto it = traces_.rbegin(); it != traces_.rend(); ++it)
            {
                const FWeaponTraceEvent& trace = it->trace;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%llu.%d", static_cast<unsigned long long>(trace.sequence), trace.pellet);
                ImGui::TableNextColumn();
                Colored(trace.hitZombie ? Style::kGood : trace.hit ? Style::kBad : Style::kWarn,
                        trace.hitZombie ? NextDayz::L10n::Tr("infected") : trace.hit ? NextDayz::L10n::Tr("world") : NextDayz::L10n::Tr("miss"));
                ImGui::TableNextColumn();
                ImGui::Text("%.1f", glm::distance(trace.origin, trace.endPoint));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(trace.hit && !trace.hitZombie && commands_.nodeName
                                           ? commands_.nodeName(trace.hitInstanceId).c_str()
                                           : trace.melee ? NextDayz::L10n::Tr("melee") : "");
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
        KV(NextDayz::L10n::Tr("Active"), "{} / cap {}  ({} slots)", zombies->ActiveCount(), systems_.spawns ? systems_.spawns->GlobalCap() : 0,
           zombies->Capacity());
        KV(NextDayz::L10n::Tr("Alerted / kills"), "{} / {}  headshots {}  staggers {}", zombies->AlertedCount(), zombies->KillCount(),
           systems_.combat ? systems_.combat->HeadshotCount() : 0, zombies->StaggerCount());
        KV(NextDayz::L10n::Tr("Perception"), "sight x{:.2f} (night, crouch, fog, lantern)", zombies->SightScale());
        KV(NextDayz::L10n::Tr("Routing"), "{} solves, {} deferred last update (budget {}, {} expansions)", zombies->PathSolvesLastUpdate(),
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
                    NextDayz::L10n::Format("{} {}", NextDayz::L10n::Tr(ZombieStateName(static_cast<EZombieState>(index))), states[index]));
        }

        ImGui::SeparatorText(NextDayz::L10n::Tr("Control"));
        ImGui::SetNextItemWidth(170.0f);
        ImGui::SliderFloat("##zombieTime", &knobs_.zombieTimeScale, 0.0f, 2.0f, NextDayz::L10n::Tr("AI time x%.2f"));
        ImGui::SameLine();
        if (ImGui::SmallButton(knobs_.zombieTimeScale > 0.0f ? NextDayz::L10n::Tr("Freeze AI") : NextDayz::L10n::Tr("Resume AI")))
        {
            knobs_.zombieTimeScale = knobs_.zombieTimeScale > 0.0f ? 0.0f : 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(NextDayz::L10n::Label("Wake all (noise)")) && systems_.noise)
        {
            systems_.noise->Emit(feet, 400.0f, 2.0f, ENoiseType::Rifle, 1.5f);
        }
        ImGui::SetNextItemWidth(110.0f);
        const std::array<const char*, 4> profiles = {L10n::Tr(kProfiles[0]), L10n::Tr(kProfiles[1]),
                                                   L10n::Tr(kProfiles[2]), L10n::Tr(kProfiles[3])};
        ImGui::Combo("##profile", &spawnProfile_, profiles.data(), static_cast<int>(profiles.size()));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::SliderInt("##count", &spawnCount_, 1, 12, "x%d");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("##distance", &spawnDistance_, 1.5f, 60.0f, NextDayz::L10n::Tr("%.0f m ahead"));
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
        if (ImGui::Button(NextDayz::L10n::Label("Spawn ahead")))
        {
            glm::vec3 ahead = PlayerForward();
            ahead.y = 0.0f;
            ahead = glm::length(ahead) > 1.0e-3f ? glm::normalize(ahead) : glm::vec3(0.0f, 0.0f, 1.0f);
            spawnAt(feet + ahead * spawnDistance_);
        }
        ImGui::SameLine();
        if (ImGui::Button(NextDayz::L10n::Label("At crosshair")))
        {
            if (const std::optional<FRayHit> hit = CrosshairHit(300.0f))
            {
                spawnAt(hit->point);
            }
        }
        if (ImGui::Button(NextDayz::L10n::Label("Kill all")) && commands_.killZombie)
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
        if (ImGui::Button(NextDayz::L10n::Label("Despawn all")))
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
        ImGui::Checkbox(NextDayz::L10n::Label("List corpses"), &infectedShowDead_);

        if (const FZombieRuntime* selected = SelectedZombie())
        {
            ImGui::SeparatorText(NextDayz::L10n::Format("Selected {}", ZombieTag(*selected)).c_str());
            InfectedDetail(*selected);
        }

        ImGui::SeparatorText(NextDayz::L10n::Tr("Infected by distance (click to select, hover to locate)"));
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
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Id"), ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Profile"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("State"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("HP"), ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Dist"), ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Path"), ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Flags"));
            ImGui::TableHeadersRow();
            for (const FZombieRuntime* zombie : listed)
            {
                ImGui::PushID(static_cast<int>(zombie->handle.index));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool isSelected = selection_.kind == ESelection::Infected && selection_.zombie == zombie->handle;
                if (ImGui::Selectable(NextDayz::L10n::Format("Z{}:{}", zombie->handle.index, zombie->handle.generation).c_str(),
                                      isSelected, ImGuiSelectableFlags_SpanAllColumns))
                {
                    SelectZombie(zombie->handle);
                }
                if (ImGui::IsItemHovered())
                {
                    hoveredWorldPoint_ = zombie->position;
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(std::string(L10n::Tr(ZombieDef(zombie->profile).id)).c_str());
                ImGui::TableNextColumn();
                Colored(Style::ZombieState(zombie->state), NextDayz::L10n::Tr(ZombieStateName(zombie->state)));
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", zombie->health);
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", glm::distance(zombie->position, feet));
                ImGui::TableNextColumn();
                const size_t left = zombie->pathWaypoint < zombie->path.size() ? zombie->path.size() - zombie->pathWaypoint : 0;
                ImGui::Text("%zu%s", left, zombie->pathFailed ? "!" : "");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(NextDayz::L10n::Format("{}{}{}{}{}", zombie->seesPlayer ? NextDayz::L10n::Tr("sees ") : "",
                                                   zombie->heardNoise > 0.0f ? NextDayz::L10n::Tr("hears ") : "", zombie->aware ? NextDayz::L10n::Tr("aware ") : "",
                                                   zombie->Lame() ? NextDayz::L10n::Tr("lame ") : "",
                                                   zombie->stuckSeconds > 1.0f ? NextDayz::L10n::Tr("STUCK") : "").c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void NextDayzDebugger::InfectedDetail(const FZombieRuntime& zombie)
    {
        const glm::vec3 feet = PlayerFeet();
        const FZombieDef& definition = ZombieDef(zombie.profile);
        KVColored(NextDayz::L10n::Tr("State"), Style::ZombieState(zombie.state),
                  NextDayz::L10n::Format("{} for {:.1f} s{}", NextDayz::L10n::Tr(ZombieStateName(zombie.state)), zombie.stateSeconds,
                              zombie.state == EZombieState::Dead ? NextDayz::L10n::Format("  ({}, corpse {:.0f} s)",
                                                                               ZombieDeathName(zombie.death),
                                                                               zombie.corpseSeconds)
                                                                 : std::string()));
        KV(NextDayz::L10n::Tr("Health"), "{:.0f} / {:.0f}  poise {:.0f}/{:.0f}  leg {:.0f}{}", zombie.health, definition.maxHealth, zombie.poise,
           systems_.config ? systems_.config->Zombie.PoiseMax : 0.0f, zombie.legDamage, zombie.crippled ? NextDayz::L10n::Tr("  CRIPPLED") : "");
        KV(NextDayz::L10n::Tr("Position"), "({:.1f}, {:.1f}, {:.1f})  {:.1f} m away", zombie.position.x, zombie.position.y, zombie.position.z,
           glm::distance(zombie.position, feet));
        KV(NextDayz::L10n::Tr("Motion"), "speed {:.2f} m/s (wander {:.2f}, chase {:.2f}) x{:.2f}", zombie.Speed(), definition.wanderSpeed,
           definition.chaseSpeed, zombie.variant.speedScale);
        KV(NextDayz::L10n::Tr("Perception"), "{}  sight {:.0f} m  fov {:.0f}  heard {:.2f}  aware {}",
           zombie.seesPlayer ? NextDayz::L10n::Tr("SEES PLAYER") : NextDayz::L10n::Tr("no sight"), definition.sightDistance * systems_.zombies->SightScale(),
           definition.fieldOfViewDegrees, zombie.heardNoise, zombie.aware ? NextDayz::L10n::Tr("yes") : NextDayz::L10n::Tr("no"));
        KV(NextDayz::L10n::Tr("Lost sight"), "{:.2f} s  last known {:.1f} m away", zombie.lostSightSeconds,
           glm::distance(zombie.lastKnownPlayerPosition, zombie.position));
        const size_t left = zombie.pathWaypoint < zombie.path.size() ? zombie.path.size() - zombie.pathWaypoint : 0;
        KV(NextDayz::L10n::Tr("Route"), "{} of {} waypoints left  {} solves{}  repath {:.2f} s  stuck {:.2f} s", left, zombie.path.size(),
           zombie.pathSolves, zombie.pathFailed ? NextDayz::L10n::Tr("  LAST FAILED") : "", zombie.repathSeconds, zombie.stuckSeconds);
        KV(NextDayz::L10n::Tr("Attack"), "{} timer {:.2f}  cooldown {:.2f}  count {}{}",
           zombie.attack == EZombieAttack::Lunge ? NextDayz::L10n::Tr("lunge") : NextDayz::L10n::Tr("swipe"), zombie.attackTimer, zombie.attackCooldown,
           zombie.attackCount, zombie.attackCommitted ? NextDayz::L10n::Tr("  committed") : "");
        KV(NextDayz::L10n::Tr("Reaction"), "flinch {:.2f} s  stagger immunity {:.2f} s", zombie.flinchSeconds, zombie.staggerImmunity);
        KV(NextDayz::L10n::Tr("Variant"), "hunch {:.0f}  tilt {:.0f}  reach {:.2f}  phase {:.2f}{}", zombie.variant.hunchDegrees,
           zombie.variant.headTiltDegrees, zombie.variant.reach, zombie.variant.phase, zombie.variant.lame ? NextDayz::L10n::Tr("  born lame") : "");
        const int point = SpawnPointOf(zombie.handle);
        KV(NextDayz::L10n::Tr("Spawn"), "{}", point >= 0 ? NextDayz::L10n::Format("S{}", point) : std::string(NextDayz::L10n::Tr("by hand")));
        if (systems_.zombieVisuals && zombie.handle.index < systems_.zombieVisuals->size())
        {
            const ZombieRigVisual& visual = (*systems_.zombieVisuals)[zombie.handle.index];
            KV(NextDayz::L10n::Tr("Rig"), "{} / {}  flinch {:.1f} deg  hitboxes {}", visual.BaseClipName() ? visual.BaseClipName() : "-",
               visual.ActionClipName() ? visual.ActionClipName() : "-", visual.FlinchDegrees(),
               systems_.combat ? systems_.combat->Hitboxes(zombie.handle).size() : 0);
        }
        if (ImGui::Button(NextDayz::L10n::Label("Kill")) && commands_.killZombie)
        {
            commands_.killZombie(zombie.handle);
        }
        ImGui::SameLine();
        if (ImGui::Button(NextDayz::L10n::Label("Despawn")))
        {
            systems_.zombies->Recycle(zombie.handle);
            if (commands_.syncZombieVisuals) commands_.syncZombieVisuals();
        }
        ImGui::SameLine();
        if (ImGui::Button(knobs_.orbit && knobs_.orbitZombie ? NextDayz::L10n::Tr("Stop orbit") : NextDayz::L10n::Tr("Orbit camera")))
        {
            const bool on = !(knobs_.orbit && knobs_.orbitZombie);
            knobs_.orbit = on;
            knobs_.orbitZombie = on;
        }
        ImGui::SameLine();
        if (ImGui::Button(NextDayz::L10n::Label("Teleport next to it")) && commands_.teleport)
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
        KV(NextDayz::L10n::Tr("Points"), "{}  cap {}  next pass in {:.1f} s", spawns->Points().size(), spawns->GlobalCap(),
           spawns->NextEvaluationSeconds());
        KV(NextDayz::L10n::Tr("Totals"), "{} spawned  {} recycled  {} rejected (visible)", spawns->SpawnCount(), spawns->RecycledCount(),
           spawns->RejectedVisible());
        KV(NextDayz::L10n::Tr("Window"), "{:.0f} .. {:.0f} m from the player, out of view", tuning.minimumDistance, tuning.maximumDistance);
        KV(NextDayz::L10n::Tr("Recycle"), "calm for {:.0f} s beyond {:.0f} m", tuning.recycleCalmSeconds, tuning.recycleDistance);
        KV(NextDayz::L10n::Tr("Respawn"), "{:.0f} s + up to {:.0f} s after a group is gone", tuning.respawnMinSeconds, tuning.respawnJitterSeconds);

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
                    NextDayz::L10n::Format("{} {}", NextDayz::L10n::Tr(SpawnVerdictName(static_cast<ESpawnVerdict>(index))), verdicts[index]));
        }
        bool on = LayerOn(LayerSpawns);
        if (ImGui::Checkbox(NextDayz::L10n::Label("Show spawn points and activation rings in the world"), &on))
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
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Point"), ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Profile"), ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Out"), ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Last verdict"), ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Dist"), ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Cooldown / calm"));
            ImGui::TableHeadersRow();
            const double now = spawns->WorldSeconds();
            for (const int index : order)
            {
                const FZombieSpawnPoint& point = spawns->Points()[index];
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool isSelected = selection_.kind == ESelection::SpawnPoint && selection_.index == index;
                if (ImGui::Selectable(NextDayz::L10n::Format("S{}", index).c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns))
                {
                    SelectSpawnPoint(index);
                }
                if (ImGui::IsItemHovered())
                {
                    hoveredWorldPoint_ = point.position;
                }
                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem(NextDayz::L10n::Label("Teleport here")) && commands_.teleport)
                    {
                        commands_.teleport(point.position + glm::vec3(2.0f, 0.0f, 2.0f));
                    }
                    ImGui::EndPopup();
                }
                int out = 0;
                for (const FZombieHandle& handle : point.active) out += handle.IsValid() ? 1 : 0;
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(std::string(L10n::Tr(ZombieDef(point.profile).id)).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d/%d", out, point.groupSize);
                ImGui::TableNextColumn();
                Colored(Style::SpawnVerdict(point.verdict), NextDayz::L10n::Tr(SpawnVerdictName(point.verdict)));
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", point.playerDistance);
                ImGui::TableNextColumn();
                const double cooldown = std::max(0.0, point.cooldownUntil - now);
                ImGui::Text("%s%s", cooldown > 0.0 ? NextDayz::L10n::Format("{:.0f} s  ", cooldown).c_str() : "",
                            point.farCalmSeconds > 0.0f ? NextDayz::L10n::Format("calm {:.0f} s", point.farCalmSeconds).c_str() : "");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled(NextDayz::L10n::Tr("Right-click a row to teleport. Verdicts update once per pass."));
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
        KV(NextDayz::L10n::Tr("Entries"), "{}  ({} spots, {} piles, {} zones)", loot->Entries().size(), loot->SpotCount(), loot->PileCount(),
           loot->Zones().size());
        int reserved = 0;
        for (const FLootEntry& entry : loot->Entries()) reserved += entry.state == FLootEntry::EState::Reserved ? 1 : 0;
        KV(NextDayz::L10n::Tr("State"), "{} available  {} reserved  {} cooling", loot->RemainingCount(), reserved, loot->CooldownCount());
        KV(NextDayz::L10n::Tr("Restock rule"), "away {:.0f} m from the zone, unseen {:.0f} s, category cooldown", tuning.minimumPlayerDistance,
           tuning.minimumOffscreenSeconds);
        KVColored(NextDayz::L10n::Tr("Scarcity"), loot->FoodScarce() || loot->MedicalScarce() ? Style::kWarn : Style::kMuted,
                  NextDayz::L10n::Format("food {}  medical {}", loot->FoodScarce() ? NextDayz::L10n::Tr("SCARCE") : NextDayz::L10n::Tr("ok"),
                              loot->MedicalScarce() ? NextDayz::L10n::Tr("SCARCE") : NextDayz::L10n::Tr("ok")));

        if (ImGui::BeginTable("##categories", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Category"));
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Avail"), ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Total"), ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Cooling"), ImGuiTableColumnFlags_WidthFixed, 52.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Cap"), ImGuiTableColumnFlags_WidthFixed, 36.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Cooldown"), ImGuiTableColumnFlags_WidthFixed, 96.0f);
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
                        NextDayz::L10n::Format("{}{}", NextDayz::L10n::Tr(LootCategoryName(category)), available == 0 ? NextDayz::L10n::Tr("  MISSING") : ""));
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
            Colored(count > 0 ? Style::kGood : Style::kBad, NextDayz::L10n::Format("{} {}", name, count));
            ImGui::SameLine();
        };
        ImGui::TextDisabled(NextDayz::L10n::Tr("Coverage:"));
        ImGui::SameLine();
        coverage(NextDayz::L10n::Tr("food"), food);
        coverage(NextDayz::L10n::Tr("medical"), medical);
        coverage(NextDayz::L10n::Tr("backpacks"), backpacks);
        coverage(NextDayz::L10n::Tr("weapons"), guns);
        coverage(NextDayz::L10n::Tr("ammo"), ammo);
        coverage(NextDayz::L10n::Tr("wells"), wells);
        ImGui::NewLine();

        if (ImGui::Button(NextDayz::L10n::Label("Restock everything now")))
        {
            Log(EChannel::Loot, 1, NextDayz::L10n::Message("Debug restock: {} entries", loot->DebugRestockAll(*systems_.engine)));
        }
        ImGui::SameLine();
        bool zones = LayerOn(LayerLootZones);
        if (ImGui::Checkbox(NextDayz::L10n::Label("Zones"), &zones)) SetLayer(LayerLootZones, zones);
        ImGui::SameLine();
        bool markers = LayerOn(LayerLoot);
        if (ImGui::Checkbox(NextDayz::L10n::Label("Markers (F7)"), &markers)) SetLayer(LayerLoot, markers);

        // Selected entry.
        if (selection_.kind == ESelection::Loot && selection_.index >= 0 &&
            selection_.index < static_cast<int>(loot->Entries().size()))
        {
            const FLootEntry& entry = loot->Entries()[selection_.index];
            const FLootSlot* slot = loot->ResolveSlot(entry.directorHandle);
            ImGui::SeparatorText(NextDayz::L10n::Format("Selected L{}: {}", selection_.index, entry.displayName).c_str());
            KV(NextDayz::L10n::Tr("Source"), "{}  {} / {}  zone {}", entry.source == ELootSource::Spot ? NextDayz::L10n::Tr("spot")
                                                 : entry.source == ELootSource::Pile ? NextDayz::L10n::Tr("pile") : NextDayz::L10n::Tr("authored"),
               NextDayz::L10n::Tr(LootProfileName(entry.profile)), NextDayz::L10n::Tr(LootCategoryName(entry.category)), entry.zone);
            for (const FLootStack& stack : entry.contents)
            {
                KV(NextDayz::L10n::Tr("  contains"), "{} x{}{}", stack.itemId, stack.count,
                   stack.loadedAmmo > 0 ? NextDayz::L10n::Format(" ({} loaded)", stack.loadedAmmo) : std::string());
            }
            if (slot && entry.state == FLootEntry::EState::Cooldown)
            {
                KVColored(NextDayz::L10n::Tr("Restock"), Style::LootBlock(slot->block),
                          NextDayz::L10n::Format("blocked by {}  ready in {:.0f} s  unseen since {}", NextDayz::L10n::Tr(LootRespawnBlockName(slot->block)),
                                      std::max(0.0, loot->Director().ReadyAt(entry.directorHandle) - loot->WorldSeconds()),
                                      slot->offscreenSince < 0.0 ? std::string("-")
                                                                 : NextDayz::L10n::Format("{:.0f} s", loot->WorldSeconds() - slot->offscreenSince)));
                if (ImGui::Button(NextDayz::L10n::Label("Restock this")))
                {
                    loot->DebugRestock(selection_.index, *systems_.engine);
                }
                ImGui::SameLine();
            }
            if (ImGui::Button(NextDayz::L10n::Label("Teleport next to it")) && commands_.teleport)
            {
                commands_.teleport(entry.worldPos + glm::vec3(0.8f, 0.0f, 0.8f));
            }
        }

        ImGui::SeparatorText(NextDayz::L10n::Tr("Entries by distance"));
        const std::array<const char*, 7> categoryItems = {NextDayz::L10n::Tr("any category"), NextDayz::L10n::Tr("Food/Water"), NextDayz::L10n::Tr("Medical"), NextDayz::L10n::Tr("Ammo"),
                                                                     NextDayz::L10n::Tr("Weapon"), NextDayz::L10n::Tr("Clothing"), NextDayz::L10n::Tr("Misc")};
        const std::array<const char*, 5> stateItems = {NextDayz::L10n::Tr("any state"), NextDayz::L10n::Tr("Available"), NextDayz::L10n::Tr("Reserved"), NextDayz::L10n::Tr("Cooldown"), NextDayz::L10n::Tr("Empty")};
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
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Entry"), ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Contents"));
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Profile"), ImGuiTableColumnFlags_WidthFixed, 76.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("State"), ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Restock"), ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Dist"), ImGuiTableColumnFlags_WidthFixed, 40.0f);
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
                    if (ImGui::Selectable(NextDayz::L10n::Format("L{}", index).c_str(), isSelected,
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
                    ImGui::TextUnformatted(NextDayz::L10n::Tr(LootProfileName(entry.profile)));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(entry.state == FLootEntry::EState::Available  ? NextDayz::L10n::Tr("Available")
                                           : entry.state == FLootEntry::EState::Reserved ? NextDayz::L10n::Tr("Reserved")
                                           : entry.state == FLootEntry::EState::Cooldown ? NextDayz::L10n::Tr("Cooldown")
                                                                                          : NextDayz::L10n::Tr("Empty"));
                    ImGui::TableNextColumn();
                    if (entry.state == FLootEntry::EState::Cooldown)
                    {
                        const FLootSlot* slot = loot->ResolveSlot(entry.directorHandle);
                        const double readyIn =
                            std::max(0.0, loot->Director().ReadyAt(entry.directorHandle) - loot->WorldSeconds());
                        Colored(slot ? Style::LootBlock(slot->block) : Style::kMuted,
                                NextDayz::L10n::Format("{} {:.0f}s", slot ? NextDayz::L10n::Tr(LootRespawnBlockName(slot->block)) : "-", readyIn));
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
            KV(NextDayz::L10n::Tr("Clock"), "{:02d}:{:02d}  ({:.0f} game min/real s)", time->HourInt(), time->MinuteInt(), time->TimeScale());
            KV(NextDayz::L10n::Tr("Light"), "daylight {:.2f}  storm {:.2f}  chill {:.2f}", time->Daylight(), time->Storm(), time->Chill());
            float hour = static_cast<float>(time->Hour());
            if (ImGui::SliderFloat(NextDayz::L10n::Label("Hour"), &hour, 0.0f, 23.99f, "%.2f"))
            {
                time->SetHour(hour);
            }
            float scale = static_cast<float>(time->TimeScale());
            if (ImGui::SliderFloat(NextDayz::L10n::Label("Clock speed"), &scale, 0.0f, 600.0f, NextDayz::L10n::Tr("%.0f game min/real s")))
            {
                time->TimeScaleRef() = scale;
            }
            for (const auto& [label, minutes] : {std::pair{NextDayz::L10n::Tr("+1 h"), 60.0}, std::pair{NextDayz::L10n::Tr("+6 h"), 360.0}, std::pair{NextDayz::L10n::Tr("+12 h"), 720.0}})
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
                if (ImGui::RadioButton(NextDayz::L10n::Tr(WeatherName(weather)), time->Weather() == weather))
                {
                    time->SetWeather(weather);
                }
            }
        }
        KV(NextDayz::L10n::Tr("Lantern"), "{}", facts.lanternLit ? NextDayz::L10n::Format("lit, {:.1f} min of fuel", facts.lanternSeconds / 60.0f)
                                             : std::string(NextDayz::L10n::Tr("off")));
        KV(NextDayz::L10n::Tr("Supply cache"), "{}", facts.cacheMarker ? NextDayz::L10n::Format("{:.0f} m away", glm::distance(*facts.cacheMarker, PlayerFeet()))
                                                   : std::string(NextDayz::L10n::Tr("none")));
        KV(NextDayz::L10n::Tr("Firecrackers"), "{} live", facts.firecrackers.size());

        if (systems_.anchors)
        {
            const WorldAnchorRegistry& anchors = *systems_.anchors;
            ImGui::SeparatorText(NextDayz::L10n::Tr("Map anchors"));
            KV(NextDayz::L10n::Tr("Counts"), "{} POIs  {} loot  {} infected  {} wells  {} safe spawns", anchors.Count(EWorldAnchorType::Poi),
               anchors.Count(EWorldAnchorType::Loot), anchors.Count(EWorldAnchorType::Zombie),
               anchors.Count(EWorldAnchorType::Well), anchors.Count(EWorldAnchorType::PlayerSafe));
            bool on = LayerOn(LayerAnchors);
            if (ImGui::Checkbox(NextDayz::L10n::Label("Show POIs, wells and markers in the world"), &on))
            {
                SetLayer(LayerAnchors, on);
            }
            const glm::vec3 feet = PlayerFeet();
            if (ImGui::BeginTable("##pois", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                                   ImGuiTableFlags_ScrollY, ImVec2(0.0f, 220.0f)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(NextDayz::L10n::Tr("POI"));
                ImGui::TableSetupColumn(NextDayz::L10n::Tr("Kind"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn(NextDayz::L10n::Tr("Dist"), ImGuiTableColumnFlags_WidthFixed, 50.0f);
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
                    ImGui::TextUnformatted(NextDayz::L10n::Tr(anchor.label));
                    if (ImGui::IsItemHovered())
                    {
                        hoveredWorldPoint_ = anchor.worldPos;
                    }
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(L10n::Tr(anchor.profile));
                    ImGui::TableNextColumn();
                    ImGui::Text("%.0f", glm::distance(anchor.worldPos, feet));
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton(NextDayz::L10n::Label("Go")) && commands_.teleport)
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
            ImGui::SeparatorText(NextDayz::L10n::Tr("Infected navigation"));
            KV(NextDayz::L10n::Tr("Grid"), "{}", grid->IsBuilt() ? NextDayz::L10n::Format("{} x {} cells at {:.1f} m", grid->GetWidth(), grid->GetHeight(),
                                                           grid->GetCellSize())
                                             : std::string(NextDayz::L10n::Tr("NOT BUILT")));
            const glm::vec3 feet = PlayerFeet();
            KV(NextDayz::L10n::Tr("Under you"), "{}", grid->IsBuilt() ? (grid->IsWalkable(feet) ? NextDayz::L10n::Tr("walkable") : NextDayz::L10n::Tr("NOT walkable")) : "-");
            bool on = LayerOn(LayerNav);
            if (ImGui::Checkbox(NextDayz::L10n::Label("Show cells"), &on))
            {
                SetLayer(LayerNav, on);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderFloat("##navRadius", &knobs_.navRadius, 6.0f, 80.0f, NextDayz::L10n::Tr("radius %.0f m"));
            ImGui::SameLine();
            if (ImGui::Button(NextDayz::L10n::Label("Route to crosshair")) && grid->IsBuilt())
            {
                if (const std::optional<FRayHit> hit = CrosshairHit(300.0f))
                {
                    const auto start = std::chrono::steady_clock::now();
                    navProbePath_ = grid->FindPath(feet, hit->point, feet.y,
                                                   systems_.config ? systems_.config->Zombie.MaxPathExpansions : 0);
                    const float ms =
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
                    Log(EChannel::World, 0,
                        NextDayz::L10n::Message("Nav probe: {} waypoints to {:.0f} m in {:.2f} ms", navProbePath_->size(),
                                    glm::distance(feet, hit->point), ms));
                    SetLayer(LayerNav, true);
                }
            }
            if (navProbePath_)
            {
                ImGui::SameLine();
                ImGui::Text(NextDayz::L10n::Tr("%zu waypoints"), navProbePath_->size());
                ImGui::SameLine();
                if (ImGui::SmallButton(NextDayz::L10n::Label("Clear route##probe")))
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
            KV(NextDayz::L10n::Tr("Kills"), "{}  headshots {}  staggers {}", systems_.zombies->KillCount(), systems_.combat->HeadshotCount(),
               systems_.zombies->StaggerCount());
            KV(NextDayz::L10n::Tr("Last hit"), "{}", last.IsValid() ? NextDayz::L10n::Format("Z{}:{} {}", last.index, last.generation,
                                                              NextDayz::L10n::Tr(HitZoneName(systems_.combat->LastHitZone())))
                                                : std::string("-"));
            if (systems_.config)
            {
                const FZombieConfig& zombie = systems_.config->Zombie;
                KV(NextDayz::L10n::Tr("Zone damage"), "head x{:.1f}  torso x{:.1f}  arm x{:.1f}  leg x{:.1f}", zombie.HeadMultiplier,
                   zombie.TorsoMultiplier, zombie.LimbMultiplier, zombie.LegMultiplier);
                KV(NextDayz::L10n::Tr("Poise"), "{:.0f} max  {:.0f}/s recovery  cripple at {:.0f} leg damage", zombie.PoiseMax,
                   zombie.PoiseRecoverPerSecond, zombie.CrippleLegDamage);
            }
        }
        if (systems_.effects && systems_.audio)
        {
            KV(NextDayz::L10n::Tr("Effects"), "{} particles  {} tracers  flash {}", systems_.effects->ActiveParticles(),
               systems_.effects->ActiveTracers(), systems_.effects->FlashVisible() ? NextDayz::L10n::Tr("on") : NextDayz::L10n::Tr("off"));
            KV(NextDayz::L10n::Tr("Audio"), "{} sounds played", systems_.audio->PlayedCount());
        }
        ImGui::SetNextItemWidth(170.0f);
        ImGui::SliderFloat("##fxTime", &knobs_.fxTimeScale, 0.0f, 2.0f, NextDayz::L10n::Tr("FX time x%.2f"));
        ImGui::SameLine();
        bool hits = LayerOn(LayerHitboxes);
        if (ImGui::Checkbox(NextDayz::L10n::Label("Hitboxes"), &hits)) SetLayer(LayerHitboxes, hits);
        ImGui::SameLine();
        bool traces = LayerOn(LayerTraces);
        if (ImGui::Checkbox(NextDayz::L10n::Label("Traces"), &traces)) SetLayer(LayerTraces, traces);
        ImGui::SameLine();
        bool noise = LayerOn(LayerNoise);
        if (ImGui::Checkbox(NextDayz::L10n::Label("Noise"), &noise)) SetLayer(LayerNoise, noise);

        if (systems_.noise)
        {
            ImGui::SeparatorText(NextDayz::L10n::Format("Live noise ({})", systems_.noise->Events().size()).c_str());
            const glm::vec3 feet = PlayerFeet();
            for (const FNoiseEvent& event : systems_.noise->Events())
            {
                ImGui::Text(NextDayz::L10n::Tr("#%llu %-8s r %3.0f m  x%.1f  %.2f s left  %.0f m away"),
                            static_cast<unsigned long long>(event.sequence), NextDayz::L10n::Tr(NoiseTypeName(event.type)), event.radius,
                            event.strength, event.remainingSeconds, glm::distance(event.position, feet));
                if (ImGui::IsItemHovered())
                {
                    hoveredWorldPoint_ = event.position;
                }
            }
        }

        ImGui::SeparatorText(NextDayz::L10n::Tr("Damage feed (newest first)"));
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
                    NextDayz::L10n::Format("{:7.1f}  {}{}", event.time, event.text,
                                event.repeats > 1 ? NextDayz::L10n::Format("  x{}", event.repeats) : std::string()));
            if (event.hasPosition && ImGui::IsItemHovered())
            {
                hoveredWorldPoint_ = event.position;
            }
        }
        if (shown == 0)
        {
            ImGui::TextDisabled(NextDayz::L10n::Tr("No hits yet"));
        }
    }

    // ------------------------------------------------------------------ log

    void NextDayzDebugger::TabLog()
    {
        for (size_t index = 0; index < kChannelCount; ++index)
        {
            const EChannel channel = static_cast<EChannel>(index);
            ImGui::PushStyleColor(ImGuiCol_Text, Style::V4(Style::Channel(channel)));
            ImGui::Checkbox(NextDayz::L10n::Format("{} {}##ch{}", NextDayz::L10n::Tr(ChannelName(channel)), log_.ChannelCount(channel), index).c_str(),
                            &channelFilter_[index]);
            ImGui::PopStyleColor();
            if ((index + 1) % 6 != 0)
            {
                ImGui::SameLine();
            }
        }
        ImGui::NewLine();
        if (ImGui::SmallButton(NextDayz::L10n::Label("All")))
        {
            channelFilter_.fill(true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(NextDayz::L10n::Label("None")))
        {
            channelFilter_.fill(false);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        InputString("##logFilter", logFilter_, NextDayz::L10n::Tr("text filter"));
        ImGui::SameLine();
        ImGui::Checkbox(NextDayz::L10n::Label("Follow"), &logFollow_);
        ImGui::SameLine();
        if (ImGui::SmallButton(NextDayz::L10n::Label("Copy")))
        {
            std::string text;
            for (size_t index = 0; index < log_.Size(); ++index)
            {
                const FEvent& event = log_.At(index);
                if (channelFilter_[static_cast<size_t>(event.channel)] && Contains(event.text.Display(), logFilter_))
                {
                    text += NextDayz::L10n::Format("{:.2f} [{}] {}{}\n", event.time, NextDayz::L10n::Tr(ChannelName(event.channel)), event.text,
                                        event.repeats > 1 ? NextDayz::L10n::Format(" x{}", event.repeats) : std::string());
                }
            }
            ImGui::SetClipboardText(text.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(NextDayz::L10n::Label("Clear log")))
        {
            log_.Clear();
        }
        ImGui::SameLine();
        ImGui::TextDisabled(NextDayz::L10n::Tr("%zu kept / %llu total"), log_.Size(), static_cast<unsigned long long>(log_.TotalPushed()));

        std::vector<int> visible;
        visible.reserve(log_.Size());
        for (size_t index = 0; index < log_.Size(); ++index)
        {
            const FEvent& event = log_.At(index);
            if (channelFilter_[static_cast<size_t>(event.channel)] && Contains(event.text.Display(), logFilter_))
            {
                visible.push_back(static_cast<int>(index));
            }
        }
        if (ImGui::BeginTable("##log", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                              ImGuiTableFlags_ScrollY, ImVec2(0.0f, 0.0f)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Time"), ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Channel"), ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupColumn(NextDayz::L10n::Tr("Event"));
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
                    Colored(Style::Channel(event.channel), NextDayz::L10n::Tr(ChannelName(event.channel)));
                    ImGui::TableNextColumn();
                    const std::string text = event.repeats > 1 ? NextDayz::L10n::Format("{}  x{}", event.text, event.repeats)
                                                               : event.text.Display();
                    ImGui::PushStyleColor(ImGuiCol_Text, Style::V4(Style::Severity(event.severity)));
                    ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_SpanAllColumns |
                                                               ImGuiSelectableFlags_AllowDoubleClick);
                    ImGui::PopStyleColor();
                    if (event.hasPosition && ImGui::IsItemHovered())
                    {
                        hoveredWorldPoint_ = event.position;
                        ImGui::SetTooltip(NextDayz::L10n::Tr("(%.1f, %.1f, %.1f)  double-click: teleport there"), event.position.x,
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
