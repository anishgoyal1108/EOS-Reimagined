#include "doctest.h"

#include "manager/release_catalog.h"

using namespace eosr::manager;

TEST_CASE("a packaged manager manifest resolves exact platform artifacts") {
    const std::string manifest =
        "{\"schema_version\":1,\"version\":\"v0.1.0-alpha.7\","
        "\"commit\":\"0123456789abcdef0123456789abcdef01234567\","
        "\"platform\":\"linux\",\"architecture\":\"x86_64\",\"files\":["
        "{\"path\":\"README.md\",\"bytes\":12,\"sha256\":"
        "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"mode\":\"0644\"},"
        "{\"path\":\"artifacts/EOSSDK-Win64-Shipping.dll\",\"bytes\":4096,"
        "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\","
        "\"mode\":\"0644\"},"
        "{\"path\":\"artifacts/libEOSSDK-Linux-Shipping.so\",\"bytes\":8192,"
        "\"sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\","
        "\"mode\":\"0755\"}]}";
    release_catalog catalog;
    std::string error;
    REQUIRE(parse_release_catalog(manifest, "/opt/eosr", catalog, error));
    CHECK(catalog.release_id ==
          "v0.1.0-alpha.7@0123456789abcdef0123456789abcdef01234567");
    release_artifact windows;
    release_artifact linux;
    REQUIRE(catalog_artifact(catalog, eos_binary_kind::windows_x86_64, windows));
    REQUIRE(catalog_artifact(catalog, eos_binary_kind::linux_x86_64, linux));
    CHECK(windows.path == "/opt/eosr/artifacts/EOSSDK-Win64-Shipping.dll");
    CHECK(windows.bytes == 4096);
    CHECK(linux.path == "/opt/eosr/artifacts/libEOSSDK-Linux-Shipping.so");
    CHECK(linux.sha256 ==
          "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
}

TEST_CASE("artifact catalog parsing rejects traversal duplicates and malformed hashes") {
    const std::string prefix =
        "{\"schema_version\":1,\"version\":\"v0.1.0-alpha.1\","
        "\"commit\":\"0123456789abcdef0123456789abcdef01234567\","
        "\"platform\":\"linux\",\"architecture\":\"x86_64\",\"files\":[";
    const std::string suffix = "]}";
    release_catalog catalog;
    std::string error;
    CHECK_FALSE(parse_release_catalog(prefix +
        "{\"path\":\"../artifact.dll\",\"bytes\":1,\"sha256\":"
        "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"mode\":\"0644\"}" + suffix, "/opt/eosr", catalog, error));
    CHECK_FALSE(parse_release_catalog(prefix +
        "{\"path\":\"artifacts/EOSSDK-Win64-Shipping.dll\",\"bytes\":1,"
        "\"sha256\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\","
        "\"mode\":\"0644\"}" + suffix, "/opt/eosr", catalog, error));
    CHECK_FALSE(parse_release_catalog(prefix +
        "{\"path\":\"README.md\",\"bytes\":1,\"sha256\":"
        "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"mode\":\"0644\"},"
        "{\"path\":\"README.md\",\"bytes\":1,\"sha256\":"
        "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"mode\":\"0644\"}" + suffix, "/opt/eosr", catalog, error));
}
