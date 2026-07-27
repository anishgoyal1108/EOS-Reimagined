#ifndef EOSR_CORE_BOOTSTRAP_DESCRIPTOR_H
#define EOSR_CORE_BOOTSTRAP_DESCRIPTOR_H

#include <string>
#include <vector>

#include "core/config.h"

namespace eosr {

struct bootstrap_descriptor {
    bootstrap_descriptor() : version(0) {}

    int version;
    std::string data_dir;
};

// Parse the complete, deliberately tiny descriptor schema. Failure clears `out` and returns a
// stable human-readable detail in `error`; the caller decides how to surface it.
bool parse_bootstrap_descriptor(const std::string& bytes, bootstrap_descriptor& out,
                                std::string& error);

enum class data_directory_source {
    environment,
    bootstrap,
    platform_default
};

struct data_directory_selection {
    data_directory_selection() : source(data_directory_source::platform_default) {}

    std::string path;
    data_directory_source source;
    std::vector<config_diagnostic> diagnostics;
};

// Resolve EOSR_DATA_DIR -> sibling descriptor -> platform default once during EOS_Initialize.
data_directory_selection select_data_directory();

} // namespace eosr

#endif
