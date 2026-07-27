#include "doctest.h"

#include <string>

#include "manager/manager_state.h"

using namespace eosr::manager;

namespace {

game_state sample_game() {
    game_state game;
    game.id = "9f30ac7fbdad4ee6a7784c668ee21db1";
    game.steam_app_id = "632360";
    game.display_name = "Risk of Rain 2 — Proton";
    game.install_root = "/games/Risk of Rain 2";
    game.steam_root = "/home/alice/.var/app/com.valvesoftware.Steam/data/Steam";
    game.library_root = "/games";
    game.install_kind = steam_install_kind::flatpak_linux;
    game.manual = false;
    game.active_instance_id = "764d9fa2682243e1831169bb8d9369a6";
    game.active_target_id = "85036875e8a740b0a6bd6cf93450b9ee";

    managed_target target;
    target.id = "85036875e8a740b0a6bd6cf93450b9ee";
    target.path = "/games/Risk of Rain 2/EOSSDK-Win64-Shipping.dll";
    target.kind = eos_binary_kind::windows_x86_64;
    game.targets.push_back(target);

    managed_instance instance;
    instance.id = game.active_instance_id;
    instance.slug = "alice";
    instance.display_name = "Alïce";
    instance.data_dir = "/manager/games/game/instances/alice/data";
    instance.target_data_dir = "Z:\\manager\\games\\game\\instances\\alice\\data";
    instance.proton_prefix = "/games/steamapps/compatdata/632360/pfx";
    instance.proton_mapping_name = "z:";
    instance.proton_mapped_host_root = "/";
    instance.config_sha256 =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    game.instances.push_back(instance);
    return game;
}

} // namespace

TEST_CASE("versioned manager index and game state round trip without losing identities") {
    manager_index index;
    index.selected_release_id = "v0.1.0-alpha.1";
    game_reference reference;
    reference.id = "9f30ac7fbdad4ee6a7784c668ee21db1";
    reference.state_path = "/manager/games/9f30ac7fbdad4ee6a7784c668ee21db1/game.json";
    index.games.push_back(reference);

    manager_index parsed_index;
    std::string error;
    REQUIRE(parse_manager_index(serialize_manager_index(index), parsed_index, error));
    CHECK(parsed_index.version == 1);
    CHECK(parsed_index.selected_release_id == index.selected_release_id);
    REQUIRE(parsed_index.games.size() == 1);
    CHECK(parsed_index.games[0].id == reference.id);

    const game_state game = sample_game();
    game_state parsed_game;
    REQUIRE(parse_game_state(serialize_game_state(game), parsed_game, error));
    CHECK(parsed_game.id == game.id);
    CHECK(parsed_game.display_name == game.display_name);
    CHECK(parsed_game.install_kind == steam_install_kind::flatpak_linux);
    CHECK(parsed_game.steam_root == game.steam_root);
    CHECK(parsed_game.library_root == game.library_root);
    REQUIRE(parsed_game.targets.size() == 1);
    CHECK(parsed_game.targets[0].kind == eos_binary_kind::windows_x86_64);
    CHECK(parsed_game.active_target_id == game.active_target_id);
    REQUIRE(parsed_game.instances.size() == 1);
    CHECK(parsed_game.instances[0].id == game.active_instance_id);
    CHECK(parsed_game.instances[0].display_name == "Alïce");
}

TEST_CASE("state rejects duplicate keys unknown versions relative paths and dangling selections") {
    manager_index index;
    game_state game;
    std::string error;
    CHECK_FALSE(parse_manager_index("{\"version\":1,\"version\":1,\"selected_release_id\":\"\",\"games\":[]}",
                                    index, error));
    CHECK(error.find("duplicate") != std::string::npos);
    CHECK_FALSE(parse_manager_index("{\"version\":2,\"selected_release_id\":\"\",\"games\":[]}",
                                    index, error));
    CHECK(error.find("version") != std::string::npos);

    std::string bytes = serialize_game_state(sample_game());
    const std::string absolute = "/games/Risk of Rain 2";
    bytes.replace(bytes.find(absolute), absolute.size(), "relative/game");
    CHECK_FALSE(parse_game_state(bytes, game, error));
    CHECK(error.find("absolute") != std::string::npos);

    game_state dangling = sample_game();
    dangling.active_instance_id = "00000000000000000000000000000000";
    CHECK_FALSE(parse_game_state(serialize_game_state(dangling), game, error));
    CHECK(error.find("active instance") != std::string::npos);

    dangling = sample_game();
    dangling.active_target_id = "00000000000000000000000000000000";
    CHECK_FALSE(parse_game_state(serialize_game_state(dangling), game, error));
    CHECK(error.find("active target") != std::string::npos);
}

TEST_CASE("state ids and slugs are independently validated") {
    game_state game = sample_game();
    std::string error;
    game_state parsed;
    game.instances[0].slug = "../alice";
    CHECK_FALSE(parse_game_state(serialize_game_state(game), parsed, error));
    CHECK(error.find("slug") != std::string::npos);

    game = sample_game();
    game.instances[0].display_name = "Renamed Player";
    REQUIRE(parse_game_state(serialize_game_state(game), parsed, error));
    CHECK(parsed.instances[0].id == "764d9fa2682243e1831169bb8d9369a6");
    CHECK(parsed.instances[0].slug == "alice");
}

TEST_CASE("manager-owned state parsing is bounded") {
    manager_index index;
    std::string error;
    const std::string oversized = "{\"version\":1,\"selected_release_id\":\"" +
        std::string(1024 * 1024, 'x') + "\",\"games\":[]}";
    CHECK_FALSE(parse_manager_index(oversized, index, error));
    CHECK(error.find("large") != std::string::npos);
}
