#include "sbom_exporter_internal.h"

#include "core/common/tty_helpers.h"

#include <cstdlib>
#include <string>

namespace sbom_exporter_internal {

bool table_colors_enabled() {
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
    const char* value = std::getenv("FORCE_COLOR");
    if (value != nullptr) {
        const std::string normalized = to_lower_copy(value);
        return normalized.empty() || (normalized != "0" && normalized != "false" && normalized != "no");
    }
    return reqpack_stdout_is_tty();
}

}  // namespace sbom_exporter_internal
