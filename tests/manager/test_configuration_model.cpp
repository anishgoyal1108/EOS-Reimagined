#include "doctest.h"

#include <set>
#include <string>

#include "manager/configuration_model.h"

using namespace eosr::manager;

TEST_CASE("the shared configuration schema exposes every SDK file directive") {
    const std::vector<configuration_field>& fields = configuration_fields();
    REQUIRE(fields.size() == 13);
    std::set<std::string> keys;
    for (std::size_t i = 0; i < fields.size(); i++) {
        CHECK_FALSE(fields[i].key.empty());
        CHECK_FALSE(fields[i].environment.empty());
        CHECK(keys.insert(fields[i].key).second);
    }
    CHECK(keys.count("display_name") == 1);
    CHECK(keys.count("peer_seeds") == 1);
    CHECK(keys.count("unlock_dlcs") == 1);
}

TEST_CASE("all configuration fields parse validate and serialize deterministically") {
    const std::string input =
        "{\"display_name\":\"Alice\",\"locale\":\"pt-BR\","
        "\"discovery_ports\":[56000,56010],\"peer_seeds\":[\"100.70.1.2\",\"10.0.0.7\"],"
        "\"enable_lan\":true,\"log_level\":\"debug\",\"trace_level\":\"lifecycle\","
        "\"trace_dir\":\"diagnostics\",\"trace_max_bytes\":1048576,"
        "\"trace_max_rotated_files\":4,\"instance_label\":\"alice-1\","
        "\"enable_overlay\":false,\"unlock_dlcs\":false,"
        "\"future_setting\":{\"kept\":[1,true]}}";
    manager_configuration config;
    std::string error;
    REQUIRE(parse_manager_configuration(input, config, error));
    const configuration_validation validation = validate_manager_configuration(config);
    REQUIRE(validation.valid);
    CHECK(validation.normalized.display_name == "Alice");
    REQUIRE(validation.normalized.peer_seeds.size() == 2);
    CHECK(validation.normalized.trace_level == "lifecycle");

    const std::string output = serialize_manager_configuration(validation.normalized);
    CHECK(output.find("future_setting") != std::string::npos);
    manager_configuration reparsed;
    REQUIRE(parse_manager_configuration(output, reparsed, error));
    CHECK(serialize_manager_configuration(reparsed) == output);
}

TEST_CASE("configuration previews SDK truncation and clamping before save") {
    manager_configuration config = default_manager_configuration();
    config.display_name = "abcdefghijklmnop-more";
    config.trace_max_bytes = 1;
    config.trace_max_rotated_files = 1000;
    const configuration_validation result = validate_manager_configuration(config);
    REQUIRE(result.valid);
    CHECK(result.normalized.display_name == "abcdefghijklmnop");
    CHECK(result.normalized.trace_max_bytes == 65536);
    CHECK(result.normalized.trace_max_rotated_files == 64);
    REQUIRE(result.diagnostics.size() == 3);
}

TEST_CASE("invalid configuration values are rejected rather than silently rewritten") {
    manager_configuration config = default_manager_configuration();
    config.locale = "en/../../";
    config.discovery_first = 56010;
    config.discovery_last = 56000;
    config.instance_label = "../alice";
    config.trace_level = "everything";
    config.peer_seeds.push_back("example.com");
    const configuration_validation result = validate_manager_configuration(config);
    CHECK_FALSE(result.valid);
    CHECK(result.diagnostics.size() >= 5);
}

TEST_CASE("aliases import while unknown JSON survives and malformed JSON never becomes editable") {
    manager_configuration config;
    std::string error;
    REQUIRE(parse_manager_configuration(
        "{\"username\":\"Alias\",\"language\":\"de\",\"unknown\":42}", config, error));
    CHECK(config.display_name == "Alias");
    CHECK(config.locale == "de");
    const std::string serialized = serialize_manager_configuration(config);
    CHECK(serialized.find("\"unknown\":42") != std::string::npos);
    CHECK(serialized.find("\"username\":\"Alias\"") != std::string::npos);

    CHECK_FALSE(parse_manager_configuration("{\"display_name\":", config, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(parse_manager_configuration(
        "{\"display_name\":\"one\",\"display_name\":\"two\"}", config, error));
}

TEST_CASE("wrong-typed known fields import as SDK defaults with actionable diagnostics") {
    manager_configuration config;
    std::string error;
    REQUIRE(parse_manager_configuration(
        "{\"display_name\":7,\"username\":\"AliasMustNotWin\","
        "\"discovery_ports\":\"bad\",\"peer_seeds\":[\"10.0.0.2\",3],"
        "\"enable_lan\":\"yes\",\"trace_max_bytes\":false,"
        "\"future_setting\":42}", config, error));
    CHECK(error.empty());
    CHECK(config.display_name == "Player");
    CHECK(config.discovery_first == 55789);
    CHECK(config.peer_seeds.empty());
    CHECK(config.enable_lan);
    CHECK(config.trace_max_bytes == 67108864);
    CHECK(config.import_issues.size() == 5);
    const std::string saved = serialize_manager_configuration(config);
    CHECK(saved.find("AliasMustNotWin") != std::string::npos); // preserved alias is unknown now
    CHECK(saved.find("\"display_name\":\"Player\"") != std::string::npos);
    CHECK(saved.find("\"future_setting\":42") != std::string::npos);
}
