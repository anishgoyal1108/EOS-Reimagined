#include "doctest.h"

#include <string>

#include "core/bootstrap_descriptor.h"

using namespace eosr;

namespace {

std::string absolute_data_path() {
#if defined(_WIN32)
    return "C:\\manager\\instances\\alice\\data";
#else
    return "/manager/instances/alice/data";
#endif
}
std::string quoted_path(const std::string& path) {
    std::string out;
    for (std::size_t i = 0; i < path.size(); i++) {
        out += path[i] == '\\' ? "\\\\" : std::string(1, path[i]);
    }
    return out;
}

bool parse(const std::string& bytes, bootstrap_descriptor& descriptor) {
    std::string error;
    return parse_bootstrap_descriptor(bytes, descriptor, error);
}

} // namespace

TEST_CASE("a strict version-one bootstrap descriptor is accepted") {
    bootstrap_descriptor descriptor;
    const std::string path = absolute_data_path();
    CHECK(parse("{\"version\":1,\"data_dir\":\"" + quoted_path(path) + "\"}", descriptor));
    CHECK(descriptor.version == 1);
    CHECK(descriptor.data_dir == path);
}

TEST_CASE("bootstrap fields may appear in either order") {
    bootstrap_descriptor descriptor;
    const std::string path = absolute_data_path();
    CHECK(parse("{\"data_dir\":\"" + quoted_path(path) + "\",\"version\":1}", descriptor));
    CHECK(descriptor.data_dir == path);
}

TEST_CASE("a bootstrap descriptor rejects unsupported or mistyped versions") {
    bootstrap_descriptor descriptor;
    const std::string path = quoted_path(absolute_data_path());
    CHECK_FALSE(parse("{\"version\":2,\"data_dir\":\"" + path + "\"}", descriptor));
    CHECK_FALSE(parse("{\"version\":\"1\",\"data_dir\":\"" + path + "\"}", descriptor));
}

TEST_CASE("a bootstrap descriptor rejects relative and NUL-bearing paths") {
    bootstrap_descriptor descriptor;
    CHECK_FALSE(parse("{\"version\":1,\"data_dir\":\"relative/data\"}", descriptor));
    CHECK_FALSE(parse("{\"version\":1,\"data_dir\":\"/safe\\u0000hidden\"}", descriptor));
}

TEST_CASE("a bootstrap descriptor rejects duplicate and unknown fields") {
    bootstrap_descriptor descriptor;
    const std::string path = quoted_path(absolute_data_path());
    CHECK_FALSE(parse("{\"version\":1,\"version\":1,\"data_dir\":\"" + path + "\"}",
                      descriptor));
    CHECK_FALSE(parse("{\"version\":1,\"data_dir\":\"" + path +
                          "\",\"display_name\":\"Mallory\"}",
                      descriptor));
}

TEST_CASE("a malformed bootstrap descriptor leaves no partially accepted path") {
    bootstrap_descriptor descriptor;
    descriptor.version = 99;
    descriptor.data_dir = "stale";
    std::string error;
    CHECK_FALSE(parse_bootstrap_descriptor("{\"version\":1,", descriptor, error));
    CHECK(descriptor.version == 0);
    CHECK(descriptor.data_dir.empty());
    CHECK_FALSE(error.empty());
}
