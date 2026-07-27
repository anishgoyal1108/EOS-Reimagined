#include "doctest.h"

#include <string>
#include <vector>

#include "core/config.h"
#include "core/config_file.h"
#include "manager/configuration_model.h"

using namespace eosr;

namespace {

class file_source : public config_source {
public:
    explicit file_source(const config_file& file) : file_(file) {}
    bool env(const std::string&, std::string&) const { return false; }
    lookup file_string(const std::string& key, std::string& out) const {
        return file_.get_string(key, out);
    }
    lookup file_int(const std::string& key, i64& out) const {
        return file_.get_int(key, out);
    }
    lookup file_int_pair(const std::string& key, i64& first, i64& second) const {
        return file_.get_int_pair(key, first, second);
    }
    lookup file_bool(const std::string& key, bool& out) const {
        return file_.get_bool(key, out);
    }
    lookup file_string_array(const std::string& key, std::vector<std::string>& out) const {
        return file_.get_string_array(key, out);
    }
private:
    const config_file& file_;
};

resolved_config resolve_manager_output(const manager::manager_configuration& config) {
    config_file file;
    std::string error;
    REQUIRE(parse_config_file(manager::serialize_manager_configuration(config), file, error));
    file_source source(file);
    config_defaults defaults;
    defaults.data_dir = "/manager/instance/data";
    defaults.default_ports.first = 55789;
    defaults.default_ports.last = 55798;
    return resolve_config(source, defaults);
}

} // namespace

TEST_CASE("manager defaults and normalized output conform to the SDK resolver") {
    manager::manager_configuration config = manager::default_manager_configuration();
    config.display_name = "abcdefghijklmnop-extra";
    config.locale = "pt-BR";
    config.discovery_first = 56000;
    config.discovery_last = 56010;
    config.peer_seeds.push_back("100.70.1.2");
    config.enable_lan = false;
    config.log_level = "warning";
    config.trace_level = "lifecycle";
    config.trace_dir = "diagnostics";
    config.trace_max_bytes = 1;
    config.trace_max_rotated_files = 100;
    config.instance_label = "alice-1";

    const manager::configuration_validation validation =
        manager::validate_manager_configuration(config);
    REQUIRE(validation.valid);
    const resolved_config sdk = resolve_manager_output(validation.normalized);
    CHECK(sdk.display_name == validation.normalized.display_name);
    CHECK(sdk.locale == validation.normalized.locale);
    CHECK(sdk.discovery_ports.first == validation.normalized.discovery_first);
    CHECK(sdk.discovery_ports.last == validation.normalized.discovery_last);
    REQUIRE(sdk.peer_seeds.size() == 1);
    CHECK(sdk.peer_seeds[0] == 0x64460102u);
    CHECK(sdk.enable_lan == validation.normalized.enable_lan);
    CHECK(sdk.logging == log_level::warn);
    CHECK(sdk.level == trace_level::lifecycle);
    CHECK(sdk.trace_dir == "/manager/instance/data/diagnostics");
    CHECK(sdk.trace_max_bytes == static_cast<u64>(validation.normalized.trace_max_bytes));
    CHECK(sdk.trace_max_rotated_files ==
          static_cast<u32>(validation.normalized.trace_max_rotated_files));
    CHECK(sdk.instance_label == validation.normalized.instance_label);
    CHECK(sdk.enable_overlay == validation.normalized.enable_overlay);
    CHECK(sdk.unlock_dlcs == validation.normalized.unlock_dlcs);
    CHECK(sdk.sources.display_name == config_origin::file);
    CHECK(sdk.sources.locale == config_origin::file);
    CHECK(sdk.sources.discovery_ports == config_origin::file);
    CHECK(sdk.sources.peer_seeds == config_origin::file);
    CHECK(sdk.sources.enable_lan == config_origin::file);
    CHECK(sdk.sources.log_level == config_origin::file);
    CHECK(sdk.sources.trace_level == config_origin::file);
    CHECK(sdk.sources.trace_dir == config_origin::file);
    CHECK(sdk.sources.trace_max_bytes == config_origin::file);
    CHECK(sdk.sources.trace_max_rotated_files == config_origin::file);
    CHECK(sdk.sources.instance_label == config_origin::file);
    CHECK(sdk.sources.enable_overlay == config_origin::file);
    CHECK(sdk.sources.unlock_dlcs == config_origin::file);
}
