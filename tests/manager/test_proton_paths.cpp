#include "doctest.h"

#include <string>

#include <unistd.h>

#include "platform/paths.h"
#include "platform/proton_paths.h"

using namespace eosr::platform;

namespace {

struct proton_fixture {
    std::string root;
    std::string prefix;
    std::string dosdevices;
    std::string mapped_root;
    std::string instance;

    explicit proton_fixture(const std::string& name) {
        root = std::string(EOSR_MANAGER_TEST_DIR) + "/" + name + "-" +
               std::to_string(static_cast<unsigned long long>(getpid()));
        prefix = root + "/prefix";
        dosdevices = prefix + "/dosdevices";
        mapped_root = root + "/mapped";
        instance = mapped_root + "/manager/instances/alice/data";
        REQUIRE(make_directories(dosdevices));
        REQUIRE(make_directories(instance));
    }

    std::string mapping(char drive) const {
        std::string name;
        name += drive;
        name += ':';
        return dosdevices + "/" + name;
    }

    void map(char drive, const std::string& target) {
        unlink(mapping(drive).c_str());
        REQUIRE(symlink(target.c_str(), mapping(drive).c_str()) == 0);
    }
};

std::string windows_spelling(const std::string& path) {
    std::string out = "Z:";
    for (std::size_t i = 0; i < path.size(); i++)
        out += path[i] == '/' ? '\\' : path[i];
    return out;
}

} // namespace

TEST_CASE("a Proton dosdevices mapping produces a verified Windows directory") {
    proton_fixture fx("valid");
    fx.map('z', fx.mapped_root);

    proton_path_translation translation;
    proton_path_error error = proton_path_error::none;
    REQUIRE(translate_host_path_for_proton(fx.prefix, fx.instance, translation, error));
    CHECK(error == proton_path_error::none);
    CHECK(translation.windows_path == "Z:\\manager\\instances\\alice\\data");
    CHECK(translation.host_path == fx.instance);
    CHECK(validate_proton_path_translation(fx.prefix, translation, error));
}

TEST_CASE("a missing Proton mapping is refused rather than guessed") {
    proton_fixture fx("missing");

    proton_path_translation translation;
    translation.windows_path = "stale";
    proton_path_error error = proton_path_error::none;
    CHECK_FALSE(translate_host_path_for_proton(fx.prefix, fx.instance, translation, error));
    CHECK(error == proton_path_error::no_mapping);
    CHECK(translation.windows_path.empty());
}

TEST_CASE("a changed Proton mapping invalidates a prior translation") {
    proton_fixture fx("changed");
    fx.map('z', fx.mapped_root);

    proton_path_translation translation;
    proton_path_error error = proton_path_error::none;
    REQUIRE(translate_host_path_for_proton(fx.prefix, fx.instance, translation, error));

    const std::string replacement = fx.root + "/replacement";
    REQUIRE(make_directories(replacement));
    fx.map('z', replacement);
    CHECK_FALSE(validate_proton_path_translation(fx.prefix, translation, error));
    CHECK(error == proton_path_error::mapping_changed);
}

TEST_CASE("a mapping target only matches at a path-component boundary") {
    proton_fixture fx("boundary");
    const std::string short_root = fx.root + "/map";
    REQUIRE(make_directories(short_root));
    fx.map('z', short_root);

    proton_path_translation translation;
    proton_path_error error = proton_path_error::none;
    CHECK_FALSE(translate_host_path_for_proton(fx.prefix, fx.mapped_root, translation, error));
    CHECK(error == proton_path_error::no_mapping);
}

TEST_CASE("relative dosdevices symlinks are canonicalized and proven") {
    proton_fixture fx("relative");
    const std::string relative_target = "../../mapped";
    fx.map('d', relative_target);

    proton_path_translation translation;
    proton_path_error error = proton_path_error::none;
    REQUIRE(translate_host_path_for_proton(fx.prefix, fx.instance, translation, error));
    CHECK(translation.windows_path == "D:\\manager\\instances\\alice\\data");
    CHECK(validate_proton_path_translation(fx.prefix, translation, error));
}

