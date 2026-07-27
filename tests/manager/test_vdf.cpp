#include "doctest.h"

#include <string>

#include "manager/vdf.h"

using namespace eosr::manager;

TEST_CASE("the current libraryfolders VDF shape preserves nested paths and apps") {
    const std::string text =
        "\"libraryfolders\"\n"
        "{\n"
        "  \"0\" { \"path\" \"C:\\\\Program Files (x86)\\\\Steam\" "
        "\"apps\" { \"632360\" \"1234\" } }\n"
        "  \"1\" { \"path\" \"D:\\\\Games\\\\Steam\" }\n"
        "}\n";
    vdf_document document;
    std::string error;
    REQUIRE(parse_vdf(text, document, error));

    const vdf_entry* folders = find_vdf_entry(document.entries, "libraryfolders");
    REQUIRE(folders != 0);
    REQUIRE(folders->is_object);
    const vdf_entry* first = find_vdf_entry(folders->children, "0");
    REQUIRE(first != 0);
    const vdf_entry* path = find_vdf_entry(first->children, "path");
    REQUIRE(path != 0);
    CHECK(path->value == "C:\\Program Files (x86)\\Steam");
    const vdf_entry* apps = find_vdf_entry(first->children, "apps");
    REQUIRE(apps != 0);
    const vdf_entry* app = find_vdf_entry(apps->children, "632360");
    CHECK(app != 0);
}

TEST_CASE("the old libraryfolders VDF shape keeps numbered scalar paths") {
    const std::string text =
        "\"LibraryFolders\"\n"
        "{\n"
        "  \"TimeNextStatsReport\" \"123\"\n"
        "  \"1\" \"/mnt/games/SteamLibrary\"\n"
        "}\n";
    vdf_document document;
    std::string error;
    REQUIRE(parse_vdf(text, document, error));
    const vdf_entry* folders = find_vdf_entry_case_insensitive(document.entries, "libraryfolders");
    REQUIRE(folders != 0);
    const vdf_entry* library = find_vdf_entry(folders->children, "1");
    REQUIRE(library != 0);
    CHECK_FALSE(library->is_object);
    CHECK(library->value == "/mnt/games/SteamLibrary");
}

TEST_CASE("VDF quoted strings support escaped quotes, slashes, and Unicode") {
    vdf_document document;
    std::string error;
    REQUIRE(parse_vdf("\"key\" \"D:\\\\Jeux Игры\\\\\\\"quoted\\\"\"", document, error));
    REQUIRE(document.entries.size() == 1);
    CHECK(document.entries[0].value == "D:\\Jeux Игры\\\"quoted\"");
}

TEST_CASE("VDF comments and unquoted tokens are bounded syntax, not data") {
    vdf_document document;
    std::string error;
    REQUIRE(parse_vdf("// comment\nroot { key value }", document, error));
    const vdf_entry* root = find_vdf_entry(document.entries, "root");
    REQUIRE(root != 0);
    const vdf_entry* key = find_vdf_entry(root->children, "key");
    REQUIRE(key != 0);
    CHECK(key->value == "value");
}

TEST_CASE("duplicate VDF keys remain visible to the discovery policy") {
    vdf_document document;
    std::string error;
    REQUIRE(parse_vdf("\"path\" \"one\" \"path\" \"two\"", document, error));
    REQUIRE(document.entries.size() == 2);
    CHECK(document.entries[0].key == "path");
    CHECK(document.entries[1].key == "path");
    CHECK(document.entries[1].value == "two");
}

TEST_CASE("malformed, oversized, and hostile VDF input is rejected without partial output") {
    const std::string cases[] = {
        "\"root\" { \"key\" \"value\"",
        "\"root\" }",
        "\"key\"",
        "\"unterminated",
        std::string("\"key\" \"") + std::string(4097, 'x') + "\"",
        std::string(1024 * 1024 + 1, 'x')
    };
    for (std::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        vdf_document document;
        document.entries.push_back(vdf_entry());
        std::string error;
        CHECK_FALSE(parse_vdf(cases[i], document, error));
        CHECK(document.entries.empty());
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("hostile VDF nesting is capped") {
    std::string text;
    for (int i = 0; i < 18; i++) {
        text += "k{";
    }
    text += "v x";
    for (int i = 0; i < 18; i++) {
        text += "}";
    }
    vdf_document document;
    std::string error;
    CHECK_FALSE(parse_vdf(text, document, error));
    CHECK(document.entries.empty());
}
