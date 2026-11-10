#include "Engine/Common/CoreMinimal.hpp"
#include "NextDayzGameInstance.hpp"

#include <imgui.h>

#include "Engine/Options.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Platform/UserPaths.hpp"
#include "Engine/Utilities/FileHelper.hpp"

void NextDayzGameInstance::ConfigureSessionCVars(NextCVar::FCVarSystem& cvars)
{
    cvars.RegisterBool("nextdayz.ui.sessionMenu", !GOption->AgentValidation && GOption->SceneName.empty(),
        &sessionMenuOpen_, NextCVar::ECVarFlags::None, "Open the play mode and Master server menu");
    cvars.RegisterString("nextdayz.settings.survivorName", "", &preferredSurvivorName_, NextCVar::ECVarFlags::Archive,
        "Last survivor name used to log in (tokens are never stored)");
}

void NextDayzGameInstance::InitializeSessionMenu()
{
    std::string error;
    if (!NextDayz::ReadMasterServers(Utilities::FileHelper::ResolvePlatformFilePath("assets/configs/nextdayz_servers.json"),
        masterServers_, error))
    {
        SPDLOG_WARN("[NextDayz] cannot read Master server directory: {}", error);
        masterServers_ = {{"Local Master", "http://127.0.0.1:7780"}};
    }
    if (!GOption->AgentValidation)
    {
        const auto path = NextPlatform::UserPaths::GetUserDataDir("NextDayz") / "servers.json";
        if (std::filesystem::exists(path))
        {
            std::vector<NextDayz::FMasterServer> saved;
            if (NextDayz::ReadMasterServers(path, saved, error))
                for (auto& server : saved)
                    if (masterServers_.size() < 64 && std::none_of(masterServers_.begin(), masterServers_.end(), [&](const auto& entry) { return entry.url == server.url; }))
                        masterServers_.push_back(std::move(server));
            else sessionMenuMessage_ = NextDayz::L10n::Message("Cannot read saved servers: {}", error);
        }
    }
    if (!netMasterCvar_.empty())
    {
        std::string url;
        if (NextDayz::NormalizeMasterUrl(netMasterCvar_, url))
        {
            netMasterCvar_ = url;
            const auto existing = std::find_if(masterServers_.begin(), masterServers_.end(), [&](const auto& entry) { return entry.url == url; });
            if (existing == masterServers_.end()) masterServers_.push_back({"Command-line Master", url});
            const auto found = std::find_if(masterServers_.begin(), masterServers_.end(), [&](const auto& entry) { return entry.url == url; });
            selectedMasterServer_ = static_cast<int>(found - masterServers_.begin());
        }
    }
    std::snprintf(masterUrlInput_.data(), masterUrlInput_.size(), "%s",
        masterServers_.empty() ? "http://127.0.0.1:7780" : masterServers_[selectedMasterServer_].url.c_str());
    std::snprintf(survivorNameInput_.data(), survivorNameInput_.size(), "%s",
        netNameCvar_.empty() ? preferredSurvivorName_.c_str() : netNameCvar_.c_str());
    std::snprintf(directHostInput_.data(), directHostInput_.size(), "%s", netHostCvar_.c_str());
    directPort_ = netPortCvar_;
    sessionMenuTab_ = NetUsesMaster() ? 1 : 0;
    if (netMode_ == ENetMode::Server || netAutoDeployCvar_ || (netMode_ == ENetMode::Client && !NetUsesMaster()))
        sessionMenuOpen_ = false;
}

const char* NextDayzGameInstance::SessionModeLabel() const
{
    if (trainingSession_) return "OFFLINE TRAINING";
    if (netMode_ == ENetMode::Server) return "DEDICATED SERVER";
    if (netMode_ == ENetMode::Client) return NetUsesMaster() ? "MASTER MULTIPLAYER" : "DIRECT MULTIPLAYER";
    return "SINGLE-PLAYER CAMPAIGN";
}

void NextDayzGameInstance::OpenSessionMenu()
{
    if (settingsOpen_) return;
    sessionMenuOpen_ = true;
    safehouseTab_ = -1;
    SetMouseCaptured(false);
}

