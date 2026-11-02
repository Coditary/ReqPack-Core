#pragma once

#include "output/diagnostic.h"
#include "output/logger.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>

namespace report_output {

enum class ReportWriteFailure { CreateDirectory, OpenFile, WriteFile };

using ReportFileDiagnosticFactory =
    std::function<DiagnosticMessage(const std::string& resolvedPath, ReportWriteFailure failure)>;

struct ReportFileWriteConfig {
    bool skipWhenExistsWithoutForceInNonInteractiveMode {true};
};

bool write_report_file(const std::string& rendered, const std::string& outputPath, bool force, bool interactive,
                       const ReportFileWriteConfig& config, const ReportFileDiagnosticFactory& makeDiagnostic);

} // namespace report_output
