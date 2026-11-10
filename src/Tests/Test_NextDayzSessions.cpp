#include "Engine/Common/CoreMinimal.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include "Application/Game/NextDayz/Net/ServerDirectory.hpp"

TEST_CASE("NextDayz Master addresses normalize and reject unsupported endpoints", "[NextDayz][Sessions]")
{
    std::string url;
    REQUIRE(NextDayz::NormalizeMasterUrl("  127.0.0.1:7780/  ", url));
    CHECK(url == "http://127.0.0.1:7780");
    REQUIRE(NextDayz::NormalizeMasterUrl("http://[::1]:7780", url));
    CHECK(url == "http://[::1]:7780");
    for (const auto* invalid : {"", "http://", "http://a:0", "http://a:65536", "http://a/path", "http://user@a", "ftp://a", "http://a?key=x", "a b"})
    {
        CAPTURE(invalid);
        CHECK_FALSE(NextDayz::NormalizeMasterUrl(invalid, url));
    }
}

TEST_CASE("NextDayz server favorites round trip without replacing a list on corrupt input", "[NextDayz][Sessions]")
{
    const auto path = std::filesystem::temp_directory_path() / fmt::format("nextdayz-servers-{}.json",
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::string error;
    const std::vector<NextDayz::FMasterServer> saved = {{"本地服务器", "http://127.0.0.1:7780"}, {"LAN", "http://192.168.1.8:7780"}};
    REQUIRE(NextDayz::WriteMasterServers(path, saved, error));
    std::vector<NextDayz::FMasterServer> loaded;
    REQUIRE(NextDayz::ReadMasterServers(path, loaded, error));
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].name == saved[0].name);
    CHECK(loaded[1].url == saved[1].url);
    {
        std::ofstream output(path);
        output << R"({"version":1,"servers":[{"name":"bad","url":"http://a:99999"}]})";
    }
    CHECK_FALSE(NextDayz::ReadMasterServers(path, loaded, error));
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].name == saved[0].name);
    std::filesystem::remove(path);
}