bool NextDayzGameInstance::PrepareSessionSwitch()
{
    if (switchingCampaignContext_ || !sceneReady_ || !loadingMapPath_.empty() || pendingCampaign_) return false;
    if (CampaignEnabled() && !campaignSaveBlocked_ && !SaveCampaign())
    {
        sessionMenuMessage_ = "Cannot save the local campaign. Please retry before switching modes.";
        return false;
    }
    switchingCampaignContext_ = true;
    pendingCampaign_.reset();
    cabinCampaign_.reset();
    pendingDeployment_ = pendingFreshMap_ = false;
    showMap_ = showInventory_ = paused_ = false;
    safehouseTab_ = -1;
    actions_.Reset();
    weapons_.SetTriggerDown(false);
    SetMouseCaptured(false);
    return true;
}

void NextDayzGameInstance::StartTraining()
{
    const auto training = std::find_if(expeditionMaps_.begin(), expeditionMaps_.end(), [](const auto& map) { return map.published && map.training; });
    if (training == expeditionMaps_.end() || !std::filesystem::exists(Utilities::FileHelper::ResolvePlatformFilePath(training->scene.c_str())))
    {
        sessionMenuMessage_ = "Training ground files are missing.";
        return;
    }
    if (!PrepareSessionSwitch()) return;
    RetireNetworkSession();
    netMode_ = ENetMode::Offline;
    trainingSession_ = true;
    sessionMenuOpen_ = false;
    loadingCabin_ = false;
    cabinLobby_ = false;
    loadingMapPath_ = training->scene;
    GetEngine().RequestLoadScene({.filename = loadingMapPath_});
}

void NextDayzGameInstance::StartOfflineLobby()
{
    if (netMode_ == ENetMode::Offline && !trainingSession_ && cabinScene_)
    {
        sessionMenuOpen_ = false;
        return;
    }
    if (!PrepareSessionSwitch()) return;
    RetireNetworkSession();
    netMode_ = ENetMode::Offline;
    trainingSession_ = false;
    sessionMenuOpen_ = false;
    loadingCabin_ = cabinLobby_ = true;
    GetEngine().RequestLoadScene({.filename = cabinScenePath});
}

void NextDayzGameInstance::ReturnToSessionLobby()
{
    if (NetUsesMaster() && netMode_ == ENetMode::Client) NetReturnToLobby("Returned to the lobby. Your survivor is saved by the server.");
    else if (netMode_ == ENetMode::Offline && !trainingSession_ && CampaignEnabled()) RequestCabin();
    else StartOfflineLobby();
}

