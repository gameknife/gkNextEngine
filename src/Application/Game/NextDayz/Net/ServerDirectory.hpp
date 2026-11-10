#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace NextDayz
{
    struct FMasterServer
    {
        std::string name;
        std::string url;
    };

    bool NormalizeMasterUrl(std::string input, std::string& url);
    bool ReadMasterServers(const std::filesystem::path& path, std::vector<FMasterServer>& servers, std::string& error);
    bool WriteMasterServers(const std::filesystem::path& path, const std::vector<FMasterServer>& servers, std::string& error);
}
