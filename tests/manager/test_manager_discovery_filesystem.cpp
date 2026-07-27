#include "doctest.h"

#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "platform/manager_discovery.h"
#include "platform/paths.h"

using namespace eosr;

namespace {

std::string fixture_path(const std::string& name) {
    return std::string(EOSR_MANAGER_TEST_DIR) + "/filesystem-" + name + "-" +
           std::to_string(static_cast<unsigned long long>(getpid()));
}

void write_file(const std::string& path, const std::string& bytes) {
    std::ofstream stream(path.c_str(), std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

manager::steam_root_candidate root_candidate(const std::string& path) {
    manager::steam_root_candidate candidate;
    candidate.path = path;
    candidate.kind = manager::steam_install_kind::native_linux;
    return candidate;
}

} // namespace

TEST_CASE("the native discovery adapter scans only the supplied fake Steam root") {
    const std::string root = fixture_path("steam");
    const std::string steamapps = root + "/steamapps";
    const std::string game = steamapps + "/common/Unicode Game";
    REQUIRE(platform::make_directories(game));
    write_file(steamapps + "/libraryfolders.vdf", "\"libraryfolders\" {}");
    write_file(steamapps + "/appmanifest_42.acf",
               "\"AppState\" { \"appid\" \"42\" \"name\" \"Grüße\" "
               "\"installdir\" \"Unicode Game\" }");

    platform::manager_discovery_filesystem filesystem;
    std::vector<manager::steam_root_candidate> roots(1, root_candidate(root));
    const manager::steam_discovery_result result = manager::discover_steam_games(roots, filesystem);
    REQUIRE(result.games.size() == 1);
    CHECK(result.games[0].app_id == "42");
    CHECK(result.games[0].name == "Grüße");
    CHECK(result.games[0].install_root == game);
}

TEST_CASE("the native adapter canonicalizes aliases and bounds reads") {
    const std::string root = fixture_path("bounds");
    const std::string directory = root + "/real";
    const std::string alias = root + "/alias";
    REQUIRE(platform::make_directories(directory));
    unlink(alias.c_str());
    REQUIRE(symlink(directory.c_str(), alias.c_str()) == 0);
    write_file(directory + "/small", "1234");
    write_file(directory + "/large", "12345");

    platform::manager_discovery_filesystem filesystem;
    std::string canonical;
    REQUIRE(filesystem.canonical_directory(alias, canonical));
    CHECK(canonical == directory);

    std::string bytes = "stale";
    CHECK(filesystem.read_file(directory + "/small", 4, bytes) == manager::discovery_read::ok);
    CHECK(bytes == "1234");
    CHECK(filesystem.read_file(directory + "/large", 4, bytes) ==
          manager::discovery_read::too_large);
    CHECK(bytes.empty());
    CHECK(filesystem.read_file(directory + "/missing", 4, bytes) ==
          manager::discovery_read::missing);
}
