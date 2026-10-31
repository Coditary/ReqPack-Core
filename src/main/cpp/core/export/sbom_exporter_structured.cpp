#include "sbom_exporter_internal.h"

#include "rqp/security/cyclonedx.h"

#include <boost/graph/graph_traits.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace sbom_exporter_internal {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::vector<Graph::vertex_descriptor> ordered_vertices(const Graph& graph) {
    std::vector<Graph::vertex_descriptor> vertices;
    auto [it, end] = boost::vertices(graph);
    for (; it != end; ++it) {
        vertices.push_back(*it);
    }
    return vertices;
}

std::string sbom_component_ref(const Package& package) {
    std::ostringstream stream;
    stream << package.system << ':' << package.name;
    if (!package.version.empty()) {
        stream << '@' << package.version;
    }
    if (package.localTarget && !package.sourcePath.empty()) {
        stream << '#' << package.sourcePath;
    }
    return stream.str();
}

std::string package_display_name(const Package& package) {
    if (package.localTarget && !package.sourcePath.empty()) {
        return std::filesystem::path(package.sourcePath).filename().string();
    }
    return package.name;
}

} // namespace sbom_exporter_internal

#include <rqp/security/json_util.h>

std::string SbomExporter::renderJson(const Graph& graph, const bool cyclonedx) const {
    if (cyclonedx) {
        return rqp_emit_cyclonedx_inventory_json(graph, this->metadataProvider, this->config.sbom.includeDependencyEdges);
    }

    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"packages\": [\n";
    const std::vector<Graph::vertex_descriptor> vertices = sbom_exporter_internal::ordered_vertices(graph);
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const Package& package = graph[vertices[index]];
        stream << "    {\n";
        stream << "      \"system\": \"" << rqp_json_escape(package.system) << "\",\n";
        stream << "      \"name\": \"" << rqp_json_escape(package.name) << "\",\n";
        stream << "      \"displayName\": \"" << rqp_json_escape(sbom_exporter_internal::package_display_name(package))
               << "\",\n";
        stream << "      \"version\": \"" << rqp_json_escape(package.version) << "\",\n";
        stream << "      \"sourcePath\": \"" << rqp_json_escape(package.sourcePath) << "\",\n";
        stream << "      \"localTarget\": " << (package.localTarget ? "true" : "false") << "\n";
        stream << "    }";
        if (index + 1 < vertices.size()) {
            stream << ',';
        }
        stream << '\n';
    }
    stream << "  ]";
    if (this->config.sbom.includeDependencyEdges) {
        stream << ",\n  \"dependencies\": [\n";
        bool firstEdge = true;
        auto [edgeIt, edgeEnd] = boost::edges(graph);
        for (; edgeIt != edgeEnd; ++edgeIt) {
            if (!firstEdge) {
                stream << ",\n";
            }
            firstEdge = false;
            const Package& from = graph[boost::source(*edgeIt, graph)];
            const Package& to = graph[boost::target(*edgeIt, graph)];
            stream << "    {\"from\": \"" << rqp_json_escape(sbom_exporter_internal::sbom_component_ref(from))
                   << "\", \"to\": \"" << rqp_json_escape(sbom_exporter_internal::sbom_component_ref(to)) << "\"}";
        }
        stream << "\n  ]";
    }
    stream << "\n}";
    return stream.str();
}
