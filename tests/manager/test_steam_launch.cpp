#include "doctest.h"

#include <string>

#include "manager/steam_launch.h"

using namespace eosr::manager;

TEST_CASE("native Windows Steam launch keeps the executable and URI as separate arguments") {
    steam_launch_request request;
    std::string error;
    REQUIRE(build_steam_launch_request(steam_launch_adapter::windows_client,
                                       "C:\\Program Files (x86)\\Steam\\steam.exe", "632360",
                                       request, error));
    CHECK(request.executable == "C:\\Program Files (x86)\\Steam\\steam.exe");
    REQUIRE(request.arguments.size() == 1);
    CHECK(request.arguments[0] == "steam://run/632360");
    CHECK_FALSE(request.shell_command);
}

TEST_CASE("native Linux Steam launch is an argument-array request") {
    steam_launch_request request;
    std::string error;
    REQUIRE(build_steam_launch_request(steam_launch_adapter::linux_client,
                                       "/home/alice/.steam/steam/steam.sh", "632360", request,
                                       error));
    CHECK(request.executable == "/home/alice/.steam/steam/steam.sh");
    REQUIRE(request.arguments.size() == 1);
    CHECK(request.arguments[0] == "steam://run/632360");
    CHECK_FALSE(request.shell_command);
}

TEST_CASE("Flatpak Steam launch names the application and URI separately") {
    steam_launch_request request;
    std::string error;
    REQUIRE(build_steam_launch_request(steam_launch_adapter::flatpak_linux, std::string(),
                                       "632360", request, error));
    CHECK(request.executable == "flatpak");
    REQUIRE(request.arguments.size() == 3);
    CHECK(request.arguments[0] == "run");
    CHECK(request.arguments[1] == "com.valvesoftware.Steam");
    CHECK(request.arguments[2] == "steam://run/632360");
    CHECK_FALSE(request.shell_command);
}

TEST_CASE("the Linux desktop fallback opens only the validated URI") {
    steam_launch_request request;
    std::string error;
    REQUIRE(build_steam_launch_request(steam_launch_adapter::desktop_uri, std::string(), "632360",
                                       request, error));
    CHECK(request.executable == "xdg-open");
    REQUIRE(request.arguments.size() == 1);
    CHECK(request.arguments[0] == "steam://run/632360");
    CHECK_FALSE(request.shell_command);
}

TEST_CASE("the Windows registered URI fallback is explicit and never invokes xdg-open") {
    steam_launch_request request;
    std::string error;
    REQUIRE(build_steam_launch_request(steam_launch_adapter::windows_uri, std::string(),
                                       "632360", request, error));
    CHECK(request.executable.empty());
    REQUIRE(request.arguments.size() == 1);
    CHECK(request.arguments[0] == "steam://run/632360");
    CHECK_FALSE(request.shell_command);
}

TEST_CASE("a Steam app id cannot inject a command or URI component") {
    const char* invalid[] = {"", "0", "12 34", "12;rm", "12/34", "-1", "１２３"};
    for (std::size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        steam_launch_request request;
        std::string error;
        CHECK_FALSE(build_steam_launch_request(steam_launch_adapter::desktop_uri, std::string(),
                                               invalid[i], request, error));
        CHECK(request.executable.empty());
        CHECK(request.arguments.empty());
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("a native adapter refuses a missing Steam executable") {
    steam_launch_request request;
    std::string error;
    CHECK_FALSE(build_steam_launch_request(steam_launch_adapter::linux_client, std::string(),
                                           "632360", request, error));
    CHECK(request.executable.empty());
}
