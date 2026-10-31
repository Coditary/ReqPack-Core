#include "core/export/sbom_exporter.h"

#include "sbom_exporter_internal.h"

#include "output/command_output.h"

#include <algorithm>
#include <string>

SbomExporter::SbomExporter(PluginMetadataProvider* metadataProvider, const ReqPackConfig& config)
    : config(config), metadataProvider(metadataProvider) {}

SbomOutputFormat SbomExporter::resolveFormat(const Request& request) const {
    if (!request.outputFormat.empty()) {
        const auto parsed = sbom_output_format_from_string(request.outputFormat);
        if (parsed.has_value()) {
            return parsed.value();
        }
    }

    if (!request.outputPath.empty()) {
        return SbomOutputFormat::CYCLONEDX_JSON;
    }

    return this->config.sbom.defaultFormat;
}

std::string SbomExporter::resolveOutputPath(const Request& request) const {
    if (!request.outputPath.empty()) {
        return request.outputPath;
    }

    return this->config.sbom.defaultOutputPath;
}

CommandOutput SbomExporter::buildTableCommandOutput(const Graph& graph, const Request& request) const {
    (void)request;
    CommandOutput output;
    output.mode = DisplayMode::SBOM;
    output.sessionItems = {"sbom"};

    const std::vector<Graph::vertex_descriptor> vertices = sbom_exporter_internal::ordered_vertices(graph);
    std::vector<std::vector<std::string>> rows;
    rows.reserve(vertices.size());
    for (const Graph::vertex_descriptor vertex : vertices) {
        const Package& package = graph[vertex];
        rows.push_back({
            package.system,
            sbom_exporter_internal::package_display_name(package),
            package.version.empty() ? "-" : package.version,
            package.sourcePath.empty() ? "-" : package.sourcePath,
        });
    }

    output.blocks.push_back(
        make_command_table_block({"System", "Name", "Version", "Source"}, rows));
    if (rows.empty()) {
        output.blocks.push_back(make_command_message_block("No packages in SBOM"));
    }
    output.success = true;
    output.succeeded = static_cast<int>(rows.size());
    return output;
}

std::string SbomExporter::renderGraph(const Graph& graph, const Request& request) const {
    switch (resolveFormat(request)) {
    case SbomOutputFormat::JSON:
        return renderJson(graph, false);
    case SbomOutputFormat::CYCLONEDX_JSON:
        return renderJson(graph, true);
    case SbomOutputFormat::TABLE:
    default:
        return render_command_output_text(buildTableCommandOutput(graph, request));
    }
}