TEST_CASE("a nonexistent host directory cannot be translated") {
    proton_fixture fx("host-missing");
    fx.map('z', fx.mapped_root);

    proton_path_translation translation;
    proton_path_error error = proton_path_error::none;
    CHECK_FALSE(translate_host_path_for_proton(fx.prefix, fx.root + "/absent", translation,
                                               error));
    CHECK(error == proton_path_error::host_missing);
}

TEST_CASE("Flatpak Steam storage round-trips through its sandbox before Proton") {
    proton_fixture fx("flatpak-namespace");
    const std::string home = fx.root + "/home/tester";
    const std::string app_root = home + "/.var/app/com.valvesoftware.Steam";
    const std::string steam_root = app_root + "/.local/share/Steam";
    const std::string host_instance = app_root +
        "/.local/share/eos-reimagined-manager/games/game/instances/alice/data";
    const std::string runtime_instance = home +
        "/.local/share/eos-reimagined-manager/games/game/instances/alice/data";
    REQUIRE(make_directories(steam_root));
    REQUIRE(make_directories(host_instance));
    fx.map('z', "/");

    flatpak_steam_path_roots roots;
    proton_path_error error = proton_path_error::none;
    REQUIRE(flatpak_steam_manager_roots(steam_root, roots, error));
    CHECK(roots.app_host_root == app_root);
    CHECK(roots.manager_host_root == app_root +
          "/.local/share/eos-reimagined-manager");
    CHECK(roots.manager_runtime_root == home +
          "/.local/share/eos-reimagined-manager");
    std::string runtime_path;
    REQUIRE(flatpak_steam_runtime_path(
        steam_root, host_instance, runtime_path, error));
    CHECK(runtime_path == runtime_instance);

    proton_path_translation translation;
    const bool translated = translate_flatpak_host_path_for_proton(
        steam_root, fx.prefix, host_instance, translation, error);
    CAPTURE(static_cast<int>(error));
    REQUIRE(translated);
    CHECK(error == proton_path_error::none);
    CHECK(translation.host_path == host_instance);
    CHECK(translation.windows_path == windows_spelling(runtime_instance));
    CHECK(validate_flatpak_proton_path_translation(
        steam_root, fx.prefix, translation, error));
}

TEST_CASE("Flatpak path validation rejects the host-home alias exposed by the sandbox") {
    proton_fixture fx("flatpak-alias");
    const std::string home = fx.root + "/home/tester";
    const std::string app_root = home + "/.var/app/com.valvesoftware.Steam";
    const std::string steam_root = app_root + "/data/Steam";
    const std::string manager_relative =
        "/.local/share/eos-reimagined-manager/games/game/instances/alice/data";
    const std::string host_instance = app_root + manager_relative;
    const std::string unrelated_host_alias = home + manager_relative;
    REQUIRE(make_directories(steam_root));
    REQUIRE(make_directories(host_instance));
    REQUIRE(make_directories(unrelated_host_alias));
    fx.map('z', "/");

    proton_path_translation translation;
    proton_path_error error = proton_path_error::none;
    CHECK_FALSE(translate_flatpak_host_path_for_proton(
        steam_root, fx.prefix, unrelated_host_alias, translation, error));
    CHECK(error == proton_path_error::flatpak_path_outside);

    const bool translated = translate_flatpak_host_path_for_proton(
        steam_root, fx.prefix, host_instance, translation, error);
    CAPTURE(static_cast<int>(error));
    REQUIRE(translated);
    translation.host_path = unrelated_host_alias;
    CHECK_FALSE(validate_flatpak_proton_path_translation(
        steam_root, fx.prefix, translation, error));
    CHECK(error == proton_path_error::flatpak_path_outside);
}
