#ifndef EOSR_MANAGER_STATE_STORE_H
#define EOSR_MANAGER_STATE_STORE_H

#include <string>

#include "manager/install_transaction.h"
#include "manager/manager_state.h"

namespace eosr {
namespace manager {

enum class state_load_code { loaded, missing, unreadable, too_large, invalid };

struct manager_index_load_result {
    manager_index_load_result();
    state_load_code code;
    manager_index state;
    std::string sha256;
    std::string detail;
};

struct game_state_load_result {
    game_state_load_result();
    state_load_code code;
    game_state state;
    std::string sha256;
    std::string detail;
};

manager_index_load_result load_manager_index(const std::string& path,
                                             transaction_filesystem& filesystem);
game_state_load_result load_game_state(const std::string& path,
                                       transaction_filesystem& filesystem);

struct state_save_request {
    std::string path;
    std::string temporary_path;
    std::string expected_sha256; // empty only for an expected-new document
};

enum class state_save_code {
    saved,
    invalid_request,
    invalid_state,
    externally_changed,
    temporary_exists,
    write_failed,
    replace_failed,
    commit_uncertain
};

struct state_save_result {
    state_save_result();
    state_save_code code;
    std::string saved_sha256;
    std::string detail;
};

state_save_result save_manager_index(const state_save_request& request,
                                     const manager_index& state,
                                     transaction_filesystem& filesystem);
state_save_result save_game_state(const state_save_request& request, const game_state& state,
                                  transaction_filesystem& filesystem);

} // namespace manager
} // namespace eosr

#endif
