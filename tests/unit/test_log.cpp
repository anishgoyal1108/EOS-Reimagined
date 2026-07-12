#include "doctest.h"

#include <string>
#include <vector>

#include "common/log.h"

using namespace eosr;

namespace {

std::vector<std::string> g_captured;
int g_self_removing_count = 0;

void EOS_CALL capture(const EOS_LogMessage* message) {
    g_captured.push_back(message->Message);
}

void EOS_CALL capture_once(const EOS_LogMessage*) {
    g_self_removing_count++;
    logger::instance().set_callback(0);
}

// The logger is a process-global singleton, so each case restores the default state it found.
void reset_logger() {
    logger::instance().set_callback(0);
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Warning);
    g_captured.clear();
}

} // namespace

TEST_CASE("a message within the threshold reaches the callback") {
    reset_logger();
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Verbose);
    logger::instance().set_callback(capture);

    log_info("hello");
    REQUIRE(g_captured.size() == 1);
    CHECK(g_captured[0] == "hello");
    reset_logger();
}

TEST_CASE("a level below the threshold is filtered out") {
    reset_logger();
    logger::instance().set_callback(capture);
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Error);

    log_info("suppressed"); // Info (400) exceeds the Error (200) threshold
    CHECK(g_captured.empty());
    log_error("delivered"); // Error (200) is within the threshold
    CHECK(g_captured.size() == 1);
    reset_logger();
}

TEST_CASE("the Off level suppresses everything") {
    reset_logger();
    logger::instance().set_callback(capture);
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Off);

    log_error("nope");
    log_warn("nope");
    CHECK(g_captured.empty());
    reset_logger();
}

TEST_CASE("a per-category level overrides the default") {
    reset_logger();
    logger::instance().set_callback(capture);
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Off);
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_Core,
                                 EOS_ELogLevel::EOS_LOG_Verbose);

    log_info("core-visible"); // the core helpers log under EOS_LC_Core
    CHECK(g_captured.size() == 1);
    reset_logger();
}

TEST_CASE("logging without a callback is a safe no-op") {
    reset_logger();
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Verbose);
    log_info("into the void");
    CHECK(g_captured.empty());
    reset_logger();
}

TEST_CASE("a callback can remove itself without deadlocking") {
    reset_logger();
    g_self_removing_count = 0;
    logger::instance().set_level(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES,
                                 EOS_ELogLevel::EOS_LOG_Verbose);
    logger::instance().set_callback(capture_once);

    log_info("first");
    log_info("second");

    CHECK(g_self_removing_count == 1);
    reset_logger();
}
