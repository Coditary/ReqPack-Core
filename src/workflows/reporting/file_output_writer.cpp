#include "workflows/reporting/file_output_writer.hpp"

#include "output/logger.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace report_output {

bool write_report_file(const std::string& rendered, const std::string& outputPath, const bool force,
                       const bool interactive, const ReportFileWriteConfig& config,
                       const ReportFileDiagnosticFactory& makeDiagnostic) {
    std::filesystem::path filePath(outputPath);
    if (filePath.is_relative()) {
        filePath = std::filesystem::current_path() / filePath;
    }
    const std::string resolvedOutputPath = filePath.string();

    if (std::filesystem::exists(filePath) && !force) {
        if (config.skipWhenExistsWithoutForceInNonInteractiveMode && !interactive) {
            return false;
        }
        Logger& logger = Logger::instance();
        logger.logStdout(resolvedOutputPath + " already exists. Overwrite? [y/N]");
        logger.flushSync();
        std::string answer;
        if (!std::getline(std::cin, answer) || (answer != "y" && answer != "Y")) {
            logger.logStdout("aborted.");
            logger.flushSync();
            return false;
        }
    }

    std::error_code error;
    const std::filesystem::path parentPath = filePath.parent_path();
    if (!parentPath.empty()) {
        std::filesystem::create_directories(parentPath, error);
        if (error) {
            Logger::instance().diagnostic(makeDiagnostic(resolvedOutputPath, ReportWriteFailure::CreateDirectory));
            Logger::instance().flushSync();
            return false;
        }
    }

    std::ofstream output(resolvedOutputPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        Logger::instance().diagnostic(makeDiagnostic(resolvedOutputPath, ReportWriteFailure::OpenFile));
        Logger::instance().flushSync();
        return false;
    }

    output << rendered;
    if (!output.good()) {
        Logger::instance().diagnostic(makeDiagnostic(resolvedOutputPath, ReportWriteFailure::WriteFile));
        Logger::instance().flushSync();
        return false;
    }

    std::cout << resolvedOutputPath << '\n';
    std::cout.flush();
    return true;
}

} // namespace report_output
