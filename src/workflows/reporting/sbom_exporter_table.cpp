#include "sbom_exporter_internal.h"

#include "workflows/reporting/report_format_common.hpp"

namespace sbom_exporter_internal {

bool table_colors_enabled() {
    return report_format::table_colors_enabled();
}

} // namespace sbom_exporter_internal