void NextDayzGameInstance::DrawSessionMenu()
{
    using namespace NextDayz::L10n;
    if (!sessionMenuOpen_ || settingsOpen_ || netMode_ == ENetMode::Server) return;
    constexpr const char* popup = "##nextdayz_session_menu";
    const bool opening = !ImGui::IsPopupOpen(popup);
    if (opening) ImGui::OpenPopup(popup);
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(760.0f, viewport->WorkSize.x - 24), std::min(650.0f, viewport->WorkSize.y - 24)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24,20));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10,8));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.035f,0.065f,0.075f,0.98f));
    if (ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextUnformatted(Tr("PLAY MODE / SERVERS"));
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextColored(ImVec4(0.88f,0.74f,0.46f,1), "%s", Tr(SessionModeLabel()));
        const auto account = NetAccountName();
        ImGui::TextWrapped("%s", NetLoggedIn() ? Format("Logged in: {}", account).c_str() : Tr("Not logged in. Offline play is available."));
        ImGui::Separator();
        const char* tabs[] = {"Offline", "Master servers", "Direct connection"};
        const float width = (ImGui::GetContentRegionAvail().x - 16) / 3;
        for (int index = 0; index < 3; ++index)
        {
            if (index) ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, sessionMenuTab_ == index ? ImVec4(0.40f,0.31f,0.17f,1) : ImVec4(0.10f,0.16f,0.18f,1));
            if (ImGui::Button(Label(tabs[index]), ImVec2(width,36))) sessionMenuTab_ = index;
            ImGui::PopStyleColor();
        }
        ImGui::BeginChild("session_content", ImVec2(0,-90));
        ImGui::BeginDisabled(switchingCampaignContext_ || !sceneReady_ || !loadingMapPath_.empty());
        if (sessionMenuTab_ == 0)
        {
            ImGui::TextWrapped("%s", Tr("Practice offline with every weapon and item. Training never changes your campaign or online storage."));
            if (ImGui::Button(Label("ENTER TRAINING GROUND"), ImVec2(-1,48))) StartTraining();
            ImGui::Spacing();
            ImGui::TextWrapped("%s", Tr("Continue the local campaign with its own survivor, safehouse and save."));
            if (ImGui::Button(Label("SINGLE-PLAYER CAMPAIGN"), ImVec2(-1,44))) StartOfflineLobby();
        }
        else
        {
            ImGui::BeginDisabled(!NetworkAvailable());
            if (sessionMenuTab_ == 1)
            {
                ImGui::TextWrapped("%s", Tr("Select a Master server, then log in. Each Master has its own account and storage."));
                ImGui::BeginChild("master_server_list", ImVec2(0,124), ImGuiChildFlags_Borders);
                for (size_t index = 0; index < masterServers_.size(); ++index)
                {
                    ImGui::PushID(static_cast<int>(index));
                    const auto& server = masterServers_[index];
                    if (ImGui::Selectable((std::string(Tr(server.name)) + "\n" + server.url).c_str(), selectedMasterServer_ == static_cast<int>(index), 0, ImVec2(0,42)))
                    {
                        selectedMasterServer_ = static_cast<int>(index);
                        std::snprintf(masterUrlInput_.data(), masterUrlInput_.size(), "%s", server.url.c_str());
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::TextUnformatted(Tr("Master address"));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##master_address", masterUrlInput_.data(), masterUrlInput_.size());
                ImGui::TextUnformatted(Tr("Server name (optional)"));
                ImGui::SetNextItemWidth(-150);
                ImGui::InputText("##master_name", masterNameInput_.data(), masterNameInput_.size());
                ImGui::SameLine();
                if (ImGui::Button(Label("SAVE SERVER"), ImVec2(140,0)))
                {
                    std::string url;
                    if (!NextDayz::NormalizeMasterUrl(masterUrlInput_.data(), url)) sessionMenuMessage_ = "Enter an HTTP Master address, for example http://127.0.0.1:7780.";
                    else
                    {
                        const auto found = std::find_if(masterServers_.begin(), masterServers_.end(), [&](const auto& entry) { return entry.url == url; });
                        if (found == masterServers_.end() && masterServers_.size() < 64)
                        {
                            masterServers_.push_back({masterNameInput_[0] ? std::string(masterNameInput_.data()) : url, url});
                            selectedMasterServer_ = static_cast<int>(masterServers_.size()) - 1;
                        }
                        else if (found != masterServers_.end())
                        {
                            if (masterNameInput_[0]) found->name = masterNameInput_.data();
                            selectedMasterServer_ = static_cast<int>(found - masterServers_.begin());
                        }
                        std::string error;
                        if (!GOption->AgentValidation && !NextDayz::WriteMasterServers(NextPlatform::UserPaths::GetUserDataDir("NextDayz") / "servers.json", masterServers_, error))
                            sessionMenuMessage_ = Message("Cannot save servers: {}", error);
                        else sessionMenuMessage_ = "Server saved. Select it to log in.";
                    }
                }
            }
            else
            {
                ImGui::TextWrapped("%s", Tr("Connect to a standalone game server. Master accounts and storage are not used."));
                ImGui::TextUnformatted(Tr("Server host"));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##direct_host", directHostInput_.data(), directHostInput_.size());
                ImGui::TextUnformatted(Tr("UDP port"));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputInt("##direct_port", &directPort_);
            }
            ImGui::TextUnformatted(Tr("Survivor name"));
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##session_survivor", survivorNameInput_.data(), survivorNameInput_.size());
            if (ImGui::Button(Label(sessionMenuTab_ == 1 ? "LOG IN TO MASTER" : "CONNECT TO SERVER"), ImVec2(-1,40)))
            {
                if (sessionMenuTab_ == 1) LoginToMaster(masterUrlInput_.data(), survivorNameInput_.data());
                else ConnectDirect(directHostInput_.data(), directPort_, survivorNameInput_.data());
            }
            ImGui::EndDisabled();
            if (!NetworkAvailable()) ImGui::TextWrapped("%s", Tr("This build supports offline play only."));
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::TextWrapped("%s", sessionMenuMessage_.empty() ? Tr("You can change modes from the lobby at any time.") : sessionMenuMessage_.c_str());
        if (ImGui::Button(Label("BACK  [ESC]"), ImVec2(-1,36)) || (!opening && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive()))
            sessionMenuOpen_ = false;
        if (!sessionMenuOpen_) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
