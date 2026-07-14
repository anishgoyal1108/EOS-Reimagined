#include "core/trace_sink.h"

#include <string>

#include "common/log.h"
#include "platform/paths.h"

namespace eosr {

trace_sink::trace_sink()
    : level_(trace_level::off), max_bytes_(0), max_rotated_files_(0), meta_(0), current_bytes_(0),
      active_(false), disabled_(false), closed_(false) {}

trace_sink::~trace_sink() {
    close();
}

std::string trace_sink::numbered_path(u32 index) const {
    return run_dir_ + "/trace." + std::to_string(index) + ".jsonl";
}

bool trace_sink::open(const std::string& run_dir, trace_level level, u64 max_bytes,
                      u32 max_rotated_files, trace_meta_source* meta) {
    std::lock_guard<std::mutex> lock(mutex_);
    run_dir_ = run_dir;
    level_ = level;
    max_bytes_ = max_bytes;
    max_rotated_files_ = max_rotated_files;
    meta_ = meta;
    buffer_.clear();
    current_bytes_ = 0;
    rotated_sizes_.clear();
    active_ = false;
    disabled_ = false;
    closed_ = false;

    if (level == trace_level::off) {
        return false; // off creates nothing
    }
    if (!platform::make_directories(run_dir)) {
        return false; // cannot create the run directory: stay inactive rather than fail the game
    }
    active_ = true;
    emit_meta_locked("run_start", std::vector<trace_field>());
    return active_;
}

bool trace_sink::active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ && !disabled_ && !closed_;
}

void trace_sink::disable_locked() {
    disabled_ = true;
    buffer_.clear();
    // One best-effort diagnostic, then silence -- a sink failure must never generate more sink work.
    log_error("trace sink disabled after an I/O failure");
}

void trace_sink::flush_locked() {
    if (buffer_.empty()) {
        return;
    }
    if (!platform::append_file(run_dir_ + "/trace.jsonl", buffer_)) {
        disable_locked();
        return;
    }
    buffer_.clear();
}

void trace_sink::write_line_locked(const std::string& line) {
    const u64 add = static_cast<u64>(line.size()) + 1; // + newline
    if (add > max_bytes_) {
        return; // never write a line larger than a whole file could hold
    }
    if (current_bytes_ + add > max_bytes_) {
        rotate_locked();
        if (disabled_) {
            return;
        }
    }
    buffer_ += line;
    buffer_ += '\n';
    current_bytes_ += add;
}

void trace_sink::emit_meta_locked(const std::string& event, const std::vector<trace_field>& fields) {
    if (meta_ == 0) {
        return;
    }
    const std::string line = serialize_meta(meta_->next_envelope(), event, fields);
    if (!line.empty()) {
        write_line_locked(line);
    }
}

void trace_sink::rotate_locked() {
    flush_locked(); // the current file must hold all its lines before it is renamed
    if (disabled_) {
        return;
    }
    const std::string base = run_dir_ + "/trace.jsonl";
    u32 dropped_files = 0;
    u64 dropped_bytes = 0;

    if (max_rotated_files_ == 0) {
        // No history is kept: the current file is discarded outright.
        dropped_files = 1;
        dropped_bytes = current_bytes_;
        if (!platform::remove_file(base)) {
            disable_locked();
            return;
        }
    } else {
        if (rotated_sizes_.size() >= max_rotated_files_) {
            // At capacity: drop the oldest, which is the highest-numbered file.
            dropped_files = 1;
            dropped_bytes = rotated_sizes_.back();
            if (!platform::remove_file(numbered_path(static_cast<u32>(rotated_sizes_.size())))) {
                disable_locked();
                return;
            }
            rotated_sizes_.pop_back();
        }
        // Shift trace.k -> trace.k+1 from the highest number down, so no rename clobbers another.
        for (u32 k = static_cast<u32>(rotated_sizes_.size()); k >= 1; k--) {
            if (!platform::rename_file(numbered_path(k), numbered_path(k + 1))) {
                disable_locked();
                return;
            }
        }
        if (!platform::rename_file(base, numbered_path(1))) {
            disable_locked();
            return;
        }
        rotated_sizes_.insert(rotated_sizes_.begin(), current_bytes_);
    }

    current_bytes_ = 0;
    buffer_.clear();
    // The rotate record heads the fresh file. current_bytes_ is 0 and the record is small, so this
    // cannot itself re-trigger rotation.
    std::vector<trace_field> fields;
    fields.push_back(make_field(field_id::dropped_files, tv_uint(dropped_files)));
    fields.push_back(make_field(field_id::dropped_bytes, tv_uint(dropped_bytes)));
    emit_meta_locked("rotate", fields);
}

void trace_sink::write(trace_level record_level, const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || disabled_ || closed_) {
        return;
    }
    if (static_cast<int>(record_level) > static_cast<int>(level_)) {
        return; // above the configured verbosity
    }
    if (line.empty()) {
        return; // a serializer that refused a record hands back the empty string
    }
    write_line_locked(line);
    if (record_level == trace_level::errors && !disabled_) {
        flush_locked(); // errors are persisted at once
    }
}

void trace_sink::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || disabled_ || closed_) {
        return;
    }
    flush_locked();
}

void trace_sink::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || !active_) {
        closed_ = true;
        return;
    }
    if (!disabled_) {
        emit_meta_locked("shutdown", std::vector<trace_field>());
        flush_locked();
    }
    closed_ = true;
    active_ = false;
}

} // namespace eosr
