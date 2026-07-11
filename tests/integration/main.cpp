#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include <string>
#include <vector>

// The path to the built SDK library under test. Passed as the first positional argument (the
// build system fills in the real path); anything starting with '-' is left for doctest.
std::string g_library_path;

int main(int argc, char** argv) {
    std::vector<char*> doctest_args;
    doctest_args.push_back(argv[0]);
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (g_library_path.empty() && !arg.empty() && arg[0] != '-') {
            g_library_path = arg;
        } else {
            doctest_args.push_back(argv[i]);
        }
    }

    doctest::Context context(static_cast<int>(doctest_args.size()), doctest_args.data());
    return context.run();
}
