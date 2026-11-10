#include "Engine/Common/CoreMinimal.hpp"
#include "ServerDirectory.hpp"

#include <fstream>
#include <regex>
#include <nlohmann/json.hpp>

namespace NextDayz
{
    bool NormalizeMasterUrl(std::string input, std::string& url)
    {
        const auto first = input.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return false;
        input = input.substr(first, input.find_last_not_of(" \t\r\n") - first + 1);
        if (input.find("://") == std::string::npos) input = "http://" + input;
        static const std::regex address(R"(^http://(\[[0-9a-fA-F:]+\]|[a-zA-Z0-9._-]+)(:([0-9]{1,5}))?/?$)");
        std::smatch match;
        if (!std::regex_match(input, match, address)) return false;
        if (match[3].matched)
        {
            const int port = std::stoi(match[3].str());
            if (port < 1 || port > 65535) return false;
        }
        if (input.ends_with('/')) input.pop_back();
        url = std::move(input);
        return true;
    }

    bool ReadMasterServers(const std::filesystem::path& path, std::vector<FMasterServer>& servers, std::string& error)
    {
        try
        {
            std::ifstream input(path);
            const auto document = nlohmann::json::parse(input);
            if (document.at("version") != 1 || !document.at("servers").is_array() || document.at("servers").size() > 64)
                throw std::runtime_error("Invalid server directory");
            std::vector<FMasterServer> loaded;
            for (const auto& entry : document.at("servers"))
            {
                FMasterServer server{entry.at("name").get<std::string>(), {}};
                if (server.name.empty() || server.name.size() > 128 ||
                    !NormalizeMasterUrl(entry.at("url").get<std::string>(), server.url))
                    throw std::runtime_error("Invalid Master server address or name");
                if (std::none_of(loaded.begin(), loaded.end(), [&](const auto& old) { return old.url == server.url; }))
                    loaded.push_back(std::move(server));
            }
            servers = std::move(loaded);
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool WriteMasterServers(const std::filesystem::path& path, const std::vector<FMasterServer>& servers, std::string& error)
    {
        try
        {
            nlohmann::json entries = nlohmann::json::array();
            for (const auto& server : servers) entries.push_back({{"name", server.name}, {"url", server.url}});
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path);
            output << nlohmann::json{{"version", 1}, {"servers", entries}}.dump(2);
            output.flush();
            if (!output) throw std::runtime_error("Cannot save server directory");
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
}
