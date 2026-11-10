#include "Engine/Common/CoreMinimal.hpp"
#include "NextDayzGameInstance.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/ScadLoader/FScadShared.h"

namespace
{
    std::filesystem::path AuthoringRoot()
    {
#if defined(GK_NEXT_SOURCE_DIR)
        const auto source = std::filesystem::path(GK_NEXT_SOURCE_DIR);
        if (std::filesystem::is_directory(source / "assets/scad")) return source;
#endif
        auto cursor = std::filesystem::current_path();
        for (int depth = 0; depth < 8; ++depth)
        {
            if (std::filesystem::is_regular_file(cursor / "AGENTS.md")) return cursor;
            const auto parent = cursor.parent_path();
            if (cursor == parent) break;
            cursor = parent;
        }
        return Utilities::FileHelper::GetRuntimeRoot();
    }

    std::string WatchStamp(const std::vector<std::filesystem::path>& files)
    {
        std::string stamp;
        for (const auto& file : files)
        {
            std::error_code ec;
            const auto time = std::filesystem::last_write_time(file,ec);
            if (ec) { stamp += "missing;"; continue; }
            const auto size = std::filesystem::file_size(file,ec);
            stamp += fmt::format("{}:{};",time.time_since_epoch().count(),ec ? 0 : size);
        }
        return stamp;
    }
}

void NextDayzGameInstance::TickMapAuthoring()
{
    if (!authoringMode_)
    {
        authoringWatchScene_.clear();
        return;
    }
    if (!sceneReady_ || pendingCampaign_ || campaignSaveBlocked_ ||
        GetEngine().GetEngineStatus() != NextRenderer::EApplicationStatus::Running) return;
    const auto now = std::chrono::steady_clock::now();
    if (now < authoringNextPoll_) return;
    authoringNextPoll_ = now + std::chrono::milliseconds(500);
    const auto* map = NextDayz::FindExpeditionMap(expeditionMaps_,campaignMap_);
    if (!map || campaignMap_ != map->draftScene) return;
    const bool initial = authoringWatchScene_ != campaignMap_;
    if (initial)
    {
        authoringWatchScene_ = campaignMap_;
        authoringWatchFiles_ = {AuthoringRoot() / campaignMap_};
        authoringAppliedStamp_.clear();
        authoringPendingStamp_.clear();
    }
    const auto stamp = WatchStamp(authoringWatchFiles_);
    if (!initial && stamp == authoringAppliedStamp_) return;
    if (!initial && stamp != authoringPendingStamp_)
    {
        authoringPendingStamp_ = stamp;
        return; // Wait for a second stable poll before reading an editor's save.
    }
    const auto sourceRoot = AuthoringRoot().lexically_normal();
    const auto sourceScene = sourceRoot / campaignMap_;
    Assets::Scad::ScadProgram program;
    std::vector<std::string> dependencies;
    std::string error;
    const bool parsed = Assets::Scad::LoadScadProgram(sourceScene.string(),program,error,&dependencies);
    authoringWatchFiles_.clear();
    for (const auto& file : dependencies) authoringWatchFiles_.emplace_back(file);
    authoringWatchFiles_.push_back(sourceScene.parent_path() / "terrain.hmap");
    authoringAppliedStamp_ = WatchStamp(authoringWatchFiles_);
    authoringPendingStamp_ = authoringAppliedStamp_;
    if (!parsed)
    {
        campaignMessage_ = NextDayz::L10n::Message("Draft edit rejected; current world preserved: {}", error);
        SPDLOG_WARN("[NextDayz] {}",campaignMessage_);
        return;
    }
    bool changed = false;
    for (const auto& source : authoringWatchFiles_)
    {
        const auto relative = source.lexically_normal().lexically_relative(sourceRoot);
        if (relative.empty() || relative.is_absolute() || relative.generic_string().starts_with("..")) continue;
        const auto destination = Utilities::FileHelper::GetRuntimeFilePath(relative).lexically_normal();
        if (source.lexically_normal() == destination) continue;
        std::ifstream sourceFile(source,std::ios::binary), destinationFile(destination,std::ios::binary);
        if (!sourceFile) continue;
        const std::string sourceText((std::istreambuf_iterator<char>(sourceFile)),{});
        const std::string runtimeText((std::istreambuf_iterator<char>(destinationFile)),{});
        if (sourceText == runtimeText) continue;
        std::error_code ec;
        std::filesystem::create_directories(destination.parent_path(),ec);
        if (!ec) std::filesystem::copy_file(source,destination,std::filesystem::copy_options::overwrite_existing,ec);
        if (ec)
        {
            campaignMessage_ = NextDayz::L10n::Message("Cannot update draft runtime files: {}", ec.message());
            SPDLOG_ERROR("[NextDayz] {}",campaignMessage_);
            return;
        }
        changed = true;
    }
    if (!initial || changed)
    {
        campaignMessage_ = "Draft updated. Rebuilding the world while preserving your expedition.";
        GetEngine().RequestLoadScene({.filename = campaignMap_});
    }
}
