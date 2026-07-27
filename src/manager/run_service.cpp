#include "manager/run_service.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>

#include "manager/json.h"
#include "manager/manager_state.h"
#include "manager/sha256.h"

namespace eosr {
namespace manager {

namespace {

const std::size_t max_runtime_bytes = 1024 * 1024;
const std::size_t max_owned_json_bytes = 1024 * 1024;
const std::size_t max_trace_line_bytes = 60000;
const std::size_t max_run_entries = 512;
const std::size_t max_root_entries = 4096;
const std::size_t max_trace_files = 64;
const u64 max_trace_records = 50000000;

std::string join_path(const std::string& left, const std::string& right) {
    if (left.empty()) return right;
    const char last = left[left.size() - 1];
    return (last == '/' || last == '\\') ? left + right : left + "/" + right;
}

bool safe_name(const std::string& name) {
    return !name.empty() && name != "." && name != ".." && name.size() <= 255 &&
           name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

std::string parent_path(const std::string& path) {
    if (path.empty()) return std::string();
    std::string value = path;
    while (value.size() > 1 && (value[value.size() - 1] == '/' || value[value.size() - 1] == '\\'))
        value.erase(value.size() - 1);
    const std::string::size_type at = value.find_last_of("/\\");
    if (at == std::string::npos) return std::string();
    if (at == 0) return value.substr(0, 1);
    return value.substr(0, at);
}

std::string base_name(const std::string& path) {
    if (path.empty()) return path;
    std::string value = path;
    while (value.size() > 1 && (value[value.size() - 1] == '/' || value[value.size() - 1] == '\\'))
        value.erase(value.size() - 1);
    const std::string::size_type at = value.find_last_of("/\\");
    return at == std::string::npos ? value : value.substr(at + 1);
}

bool same_parent(const std::string& first, const std::string& second) {
#ifdef _WIN32
    std::string a = parent_path(first);
    std::string b = parent_path(second);
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    return a == b;
#else
    return parent_path(first) == parent_path(second);
#endif
}

bool trace_file_number(const std::string& name, int& number) {
    number = 0;
    if (name == "trace.jsonl") return true;
    const std::string prefix = "trace.";
    const std::string suffix = ".jsonl";
    if (name.size() <= prefix.size() + suffix.size() ||
        name.compare(0, prefix.size(), prefix) != 0 ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    unsigned int value = 0;
    for (std::size_t i = prefix.size(); i < name.size() - suffix.size(); i++) {
        if (name[i] < '0' || name[i] > '9') return false;
        const unsigned int digit = static_cast<unsigned int>(name[i] - '0');
        if (value > 1000000U || (value == 1000000U && digit != 0)) return false;
        value = value * 10U + digit;
    }
    if (value == 0 || value > 1000000U) return false;
    number = static_cast<int>(value);
    return true;
}

bool trace_chronological(const run_entry& left, const run_entry& right) {
    int a = 0;
    int b = 0;
    trace_file_number(left.name, a);
    trace_file_number(right.name, b);
    if (a == 0) return false;
    if (b == 0) return true;
    return a > b;
}

bool trace_bundle_order(const run_entry& left, const run_entry& right) {
    int a = 0;
    int b = 0;
    trace_file_number(left.name, a);
    trace_file_number(right.name, b);
    if (a == 0 || b == 0) return a == 0 && b != 0;
    return a < b;
}

bool value_string(const json_value& object, const char* name, std::string& out, bool nullable) {
    const json_value* value = json_member(object, name);
    if (nullable && value != 0 && value->kind == json_kind::null_value) {
        out.clear();
        return true;
    }
    if (value == 0 || value->kind != json_kind::string) return false;
    out = value->text;
    return true;
}

bool value_uint(const json_value& object, const char* name, u64& out) {
    const json_value* value = json_member(object, name);
    if (value == 0 || value->kind != json_kind::integer || value->integer < 0) return false;
    out = static_cast<u64>(value->integer);
    return true;
}

bool optional_string(const json_value& object, const char* name, std::string& out) {
    const json_value* value = json_member(object, name);
    if (value == 0) return true;
    if (value->kind != json_kind::string) return false;
    out = value->text;
    return true;
}

bool optional_bool(const json_value& object, const char* name, bool& out) {
    const json_value* value = json_member(object, name);
    if (value == 0) return true;
    if (value->kind != json_kind::boolean) return false;
    out = value->boolean;
    return true;
}

bool optional_uint(const json_value& object, const char* name, u64& out) {
    const json_value* value = json_member(object, name);
    if (value == 0) return true;
    if (value->kind != json_kind::integer || value->integer < 0) return false;
    out = static_cast<u64>(value->integer);
    return true;
}

bool optional_origin(const json_value& object, const char* name,
                     std::map<std::string, std::string>& out) {
    const json_value* value = json_member(object, name);
    if (value == 0 || value->kind == json_kind::null_value) return true;
    if (value->kind != json_kind::string ||
        (value->text != "default" && value->text != "file" &&
         value->text != "environment")) return false;
    out[name] = value->text;
    return true;
}

bool parse_runtime(const std::string& bytes, const std::string& directory, run_summary& out,
                   std::string& error) {
    json_value root;
    if (!parse_json(bytes, root, error) || root.kind != json_kind::object) {
        if (error.empty()) error = "runtime metadata is not an object";
        return false;
    }
    const json_value* schema = json_member(root, "schema_version");
    if (schema == 0 || schema->kind != json_kind::integer || schema->integer != 1 ||
        !value_string(root, "run_id", out.run_id, false) || !safe_name(out.run_id) ||
        out.run_id != base_name(directory) ||
        !value_string(root, "emulator_build", out.emulator_build, false) ||
        !value_string(root, "created_utc", out.created_utc, true) ||
        !value_string(root, "instance_label", out.instance_label, true)) {
        error = "runtime metadata has an invalid required field";
        return false;
    }
    const json_value* os = json_member(root, "os");
    const json_value* config = json_member(root, "config");
    if (os == 0 || os->kind != json_kind::object || config == 0 ||
        config->kind != json_kind::object || !value_string(*os, "name", out.os_name, false) ||
        !value_string(*os, "version", out.os_version, true) ||
        !value_string(*os, "wine", out.wine_version, true) ||
        !value_string(*config, "trace_level", out.trace_level, false)) {
        error = "runtime OS or configuration metadata is invalid";
        return false;
    }
    out.effective_config_present = json_member(*config, "display_name") != 0;
    if (!optional_string(*config, "display_name", out.effective_display_name) ||
        !optional_string(*config, "locale", out.effective_locale) ||
        !optional_string(*config, "log_level", out.effective_log_level) ||
        !optional_string(*config, "trace_dir", out.effective_trace_dir) ||
        !optional_uint(*config, "trace_max_bytes", out.effective_trace_max_bytes) ||
        !optional_uint(*config, "trace_max_rotated_files",
                       out.effective_trace_max_rotated_files) ||
        !optional_bool(*config, "enable_lan", out.effective_enable_lan) ||
        !optional_bool(*config, "enable_overlay", out.effective_enable_overlay) ||
        !optional_bool(*config, "unlock_dlcs", out.effective_unlock_dlcs) ||
        !optional_uint(*config, "peer_seed_count", out.effective_peer_seed_count)) {
        error = "runtime effective configuration metadata is invalid";
        return false;
    }
    const json_value* ports = json_member(*config, "discovery_ports");
    if (ports != 0) {
        if (ports->kind != json_kind::array || ports->elements.size() != 2 ||
            ports->elements[0].kind != json_kind::integer ||
            ports->elements[1].kind != json_kind::integer || ports->elements[0].integer < 0 ||
            ports->elements[1].integer < 0) {
            error = "runtime effective discovery ports are invalid";
            return false;
        }
        out.effective_discovery_first = static_cast<u64>(ports->elements[0].integer);
        out.effective_discovery_last = static_cast<u64>(ports->elements[1].integer);
    }
    const json_value* sources = json_member(*config, "sources");
    if (sources != 0) {
        if (sources->kind != json_kind::object ||
            !optional_origin(*sources, "display_name", out.effective_sources) ||
            !optional_origin(*sources, "locale", out.effective_sources) ||
            !optional_origin(*sources, "trace_level", out.effective_sources) ||
            !optional_origin(*sources, "log_level", out.effective_sources) ||
            !optional_origin(*sources, "trace_dir", out.effective_sources) ||
            !optional_origin(*sources, "trace_max_bytes", out.effective_sources) ||
            !optional_origin(*sources, "trace_max_rotated_files", out.effective_sources) ||
            !optional_origin(*sources, "discovery_ports", out.effective_sources) ||
            !optional_origin(*sources, "peer_seeds", out.effective_sources) ||
            !optional_origin(*sources, "instance_label", out.effective_sources) ||
            !optional_origin(*sources, "enable_lan", out.effective_sources) ||
            !optional_origin(*sources, "enable_overlay", out.effective_sources) ||
            !optional_origin(*sources, "unlock_dlcs", out.effective_sources)) {
            error = "runtime effective configuration sources are invalid";
            return false;
        }
    }
    return true;
}

operation_outcome& operation_for(run_summary& summary, const std::string& function) {
    for (std::size_t i = 0; i < summary.operations.size(); i++) {
        if (summary.operations[i].function == function) return summary.operations[i];
    }
    operation_outcome value;
    value.function = function;
    summary.operations.push_back(value);
    return summary.operations.back();
}

bool relevant_operation(const std::string& fn) {
    const bool area = fn.find("Session") != std::string::npos ||
                      fn.find("Lobby") != std::string::npos;
    const bool action = fn.find("Search") != std::string::npos ||
                        fn.find("Find") != std::string::npos ||
                        fn.find("Join") != std::string::npos;
    return area && action;
}

void add_stub(run_summary& summary, const std::string& function) {
    if (std::find(summary.stubbed_functions.begin(), summary.stubbed_functions.end(), function) ==
        summary.stubbed_functions.end()) summary.stubbed_functions.push_back(function);
}

void count_record(const json_value& record, run_summary& summary, bool& shutdown) {
    std::string kind;
    if (!value_string(record, "kind", kind, false)) return;
    if (kind == "meta") {
        std::string event;
        if (!value_string(record, "event", event, false)) return;
        if (event == "run_start") summary.sdk_initialized = true;
        if (event == "shutdown") shutdown = true;
        return;
    }
    if (kind == "net") {
        std::string event;
        if (!value_string(record, "event", event, false)) return;
        std::string reason;
        value_string(record, "reason", reason, false);
        u64 bytes = 0;
        value_uint(record, "bytes", bytes);
        if (event == "listen") value_uint(record, "port", summary.discovery_port);
        else if (event == "discover") summary.discover_count++;
        else if (event == "handshake") {
            summary.handshake_count++;
            if (!reason.empty()) summary.handshake_reasons[reason]++;
        } else if (event == "adopt") summary.adopt_count++;
        else if (event == "drop") {
            summary.drop_count++;
            if (!reason.empty()) summary.drop_reasons[reason]++;
        } else if (event == "p2p_open") summary.p2p_open_count++;
        else if (event == "p2p_close") summary.p2p_close_count++;
        else if (event == "p2p_send") summary.p2p_bytes_sent += bytes;
        else if (event == "p2p_receive") summary.p2p_bytes_received += bytes;
        return;
    }
    if (kind != "return" && kind != "callback") return;
    std::string function;
    if (!value_string(record, "fn", function, false)) return;
    const json_value* result = json_member(record, "result");
    if (result == 0 || result->kind != json_kind::object) return;
    u64 code = 0;
    std::string name;
    if (!value_uint(*result, "code", code) || !value_string(*result, "name", name, true)) return;
    if (name == "EOS_NotImplemented") add_stub(summary, function);
    if (relevant_operation(function)) {
        operation_outcome& outcome = operation_for(summary, function);
        outcome.total++;
        if (code == 0 || name == "EOS_Success") outcome.successes++;
        else outcome.failures++;
        outcome.results[name.empty() ? std::string("unknown") : name]++;
    }
}

struct trace_stream_result {
    trace_stream_result() : ok(false), records(0), torn(0) {}
    bool ok;
    u64 records;
    u64 torn;
    std::string sha256;
    std::string error;
};

bool write_all(run_filesystem& filesystem, run_handle handle, const unsigned char* data,
               std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        std::size_t count = 0;
        if (filesystem.write(handle, data + offset, size - offset, count) != run_io_result::ok ||
            count == 0 || count > size - offset) return false;
        offset += count;
    }
    return true;
}

trace_stream_result stream_trace(const std::string& path, run_filesystem& filesystem,
                                 run_summary* summary, run_handle* output) {
    trace_stream_result result;
    run_handle input = 0;
    if (filesystem.open_read(path, input) != run_io_result::ok) {
        result.error = "trace file could not be opened";
        return result;
    }
    sha256_hasher hash;
    sha256_hasher copied_hash;
    std::string line;
    bool shutdown = false;
    bool failed = false;
    unsigned char buffer[8192];
    while (!failed) {
        std::size_t count = 0;
        const run_io_result read = filesystem.read(input, buffer, sizeof(buffer), count);
        if (read == run_io_result::end_of_file) break;
        if (read != run_io_result::ok || count == 0 || count > sizeof(buffer)) {
            result.error = "trace file read failed";
            failed = true;
            break;
        }
        hash.update(buffer, count);
        for (std::size_t i = 0; i < count; i++) {
            if (buffer[i] == '\n') {
                json_value record;
                std::string error;
                if (line.size() > max_trace_line_bytes) {
                    result.error = "trace contains an oversized complete record";
                    failed = true;
                    break;
                }
                if (!parse_json(line, record, error) || record.kind != json_kind::object) {
                    result.error = "trace contains an invalid complete record";
                    failed = true;
                    break;
                }
                if (result.records >= max_trace_records) {
                    result.error = "trace record limit exceeded";
                    failed = true;
                    break;
                }
                if (summary != 0) count_record(record, *summary, shutdown);
                if (output != 0) {
                    // `inst` is the raw instance label in every SDK trace record. It is not an
                    // opaque tracer label and can contain a player's real name, so support traces
                    // use one stable bundle-local pseudonym instead.
                    record.members["inst"] = json_string("instance#0");
                    std::string sanitized_line = serialize_json(record);
                    // serialize_json is file-oriented and terminates a document. stream_trace
                    // owns the JSONL delimiter, so remove that terminator before appending exactly
                    // one newline below.
                    if (!sanitized_line.empty() &&
                        sanitized_line[sanitized_line.size() - 1] == '\n') {
                        sanitized_line.erase(sanitized_line.size() - 1);
                    }
                    if (!write_all(filesystem, *output,
                                   reinterpret_cast<const unsigned char*>(sanitized_line.data()),
                                   sanitized_line.size()) ||
                        !write_all(filesystem, *output,
                                   reinterpret_cast<const unsigned char*>("\n"), 1)) {
                        result.error = "support trace write failed";
                        failed = true;
                        break;
                    }
                    copied_hash.update(
                        reinterpret_cast<const unsigned char*>(sanitized_line.data()),
                        sanitized_line.size());
                    copied_hash.update(reinterpret_cast<const unsigned char*>("\n"), 1);
                }
                result.records++;
                line.clear();
            } else {
                line.push_back(static_cast<char>(buffer[i]));
                if (line.size() > max_trace_line_bytes) {
                    // It may be the torn tail, but once it is this wide it cannot be a valid SDK
                    // record and keeping it until EOF would defeat the line bound.
                    result.error = "trace contains an oversized record";
                    failed = true;
                    break;
                }
            }
        }
    }
    if (!failed && !line.empty()) result.torn = 1;
    if (filesystem.close(input) != run_io_result::ok && !failed) {
        result.error = "trace file close failed";
        failed = true;
    }
    result.sha256 = output == 0 ? hash.final_hex() : copied_hash.final_hex();
    result.ok = !failed;
    if (summary != 0 && shutdown) summary->status = diagnostic_run_status::completed;
    return result;
}

bool same_metadata(const std::vector<run_entry>& a, const std::vector<run_entry>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++) {
        if (a[i].name != b[i].name || a[i].kind != b[i].kind || a[i].size != b[i].size ||
            a[i].modified != b[i].modified) return false;
    }
    return true;
}

bool active_id(const std::string& id, const std::vector<std::string>& active) {
    return std::find(active.begin(), active.end(), id) != active.end();
}

void index_diagnostic(run_index_result& result, const std::string& directory,
                      run_filesystem& filesystem, run_summary_cache& cache,
                      const std::vector<std::string>& active) {
    std::vector<run_entry> entries;
    if (filesystem.list_directory(directory, entries) != run_io_result::ok ||
        entries.size() > max_run_entries) {
        run_index_diagnostic diagnostic;
        diagnostic.code = "run_unreadable";
        diagnostic.path = directory;
        diagnostic.detail = entries.size() > max_run_entries ? "run entry limit exceeded" :
                                                               "run directory could not be read";
        result.diagnostics.push_back(diagnostic);
        return;
    }
    std::vector<run_entry> metadata;
    std::vector<run_entry> traces;
    for (std::size_t i = 0; i < entries.size(); i++) {
        int ignored = 0;
        if (!safe_name(entries[i].name)) continue;
        if (entries[i].name == "runtime.json" ||
            (entries[i].kind == run_entry_kind::file && trace_file_number(entries[i].name, ignored)))
            metadata.push_back(entries[i]);
        if (entries[i].kind == run_entry_kind::file && trace_file_number(entries[i].name, ignored))
            traces.push_back(entries[i]);
    }
    std::sort(metadata.begin(), metadata.end(),
              [](const run_entry& a, const run_entry& b) { return a.name < b.name; });
    for (std::size_t i = 0; i < cache.entries.size(); i++) {
        if (cache.entries[i].directory == directory &&
            same_metadata(cache.entries[i].metadata, metadata)) {
            run_summary cached = cache.entries[i].summary;
            if (active_id(cached.run_id, active)) cached.status = diagnostic_run_status::active;
            result.runs.push_back(cached);
            result.cache_hits++;
            return;
        }
    }

    std::string runtime_bytes;
    const run_io_result runtime_read =
        filesystem.read_file(join_path(directory, "runtime.json"), max_runtime_bytes, runtime_bytes);
    run_summary summary;
    summary.directory = directory;
    std::string runtime_error;
    if (runtime_read != run_io_result::ok || !parse_runtime(runtime_bytes, directory, summary,
                                                            runtime_error)) {
        run_index_diagnostic diagnostic;
        diagnostic.code = "runtime_invalid";
        diagnostic.path = directory;
        diagnostic.detail = runtime_read == run_io_result::too_large ?
                                "runtime metadata is oversized" :
                                (runtime_error.empty() ? "runtime metadata could not be read" :
                                                         runtime_error);
        result.diagnostics.push_back(diagnostic);
        return;
    }
    if (traces.size() > max_trace_files) {
        summary.trace_invalid = true;
        summary.diagnostic = "trace file limit exceeded";
    } else {
        std::sort(traces.begin(), traces.end(), trace_chronological);
        for (std::size_t i = 0; i < traces.size(); i++) {
            const trace_stream_result streamed =
                stream_trace(join_path(directory, traces[i].name), filesystem, &summary, 0);
            trace_file_signature signature;
            signature.name = traces[i].name;
            signature.size = traces[i].size;
            signature.modified = traces[i].modified;
            signature.sha256 = streamed.sha256;
            signature.records = streamed.records;
            summary.trace_files.push_back(signature);
            summary.torn_tail_records += streamed.torn;
            if (!streamed.ok) {
                summary.trace_invalid = true;
                summary.diagnostic = streamed.error;
                break;
            }
        }
    }
    std::sort(summary.operations.begin(), summary.operations.end(),
              [](const operation_outcome& a, const operation_outcome& b) {
                  return a.function < b.function;
              });
    std::sort(summary.stubbed_functions.begin(), summary.stubbed_functions.end());
    run_cache_entry cached;
    cached.directory = directory;
    cached.metadata = metadata;
    cached.summary = summary;
    bool replaced = false;
    for (std::size_t i = 0; i < cache.entries.size(); i++) {
        if (cache.entries[i].directory == directory) {
            cache.entries[i] = cached;
            replaced = true;
            break;
        }
    }
    if (!replaced) cache.entries.push_back(cached);
    if (active_id(summary.run_id, active)) summary.status = diagnostic_run_status::active;
    result.runs.push_back(summary);
}

std::string lower_ascii(const std::string& text) {
    std::string out = text;
    for (std::size_t i = 0; i < out.size(); i++) {
        if (out[i] >= 'A' && out[i] <= 'Z') out[i] = static_cast<char>(out[i] - 'A' + 'a');
    }
    return out;
}

bool absolute_string(const std::string& text) {
    return (!text.empty() && (text[0] == '/' || text[0] == '\\')) ||
           (text.size() >= 3 && std::isalpha(static_cast<unsigned char>(text[0])) != 0 &&
            text[1] == ':' && (text[2] == '/' || text[2] == '\\'));
}

json_value sanitize_owned(const json_value& source, bool source_origins = false) {
    if (source.kind == json_kind::object) {
        json_value out = json_object();
        for (std::map<std::string, json_value>::const_iterator it = source.members.begin();
             it != source.members.end(); ++it) {
            const std::string key = lower_ascii(it->first);
            if (key == "display_name" || key == "username" || key == "message" ||
                key == "error") continue;
            // Instance labels are operationally useful in a private run but user-selected and
            // frequently contain the player's real name. Keep the effective-source origin under
            // config.sources, but pseudonymize every actual label in shareable metadata.
            if (key == "instance_label" && !source_origins) {
                out.members[it->first] = json_string("instance#0");
            } else {
                out.members[it->first] = sanitize_owned(it->second, key == "sources");
            }
        }
        return out;
    }
    if (source.kind == json_kind::array) {
        json_value out = json_array();
        for (std::size_t i = 0; i < source.elements.size(); i++)
            out.elements.push_back(sanitize_owned(source.elements[i], source_origins));
        return out;
    }
    if (source.kind == json_kind::string && absolute_string(source.text))
        return json_string(base_name(source.text));
    return source;
}

bool write_new_file(const std::string& path, const std::string& bytes,
                    run_filesystem& filesystem, std::vector<std::string>& created) {
    run_handle output = 0;
    if (filesystem.create_new(path, output) != run_io_result::ok) return false;
    created.push_back(path);
    bool ok = write_all(filesystem, output,
                        reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
    if (ok) ok = filesystem.flush(output) == run_io_result::ok;
    if (filesystem.close(output) != run_io_result::ok) ok = false;
    if (ok) ok = filesystem.flush_parent(path) == run_io_result::ok;
    return ok;
}

void exclude(support_bundle_result& result, const std::string& name, const std::string& reason) {
    bundle_exclusion value;
    value.name = name;
    value.reason = reason;
    result.excluded_files.push_back(value);
}

json_value exclusion_json(const bundle_exclusion& value) {
    json_value out = json_object();
    out.members["name"] = json_string(value.name);
    out.members["reason"] = json_string(value.reason);
    return out;
}

json_value bundle_trace_json(const bundle_file_result& value) {
    json_value out = json_object();
    out.members["name"] = json_string(value.name);
    out.members["records"] = json_int(static_cast<i64>(value.records));
    out.members["sha256"] = json_string(value.sha256);
    return out;
}

bool cleanup_bundle(const std::string& directory, const std::vector<std::string>& created,
                    run_filesystem& filesystem) {
    bool ok = true;
    for (std::size_t i = created.size(); i > 0; i--) {
        const run_io_result removed = filesystem.remove_file(created[i - 1]);
        if (removed != run_io_result::ok && removed != run_io_result::missing) ok = false;
    }
    const run_io_result removed = filesystem.remove_empty_directory(directory);
    if (removed != run_io_result::ok && removed != run_io_result::missing) ok = false;
    if (filesystem.flush_parent(directory) != run_io_result::ok) ok = false;
    return ok;
}

std::string trimmed_path(const std::string& path) {
    std::string out = path;
    while (out.size() > 1 && (out[out.size() - 1] == '/' || out[out.size() - 1] == '\\'))
        out.erase(out.size() - 1);
    return out;
}

bool same_path(const std::string& first, const std::string& second) {
    std::string a = trimmed_path(first);
    std::string b = trimmed_path(second);
#ifdef _WIN32
    for (std::size_t i = 0; i < a.size(); i++) {
        if (a[i] == '\\') a[i] = '/';
        a[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
    }
    for (std::size_t i = 0; i < b.size(); i++) {
        if (b[i] == '\\') b[i] = '/';
        b[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
    }
#endif
    return a == b;
}

struct owned_tree_plan {
    std::vector<std::string> files;
    std::vector<std::string> directories;
    std::size_t entries;
    owned_tree_remove_code code;
    std::string detail;

    owned_tree_plan() : entries(0), code(owned_tree_remove_code::removed) {}
};

bool preflight_owned_tree(const std::string& directory, std::size_t depth,
                          const owned_tree_remove_request& request,
                          run_filesystem& filesystem, owned_tree_plan& plan) {
    if (depth > request.max_depth) {
        plan.code = owned_tree_remove_code::bounds_exceeded;
        plan.detail = "owned directory depth limit exceeded";
        return false;
    }
    std::vector<run_entry> entries;
    const run_io_result listed = filesystem.list_directory(directory, entries);
    if (listed != run_io_result::ok) {
        plan.code = listed == run_io_result::too_large ?
            owned_tree_remove_code::bounds_exceeded : owned_tree_remove_code::preflight_failed;
        plan.detail = "owned directory could not be completely enumerated";
        return false;
    }
    for (std::size_t i = 0; i < entries.size(); i++) {
        if (!safe_name(entries[i].name)) {
            plan.code = owned_tree_remove_code::unsafe_entry;
            plan.detail = "owned directory contains an unsafe entry name";
            return false;
        }
        plan.entries++;
        if (plan.entries > request.max_entries) {
            plan.code = owned_tree_remove_code::bounds_exceeded;
            plan.detail = "owned directory entry limit exceeded";
            return false;
        }
        const std::string child = join_path(directory, entries[i].name);
        if (entries[i].kind == run_entry_kind::file) {
            plan.files.push_back(child);
        } else if (entries[i].kind == run_entry_kind::directory) {
            if (!preflight_owned_tree(child, depth + 1, request, filesystem, plan)) return false;
        } else {
            plan.code = owned_tree_remove_code::unsafe_entry;
            plan.detail = "owned directory contains a symlink or unsupported entry";
            return false;
        }
    }
    plan.directories.push_back(directory);
    return true;
}

} // namespace

run_entry::run_entry() : kind(run_entry_kind::other), size(0), modified(0) {}

operation_outcome::operation_outcome() : total(0), successes(0), failures(0) {}

run_summary::run_summary()
    : effective_trace_max_bytes(0), effective_trace_max_rotated_files(0),
      effective_enable_lan(false), effective_enable_overlay(false), effective_unlock_dlcs(false),
      effective_discovery_first(0), effective_discovery_last(0), effective_peer_seed_count(0),
      effective_config_present(false), status(diagnostic_run_status::incomplete),
      sdk_initialized(false), trace_invalid(false),
      discovery_port(0), discover_count(0), handshake_count(0), adopt_count(0), drop_count(0),
      p2p_open_count(0), p2p_close_count(0), p2p_bytes_sent(0), p2p_bytes_received(0),
      torn_tail_records(0) {}

support_bundle_result::support_bundle_result()
    : code(support_bundle_code::invalid_request), shareable(false), torn_tail_records_dropped(0) {}

owned_tree_remove_request::owned_tree_remove_request() : max_depth(16), max_entries(4096) {}

owned_tree_remove_result::owned_tree_remove_result()
    : code(owned_tree_remove_code::invalid_request), files_removed(0), directories_removed(0) {}

run_index_result index_diagnostic_runs(const std::string& trace_root, run_filesystem& filesystem,
                                       run_summary_cache& cache,
                                       const std::vector<std::string>& active_run_ids) {
    run_index_result result;
    result.cache_hits = 0;
    std::vector<run_entry> entries;
    if (filesystem.list_directory(trace_root, entries) != run_io_result::ok) {
        run_index_diagnostic diagnostic;
        diagnostic.code = "trace_root_unreadable";
        diagnostic.path = trace_root;
        diagnostic.detail = "trace root could not be read";
        result.diagnostics.push_back(diagnostic);
        return result;
    }
    if (entries.size() > max_root_entries) {
        run_index_diagnostic diagnostic;
        diagnostic.code = "trace_root_bounded";
        diagnostic.path = trace_root;
        diagnostic.detail = "trace root entry limit exceeded";
        result.diagnostics.push_back(diagnostic);
        return result;
    }
    std::sort(entries.begin(), entries.end(),
              [](const run_entry& a, const run_entry& b) { return a.name < b.name; });
    for (std::size_t i = 0; i < entries.size(); i++) {
        if (entries[i].kind == run_entry_kind::directory && safe_name(entries[i].name))
            index_diagnostic(result, join_path(trace_root, entries[i].name), filesystem, cache,
                             active_run_ids);
    }
    std::sort(result.runs.begin(), result.runs.end(),
              [](const run_summary& a, const run_summary& b) {
                  if (a.created_utc != b.created_utc) return a.created_utc > b.created_utc;
                  return a.run_id > b.run_id;
              });
    return result;
}

support_bundle_result create_support_bundle(const support_bundle_request& request,
                                            run_filesystem& filesystem) {
    support_bundle_result result;
    result.directory = request.bundle_directory;
    if (!manager_absolute_path(request.run_directory) ||
        !manager_absolute_path(request.bundle_directory) ||
        request.run_directory == request.bundle_directory ||
        !safe_name(base_name(request.bundle_directory)) ||
        !same_parent(request.run_directory, request.bundle_directory) ||
        request.generated_utc.empty()) {
        result.detail = "bundle must be a new direct sibling of an absolute run directory";
        return result;
    }
    std::vector<run_entry> entries;
    const run_io_result listed = filesystem.list_directory(request.run_directory, entries);
    if (listed != run_io_result::ok || entries.size() > max_run_entries) {
        result.code = listed == run_io_result::missing ? support_bundle_code::run_missing :
                                                        support_bundle_code::source_invalid;
        result.detail = "run directory could not be read within its entry bound";
        return result;
    }
    const run_io_result made = filesystem.create_private_directory(request.bundle_directory);
    if (made != run_io_result::ok) {
        result.code = made == run_io_result::exists ? support_bundle_code::bundle_exists :
                                                     support_bundle_code::create_failed;
        result.detail = "bundle directory could not be exclusively created";
        return result;
    }
    std::vector<std::string> created;
    std::vector<run_entry> traces;
    json_value runtime;
    json_value launch;
    json_value inspection;
    runtime.kind = json_kind::null_value;
    launch.kind = json_kind::null_value;
    inspection.kind = json_kind::null_value;

    std::sort(entries.begin(), entries.end(),
              [](const run_entry& a, const run_entry& b) { return a.name < b.name; });
    for (std::size_t i = 0; i < entries.size(); i++) {
        int ignored = 0;
        if (!safe_name(entries[i].name) || entries[i].kind != run_entry_kind::file) {
            exclude(result, entries[i].name, entries[i].kind == run_entry_kind::symlink ?
                                                    "symlink_not_followed" : "not_owned_file");
            continue;
        }
        if (trace_file_number(entries[i].name, ignored)) {
            traces.push_back(entries[i]);
            continue;
        }
        json_value* target = 0;
        if (entries[i].name == "runtime.json") target = &runtime;
        else if (entries[i].name == "launch.json") target = &launch;
        else if (entries[i].name == "inspection.json") target = &inspection;
        if (target != 0) {
            std::string bytes;
            std::string error;
            if (filesystem.read_file(join_path(request.run_directory, entries[i].name),
                                     max_owned_json_bytes, bytes) == run_io_result::ok &&
                parse_json(bytes, *target, error) && target->kind == json_kind::object) {
                *target = sanitize_owned(*target);
            } else {
                target->kind = json_kind::null_value;
                exclude(result, entries[i].name, "invalid_owned_metadata");
            }
        } else if (entries[i].name == "stdout.log")
            exclude(result, entries[i].name, "unsanitized_stdout");
        else if (lower_ascii(entries[i].name) == "profile.key")
            exclude(result, entries[i].name, "identity_credential");
        else
            exclude(result, entries[i].name, "not_owned_file");
    }
    if (traces.size() > max_trace_files) {
        result.code = support_bundle_code::source_invalid;
        result.detail = "trace file limit exceeded";
        if (!cleanup_bundle(request.bundle_directory, created, filesystem))
            result.code = support_bundle_code::cleanup_incomplete;
        return result;
    }
    std::sort(traces.begin(), traces.end(), trace_bundle_order);
    for (std::size_t i = 0; i < traces.size(); i++) {
        const std::string output_path = join_path(request.bundle_directory, traces[i].name);
        run_handle output = 0;
        if (filesystem.create_new(output_path, output) != run_io_result::ok) {
            result.code = support_bundle_code::write_failed;
            result.detail = "support trace could not be exclusively created";
            if (!cleanup_bundle(request.bundle_directory, created, filesystem))
                result.code = support_bundle_code::cleanup_incomplete;
            return result;
        }
        created.push_back(output_path);
        const trace_stream_result streamed =
            stream_trace(join_path(request.run_directory, traces[i].name), filesystem, 0, &output);
        bool complete = streamed.ok && filesystem.flush(output) == run_io_result::ok;
        if (filesystem.close(output) != run_io_result::ok) complete = false;
        if (complete) complete = filesystem.flush_parent(output_path) == run_io_result::ok;
        if (!complete) {
            result.code = support_bundle_code::source_invalid;
            result.detail = streamed.error.empty() ? "support trace write failed" : streamed.error;
            if (!cleanup_bundle(request.bundle_directory, created, filesystem))
                result.code = support_bundle_code::cleanup_incomplete;
            return result;
        }
        bundle_file_result included;
        included.name = traces[i].name;
        included.records = streamed.records;
        // The input hash differs from the copied hash only when a torn tail was dropped. Hash the
        // bounded output semantics explicitly by accumulating complete record bytes in stream_trace
        // would duplicate its parser, so read the new file as a stream on the next index. For bundle
        // reporting, hash the source when complete and mark a dropped tail in the summary.
        included.sha256 = streamed.sha256;
        result.included_trace_files.push_back(included);
        result.torn_tail_records_dropped += streamed.torn;
    }

    json_value summary = json_object();
    summary.members["schema_version"] = json_int(1);
    summary.members["sanitizer"] = json_string("eosr-deny-by-default-v1");
    summary.members["shareable"] = json_bool(true);
    summary.members["generated_utc"] = json_string(request.generated_utc);
    summary.members["runtime"] = runtime;
    summary.members["launch"] = launch;
    summary.members["inspection"] = inspection;
    json_value trace = json_object();
    json_value files = json_array();
    for (std::size_t i = 0; i < result.included_trace_files.size(); i++)
        files.elements.push_back(bundle_trace_json(result.included_trace_files[i]));
    trace.members["files"] = files;
    trace.members["torn_tail_records_dropped"] =
        json_int(static_cast<i64>(result.torn_tail_records_dropped));
    summary.members["trace"] = trace;
    json_value included = json_array();
    included.elements.push_back(json_string("summary.json"));
    for (std::size_t i = 0; i < result.included_trace_files.size(); i++)
        included.elements.push_back(json_string(result.included_trace_files[i].name));
    summary.members["included_files"] = included;
    json_value excluded = json_array();
    for (std::size_t i = 0; i < result.excluded_files.size(); i++)
        excluded.elements.push_back(exclusion_json(result.excluded_files[i]));
    summary.members["excluded_files"] = excluded;
    if (!write_new_file(join_path(request.bundle_directory, "summary.json"),
                        serialize_json(summary), filesystem, created)) {
        result.code = support_bundle_code::write_failed;
        result.detail = "sanitized bundle summary could not be durably written";
        if (!cleanup_bundle(request.bundle_directory, created, filesystem))
            result.code = support_bundle_code::cleanup_incomplete;
        return result;
    }
    if (filesystem.flush_parent(request.bundle_directory) != run_io_result::ok) {
        result.code = support_bundle_code::write_failed;
        result.detail = "bundle directory could not be durably committed";
        if (!cleanup_bundle(request.bundle_directory, created, filesystem))
            result.code = support_bundle_code::cleanup_incomplete;
        return result;
    }
    result.code = support_bundle_code::created;
    result.shareable = true;
    return result;
}

owned_tree_remove_result remove_owned_tree(const owned_tree_remove_request& request,
                                           run_filesystem& filesystem) {
    owned_tree_remove_result result;
    if (!manager_absolute_path(request.parent_directory) ||
        !manager_absolute_path(request.directory) || request.max_depth == 0 ||
        request.max_entries == 0 || !safe_name(base_name(request.directory)) ||
        !same_path(parent_path(request.directory), request.parent_directory)) {
        result.detail = "removal target must be a safe direct child of its declared owner";
        return result;
    }
    std::vector<run_entry> root_entries;
    const run_io_result root = filesystem.list_directory(request.directory, root_entries);
    if (root == run_io_result::missing) {
        result.code = owned_tree_remove_code::missing;
        result.detail = "owned directory does not exist";
        return result;
    }
    if (root != run_io_result::ok) {
        result.code = root == run_io_result::too_large ? owned_tree_remove_code::bounds_exceeded :
                                                        owned_tree_remove_code::preflight_failed;
        result.detail = "owned directory cannot be safely enumerated";
        return result;
    }
    owned_tree_plan plan;
    if (!preflight_owned_tree(request.directory, 1, request, filesystem, plan)) {
        result.code = plan.code;
        result.detail = plan.detail;
        return result;
    }
    for (std::size_t i = 0; i < plan.files.size(); i++) {
        if (filesystem.remove_file(plan.files[i]) != run_io_result::ok) {
            result.code = owned_tree_remove_code::cleanup_incomplete;
            result.detail = "owned file removal failed; retry after closing file users";
            return result;
        }
        result.files_removed++;
    }
    for (std::size_t i = 0; i < plan.directories.size(); i++) {
        if (filesystem.remove_empty_directory(plan.directories[i]) != run_io_result::ok) {
            result.code = owned_tree_remove_code::cleanup_incomplete;
            result.detail = "owned directory removal is incomplete and safe to retry";
            return result;
        }
        result.directories_removed++;
    }
    if (filesystem.flush_parent(request.directory) != run_io_result::ok) {
        result.code = owned_tree_remove_code::cleanup_incomplete;
        result.detail = "removal completed but parent durability could not be confirmed";
        return result;
    }
    result.code = owned_tree_remove_code::removed;
    result.detail = "owned directory removed";
    return result;
}

} // namespace manager
} // namespace eosr
