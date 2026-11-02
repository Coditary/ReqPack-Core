#include "workflows/reporting/report_format_common.hpp"

#include "platform/common/tty_helpers.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace report_format {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

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

} // namespace report_format
