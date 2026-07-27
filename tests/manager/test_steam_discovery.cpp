#include "doctest.h"

#include <map>
#include <string>
#include <vector>

#include "manager/steam_discovery.h"

using namespace eosr::manager;

namespace {

class fake_discovery_filesystem : public discovery_filesystem {
public:
    void directory(const std::string& path, const std::string& canonical = std::string()) {
        directories_[path] = canonical.empty() ? path : canonical;
    }

    void file(const std::string& path, const std::string& bytes) {
        files_[path] = bytes;
    }

    void listing(const std::string& path, const std::vector<std::string>& names) {
        listings_[path] = names;
    }

    bool canonical_directory(const std::string& path, std::string& out) {
        std::map<std::string, std::string>::const_iterator it = directories_.find(path);
        if (it == directories_.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    discovery_read read_file(const std::string& path, std::size_t max_bytes, std::string& out) {
        out.clear();
        std::map<std::string, std::string>::const_iterator it = files_.find(path);
        if (it == files_.end()) {
            return discovery_read::missing;
        }
        if (it->second.size() > max_bytes) {
            return discovery_read::too_large;
        }
        out = it->second;
        return discovery_read::ok;
    }

    bool list_file_names(const std::string& path, std::vector<std::string>& out) {
        std::map<std::string, std::vector<std::string> >::const_iterator it = listings_.find(path);
        if (it == listings_.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

private:
    std::map<std::string, std::string> directories_;
    std::map<std::string, std::string> files_;
    std::map<std::string, std::vector<std::string> > listings_;
};

std::string manifest(const std::string& app_id, const std::string& name,
                     const std::string& install_dir) {
    return "\"AppState\" { \"appid\" \"" + app_id + "\" \"name\" \"" + name +
           "\" \"installdir\" \"" + install_dir + "\" }";
}

steam_root_candidate root(const std::string& path, steam_install_kind kind) {
    steam_root_candidate value;
    value.path = path;
    value.kind = kind;
    return value;
}

bool has_diagnostic(const steam_discovery_result& result, const std::string& code) {
    for (std::size_t i = 0; i < result.diagnostics.size(); i++) {
        if (result.diagnostics[i].code == code) {
            return true;
        }
    }
    return false;
}

std::size_t diagnostic_count(const steam_discovery_result& result, const std::string& code,
                             const std::string& path) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < result.diagnostics.size(); i++) {
        if (result.diagnostics[i].code == code && result.diagnostics[i].path == path) count++;
    }
    return count;
}

} // namespace

TEST_CASE("Linux Steam root candidates cover native, Flatpak, and legacy layouts") {
    const std::vector<steam_root_candidate> roots = linux_steam_root_candidates("/home/alice");
    REQUIRE(roots.size() >= 5);
    CHECK(roots[0].path == "/home/alice/.steam/steam");
    CHECK(roots[0].kind == steam_install_kind::native_linux);
    bool flatpak_local = false;
    bool flatpak_data = false;
    for (std::size_t i = 0; i < roots.size(); i++) {
        flatpak_local = flatpak_local ||
            roots[i].path == "/home/alice/.var/app/com.valvesoftware.Steam/.local/share/Steam";
        flatpak_data = flatpak_data ||
            roots[i].path == "/home/alice/.var/app/com.valvesoftware.Steam/data/Steam";
    }
    CHECK(flatpak_local);
    CHECK(flatpak_data);
}

TEST_CASE("current library folders discover the same app id in two real installations") {
    fake_discovery_filesystem fs;
    fs.directory("/steam");
    fs.directory("/steam/steamapps/common");
    fs.directory("/steam/steamapps/common/Risk of Rain 2");
    fs.directory("/games");
    fs.directory("/games/steamapps/common");
    fs.directory("/games/steamapps/common/Risk of Rain 2", "/canonical/ror2-copy");
    fs.file("/steam/steamapps/libraryfolders.vdf",
            "\"libraryfolders\" { \"0\" { \"path\" \"/steam\" \"apps\" { "
            "\"632360\" \"1\" \"632360\" \"2\" } } "
            "\"1\" { \"path\" \"/games\" \"apps\" { \"632360\" \"1\" } } }");
    fs.file("/steam/steamapps/appmanifest_632360.acf",
            manifest("632360", "Risk of Rain 2", "Risk of Rain 2"));
    fs.file("/games/steamapps/appmanifest_632360.acf",
            manifest("632360", "Risk of Rain 2", "Risk of Rain 2"));
    fs.listing("/steam/steamapps", std::vector<std::string>(1, "appmanifest_632360.acf"));
    fs.listing("/games/steamapps", std::vector<std::string>(1, "appmanifest_632360.acf"));

    std::vector<steam_root_candidate> roots;
    roots.push_back(root("/steam", steam_install_kind::native_linux));
    roots.push_back(root("/steam", steam_install_kind::native_linux));
    const steam_discovery_result result = discover_steam_games(roots, fs);
    REQUIRE(result.installations.size() == 1);
    REQUIRE(result.games.size() == 2);
    CHECK(result.games[0].app_id == "632360");
    CHECK(result.games[0].install_root != result.games[1].install_root);
    CHECK(result.games[0].kind == steam_install_kind::native_linux);
}

TEST_CASE("Flatpak sandbox primary library paths resolve to the host Flatpak root once") {
    const std::string home = "/home/alice";
    const std::string native = home + "/.local/share/Steam";
    const std::string flatpak =
        home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam";
    fake_discovery_filesystem fs;
    fs.directory(native);
    fs.directory(flatpak);
    fs.directory(flatpak + "/steamapps/common/Risk of Rain 2");
    fs.file(flatpak + "/steamapps/libraryfolders.vdf",
            "\"libraryfolders\" { \"0\" { \"path\" \"" + native +
            "\" \"apps\" { \"632360\" \"1\" } } }");
    fs.listing(flatpak + "/steamapps",
               std::vector<std::string>(1, "appmanifest_632360.acf"));
    fs.file(flatpak + "/steamapps/appmanifest_632360.acf",
            manifest("632360", "Risk of Rain 2", "Risk of Rain 2"));

    std::vector<steam_root_candidate> roots;
    roots.push_back(root(native, steam_install_kind::native_linux));
    roots.push_back(root(flatpak, steam_install_kind::flatpak_linux));
    const steam_discovery_result result = discover_steam_games(roots, fs);
    REQUIRE(result.installations.size() == 2);
    REQUIRE(result.installations[1].libraries.size() == 1);
    CHECK(result.installations[1].libraries[0] == flatpak);
    REQUIRE(result.games.size() == 1);
    CHECK(result.games[0].kind == steam_install_kind::flatpak_linux);
    CHECK(diagnostic_count(result, "steamapps_unreadable", native + "/steamapps") == 1);
}

TEST_CASE("old scalar library folders are accepted and metadata entries are ignored") {
    fake_discovery_filesystem fs;
    fs.directory("/steam");
    fs.directory("/steam/steamapps/common");
    fs.directory("/old-library");
    fs.directory("/old-library/steamapps/common");
    fs.directory("/old-library/steamapps/common/Game");
    fs.file("/steam/steamapps/libraryfolders.vdf",
            "\"LibraryFolders\" { \"TimeNextStatsReport\" \"1\" "
            "\"1\" \"/old-library\" }");
    fs.listing("/steam/steamapps", std::vector<std::string>());
    fs.listing("/old-library/steamapps", std::vector<std::string>(1, "appmanifest_10.acf"));
    fs.file("/old-library/steamapps/appmanifest_10.acf", manifest("10", "Game", "Game"));

    std::vector<steam_root_candidate> roots(1, root("/steam", steam_install_kind::native_linux));
    const steam_discovery_result result = discover_steam_games(roots, fs);
    REQUIRE(result.games.size() == 1);
    CHECK(result.games[0].library_root == "/old-library");
}

TEST_CASE("stale manifests and traversal-shaped install directories are refused") {
    fake_discovery_filesystem fs;
    fs.directory("/steam");
    fs.directory("/steam/steamapps/common");
    fs.file("/steam/steamapps/libraryfolders.vdf", "\"libraryfolders\" {}");
    std::vector<std::string> names;
    names.push_back("appmanifest_1.acf");
    names.push_back("appmanifest_2.acf");
    fs.listing("/steam/steamapps", names);
    fs.file("/steam/steamapps/appmanifest_1.acf", manifest("1", "Gone", "Gone"));
    fs.file("/steam/steamapps/appmanifest_2.acf", manifest("2", "Escape", "../outside"));

    std::vector<steam_root_candidate> roots(1, root("/steam", steam_install_kind::native_linux));
    const steam_discovery_result result = discover_steam_games(roots, fs);
    CHECK(result.games.empty());
    CHECK(has_diagnostic(result, "install_missing"));
    CHECK(has_diagnostic(result, "unsafe_install_dir"));
}

TEST_CASE("a manifest app id must match its bounded filename") {
    fake_discovery_filesystem fs;
    fs.directory("/steam");
    fs.directory("/steam/steamapps/common");
    fs.directory("/steam/steamapps/common/Game");
    fs.file("/steam/steamapps/libraryfolders.vdf", "\"libraryfolders\" {}");
    fs.listing("/steam/steamapps", std::vector<std::string>(1, "appmanifest_10.acf"));
    fs.file("/steam/steamapps/appmanifest_10.acf", manifest("11", "Game", "Game"));

    std::vector<steam_root_candidate> roots(1, root("/steam", steam_install_kind::native_linux));
    const steam_discovery_result result = discover_steam_games(roots, fs);
    CHECK(result.games.empty());
    CHECK(has_diagnostic(result, "appid_mismatch"));
}

TEST_CASE("large manifests retain bounded field lookup without copying parsed subtrees") {
    fake_discovery_filesystem fs;
    fs.directory("/steam");
    fs.directory("/steam/steamapps/common");
    fs.directory("/steam/steamapps/common/Game");
    fs.file("/steam/steamapps/libraryfolders.vdf", "\"libraryfolders\" {}");
    fs.listing("/steam/steamapps", std::vector<std::string>(1, "appmanifest_7.acf"));
    std::string bytes = "\"AppState\" { \"appid\" \"7\" \"name\" \"Game\" "
                        "\"installdir\" \"Game\" ";
    for (int i = 0; i < 2000; i++) {
        bytes += "\"metadata_" + std::to_string(i) + "\" { \"unused\" \"value\" } ";
    }
    bytes += "}";
    fs.file("/steam/steamapps/appmanifest_7.acf", bytes);

    std::vector<steam_root_candidate> roots(1, root("/steam", steam_install_kind::native_linux));
    const steam_discovery_result result = discover_steam_games(roots, fs);
    REQUIRE(result.games.size() == 1);
    CHECK(result.games[0].app_id == "7");
}

TEST_CASE("malformed and oversized Steam metadata fails locally and discovery continues") {
    fake_discovery_filesystem fs;
    fs.directory("/bad");
    fs.file("/bad/steamapps/libraryfolders.vdf", std::string(1024 * 1024 + 1, 'x'));
    fs.listing("/bad/steamapps", std::vector<std::string>());
    fs.directory("/good");
    fs.directory("/good/steamapps/common");
    fs.directory("/good/steamapps/common/Game");
    fs.file("/good/steamapps/libraryfolders.vdf", "\"libraryfolders\" {}");
    fs.listing("/good/steamapps", std::vector<std::string>(1, "appmanifest_7.acf"));
    fs.file("/good/steamapps/appmanifest_7.acf", manifest("7", "Game", "Game"));

    std::vector<steam_root_candidate> roots;
    roots.push_back(root("/bad", steam_install_kind::flatpak_linux));
    roots.push_back(root("/good", steam_install_kind::native_linux));
    const steam_discovery_result result = discover_steam_games(roots, fs);
    REQUIRE(result.games.size() == 1);
    CHECK(result.games[0].app_id == "7");
    CHECK(has_diagnostic(result, "libraryfolders_too_large"));
}
