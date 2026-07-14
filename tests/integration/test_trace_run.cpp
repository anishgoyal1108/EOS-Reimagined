#include "doctest.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "eos_sdk.h"
#include "eos_init.h"
#include "eos_connect.h"

#include "platform/dynlib.h"
#include "platform/paths.h"

using namespace eosr::platform;

// The path to the built .so/.dll under test, from the integration main.
extern std::string g_library_path;

namespace {

#define RESOLVE(var, api_name) \
    auto var = reinterpret_cast<decltype(&api_name)>(lib.symbol(#api_name)); \
    REQUIRE((var != nullptr))

bool g_login_fired = false;
EOS_EResult g_login_result = EOS_EResult::EOS_UnexpectedError;
void EOS_CALL on_login(const EOS_Connect_LoginCallbackInfo* info) {
    g_login_fired = true;
    g_login_result = info->ResultCode;
}

void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

std::string slurp(const std::string& path) {
    std::string out;
    read_file_capped(path, 16 * 1024 * 1024, out);
    return out;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            if (start < text.size()) {
                lines.push_back(text.substr(start));
            }
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

// Pull the integer value of the "seq" envelope field out of one record line, or -1 if absent.
long parse_seq(const std::string& line) {
    const std::string key = "\"seq\":";
    const std::size_t at = line.find(key);
    if (at == std::string::npos) {
        return -1;
    }
    std::size_t i = at + key.size();
    long value = 0;
    bool any = false;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
        value = value * 10 + (line[i] - '0');
        any = true;
        i++;
    }
    return any ? value : -1;
}

// The first line whose text contains `needle`, or empty if there is none.
std::string find_line(const std::vector<std::string>& lines, const std::string& needle) {
    for (std::size_t i = 0; i < lines.size(); i++) {
        if (lines[i].find(needle) != std::string::npos) {
            return lines[i];
        }
    }
    return std::string();
}

// The value of a string field ("corr":"c#0" -> c#0), or empty if the field is absent.
std::string field_of(const std::string& line, const std::string& name) {
    const std::string key = "\"" + name + "\":\"";
    const std::size_t at = line.find(key);
    if (at == std::string::npos) {
        return std::string();
    }
    const std::size_t start = at + key.size();
    const std::size_t end = line.find('"', start);
    return (end == std::string::npos) ? std::string() : line.substr(start, end - start);
}

} // namespace

// The real observability path: enable tracing through the loaded library and drive one whole EOS
// lifetime, then check the run it produced. Runner mode gives us a known directory to read back.
TEST_CASE("tracing through the loaded library produces a well-formed run") {
    REQUIRE_FALSE(g_library_path.empty());

    const std::string base = EOSR_PROBE_DIR;
    const std::string data = base + "/data";
    const std::string run = base + "/run-probe";
    REQUIRE(make_directories(data));
    REQUIRE(make_directories(run));
    // A run directory the runner "already created": clear any owned files a previous run left, so the
    // library exclusively creates a fresh trace.jsonl and runtime.json.
    remove_file(run + "/trace.jsonl");
    remove_file(run + "/runtime.json");

    set_env("EOSR_DATA_DIR", data.c_str());
    set_env("EOSR_RUN_DIR", run.c_str());
    set_env("EOSR_TRACE", "lifecycle");
    set_env("EOSR_INSTANCE_LABEL", "probe");

    dynamic_library lib;
    REQUIRE(lib.open(g_library_path.c_str()));
    RESOLVE(fn_initialize, EOS_Initialize);
    RESOLVE(fn_shutdown, EOS_Shutdown);
    RESOLVE(fn_create, EOS_Platform_Create);
    RESOLVE(fn_release, EOS_Platform_Release);
    RESOLVE(fn_tick, EOS_Platform_Tick);

    EOS_InitializeOptions iopts = {};
    iopts.ApiVersion = EOS_INITIALIZE_API_LATEST;
    iopts.ProductName = "TraceProbe";
    iopts.ProductVersion = "1.0.0";
    REQUIRE(fn_initialize(&iopts) == EOS_EResult::EOS_Success);

    EOS_Platform_Options popts = {};
    popts.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    popts.ProductId = "prod-abc";
    popts.SandboxId = "sandbox-1";
    popts.DeploymentId = "deploy-2";
    popts.ClientCredentials.ClientId = "client";
    popts.ClientCredentials.ClientSecret = "secret";
    EOS_HPlatform platform = fn_create(&popts);
    REQUIRE((platform != nullptr));

    // Drive one real asynchronous operation, exactly as a game would: log in, then tick until the
    // completion fires. This is what the call/return/callback correlation is checked against below.
    RESOLVE(fn_get_connect, EOS_Platform_GetConnectInterface);
    RESOLVE(fn_login, EOS_Connect_Login);
    EOS_HConnect connect = fn_get_connect(platform);
    REQUIRE((connect != nullptr));

    EOS_Connect_Credentials creds = {};
    creds.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    creds.Token = "unused";
    creds.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_LoginOptions login = {};
    login.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    login.Credentials = &creds;
    fn_login(connect, &login, nullptr, on_login);

    for (int i = 0; i < 8 && !g_login_fired; i++) {
        fn_tick(platform);
    }
    CHECK(g_login_fired);
    CHECK(g_login_result == EOS_EResult::EOS_Success);

    fn_release(platform);
    REQUIRE(fn_shutdown() == EOS_EResult::EOS_Success);
    lib.close();

    // runtime.json is present, well-formed, and identifies this run and this build.
    const std::string runtime = slurp(run + "/runtime.json");
    REQUIRE_FALSE(runtime.empty());
    CHECK(runtime[0] == '{');
    CHECK(runtime[runtime.size() - 1] == '}');
    CHECK(runtime.find("\"run_id\":\"run-probe\"") != std::string::npos);
    CHECK(runtime.find("\"schema_version\":1") != std::string::npos);
    CHECK(runtime.find("\"emulator_build\":\"eosr ") != std::string::npos);
    CHECK(runtime.find("\"trace_level\":\"lifecycle\"") != std::string::npos);

    // The trace is parseable JSONL, in run_start -> profile -> shutdown order, with a strictly
    // increasing sequence from zero.
    const std::string trace = slurp(run + "/trace.jsonl");
    REQUIRE_FALSE(trace.empty());
    const std::size_t at_start = trace.find("\"event\":\"run_start\"");
    const std::size_t at_profile = trace.find("\"event\":\"profile\"");
    const std::size_t at_shutdown = trace.find("\"event\":\"shutdown\"");
    CHECK(at_start != std::string::npos);
    CHECK(at_profile != std::string::npos);
    CHECK(at_shutdown != std::string::npos);
    CHECK(at_start < at_profile);
    CHECK(at_profile < at_shutdown);

    const std::vector<std::string> lines = split_lines(trace);
    REQUIRE(lines.size() >= 3);
    long expected = 0;
    for (std::size_t i = 0; i < lines.size(); i++) {
        CHECK(lines[i].size() >= 2);
        CHECK(lines[i][0] == '{');
        CHECK(lines[i][lines[i].size() - 1] == '}');
        CHECK(lines[i].find("\"kind\":") != std::string::npos);
        CHECK(parse_seq(lines[i]) == expected); // monotonic, contiguous, from zero
        expected++;
    }

    // The asynchronous login: its call, its synchronous return, and the callback that completed it a
    // tick later all carry one correlation id, so a reader can stitch the operation back together.
    const std::string call = find_line(lines, "\"kind\":\"call\"");
    const std::string ret = find_line(lines, "\"kind\":\"return\"");
    const std::string callback = find_line(lines, "\"kind\":\"callback\"");
    REQUIRE_FALSE(call.empty());
    REQUIRE_FALSE(ret.empty());
    REQUIRE_FALSE(callback.empty());

    const std::string corr = field_of(call, "corr");
    CHECK_FALSE(corr.empty());
    CHECK(field_of(ret, "corr") == corr);
    CHECK(field_of(callback, "corr") == corr);
    CHECK(field_of(call, "fn") == "EOS_Connect_Login");
    CHECK(field_of(callback, "fn") == "EOS_Connect_Login");

    // The kind of credential is recorded; the token is not. The local user is a label, not an id.
    CHECK(call.find("\"cred_type\":\"EOS_ECT_DEVICEID_ACCESS_TOKEN\"") != std::string::npos);
    CHECK(callback.find("\"name\":\"EOS_Success\"") != std::string::npos);
    CHECK(callback.find("\"puid\":\"puid#") != std::string::npos);
    CHECK(trace.find("\"unused\"") == std::string::npos); // the credential token never appears
}
