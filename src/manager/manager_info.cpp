#include "manager/manager_info.h"

namespace eosr {
namespace manager {

std::string application_name() {
    return "EOS Reimagined Manager";
}

std::string build_id() {
#if defined(EOSR_MANAGER_BUILD_ID)
    return EOSR_MANAGER_BUILD_ID;
#else
    return "eosr manager (unknown build)";
#endif
}

} // namespace manager
} // namespace eosr
