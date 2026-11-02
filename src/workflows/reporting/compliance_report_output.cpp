#include "workflows/reporting/compliance_report_output.hpp"

#include "workflows/reporting/file_output_writer.hpp"

#include "output/diagnostic.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace report_output {

namespace {

bool request_has_flag(const Request& request, const std::string& name) {
    return std::find(request.flags.begin(), request.flags.end(), name) != request.flags.end();
}

ReportFileDiagnosticFactory compliance_diagnostic_factory(const std::string& domain) {
    return [domain](const std::string& resolvedPath, const ReportWriteFailure failure) {
        const auto make = [&](const std::string& summary, const std::string& cause, const std::string& recommendation) {
            return make_error_diagnostic(domain, summary, cause, recommendation, {}, domain, "output");
        };

        const std::string label = domain == "sbom" ? "SBOM" : "audit";
        switch (failure) {
        case ReportWriteFailure::CreateDirectory:
            return make("failed to create " + domain + " output directory: " + resolvedPath,
                        "ReqPack could not create parent directory for " + label + " export output.",
                        "Check target path permissions and parent directory state, then retry.");
        case ReportWriteFailure::OpenFile:
            return make("failed to open " + domain + " output path: " + resolvedPath,
                        "ReqPack could not open requested " + label + " output file for writing.",
                        "Check whether path points to writable file location and retry.");
        case ReportWriteFailure::WriteFile:
            return make("failed to write " + domain + " output path: " + resolvedPath,
                        "ReqPack could not finish writing " + label + " export to output file.",
                        "Check disk space, filesystem health, and write permissions, then retry.");
        }
        return make("failed to write " + domain + " output path: " + resolvedPath,
                    "ReqPack could not finish writing " + label + " export to output file.",
                    "Check disk space, filesystem health, and write permissions, then retry.");
    };
}

} // namespace

bool write_compliance_report(const std::string& domain, const std::string& rendered, const Request& request,
                             const std::string& outputPath, const bool interactive) {
    if (outputPath.empty()) {
        std::cout << rendered;
        if (rendered.empty() || rendered.back() != '\n') {
            std::cout << '\n';
        }
        std::cout.flush();
        return true;
    }

    const ReportFileWriteConfig config;
    return write_report_file(rendered, outputPath, request_has_flag(request, "force"), interactive, config,
                             compliance_diagnostic_factory(domain));
}

} // namespace report_output
