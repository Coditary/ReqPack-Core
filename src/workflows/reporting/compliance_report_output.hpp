#pragma once

#include "platform/common/types.h"

#include <string>

namespace report_output {

bool write_compliance_report(const std::string& domain, const std::string& rendered, const Request& request,
                             const std::string& outputPath, bool interactive);

} // namespace report_output
