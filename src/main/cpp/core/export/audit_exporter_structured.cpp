#include "audit_exporter_internal.h"

#include "rqp/security/cyclonedx.h"

#include <rqp/security/json_util.h>

#include <boost/graph/graph_traits.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace audit_exporter_internal {

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string package_component_ref(const Package& package) {
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

} // namespace audit_exporter_internal

namespace {

std::vector<Graph::vertex_descriptor> ordered_vertices(const Graph& graph) {
    std::vector<Graph::vertex_descriptor> vertices;
    auto [it, end] = boost::vertices(graph);
    for (; it != end; ++it) {
        vertices.push_back(*it);
    }
    return vertices;
}

std::string purl_name_for(const Package& package, const PluginSecurityMetadata& metadata) {
    if (package.name.empty()) {
        return {};
    }

    if (metadata.purlType == "maven") {
        const std::size_t separator = package.name.find(':');
        if (separator == std::string::npos || separator == 0 || separator == package.name.size() - 1) {
            return {};
        }
        return package.name.substr(0, separator) + "/" + package.name.substr(separator + 1);
    }

    return package.name;
}

std::string purl_for(const Package& package, PluginMetadataProvider* metadataProvider) {
    if (package.name.empty() || metadataProvider == nullptr) {
        return {};
    }

    const auto metadata = metadataProvider->getPluginSecurityMetadata(package.system);
    if (!metadata.has_value() || metadata->purlType.empty()) {
        return {};
    }

    const std::string purlName = purl_name_for(package, metadata.value());
    if (purlName.empty()) {
        return {};
    }

    std::string purl = "pkg:" + metadata->purlType + "/" + purlName;
    if (!package.version.empty()) {
        purl += '@' + package.version;
    }
    return purl;
}

std::string sarif_level_for(const ValidationFinding& finding) {
    const std::string severity = audit_exporter_internal::to_lower_copy(finding.severity);
    int severityRank = 0;
    if (severity == "critical") {
        severityRank = 4;
    } else if (severity == "high") {
        severityRank = 3;
    } else if (severity == "medium") {
        severityRank = 2;
    } else if (severity == "low") {
        severityRank = 1;
    }
    if (finding.kind == "sync_error") {
        return "error";
    }
    if (severityRank >= 3) {
        return "error";
    }
    if (severityRank >= 1) {
        return "warning";
    }
    return "note";
}

std::string rule_id_for(const ValidationFinding& finding) {
    if (!finding.id.empty()) {
        return finding.id;
    }
    return "reqpack-" + finding.kind;
}

std::string message_for(const ValidationFinding& finding) {
    if (!finding.message.empty()) {
        return finding.message;
    }
    if (!finding.id.empty()) {
        return finding.id;
    }
    return finding.kind;
}

std::string vex_analysis_detail_for(const ValidationFinding& finding) {
    if (finding.kind == "vulnerability") {
        return "Matched by ReqPack audit from local vulnerability data. Reachability and exploitability were not "
               "analyzed.";
    }
    if (finding.kind == "unresolved_version") {
        return "ReqPack could not resolve package version. Vulnerability matching may be incomplete.";
    }
    if (finding.kind == "unsupported_ecosystem") {
        return "ReqPack has no vulnerability ecosystem mapping for this package system.";
    }
    if (finding.kind == "sync_error") {
        return "ReqPack could not fully refresh vulnerability data during audit.";
    }
    return message_for(finding);
}

} // namespace

std::string AuditExporter::renderJson(const Graph& graph, const std::vector<ValidationFinding>& findings) const {
    const std::vector<Graph::vertex_descriptor> vertices = ordered_vertices(graph);

    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"summary\": {\n";
    stream << "    \"packageCount\": " << vertices.size() << ",\n";
    stream << "    \"findingCount\": " << findings.size() << "\n";
    stream << "  },\n";
    stream << "  \"packages\": [\n";

    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const Package& package = graph[vertices[index]];
        stream << "    {\n";
        stream << "      \"system\": \"" << rqp_json_escape(package.system) << "\",\n";
        stream << "      \"name\": \"" << rqp_json_escape(package.name) << "\",\n";
        stream << "      \"displayName\": \"" << rqp_json_escape(audit_exporter_internal::package_display_name(package))
               << "\",\n";
        stream << "      \"version\": \"" << rqp_json_escape(package.version) << "\",\n";
        stream << "      \"sourcePath\": \"" << rqp_json_escape(package.sourcePath) << "\",\n";
        stream << "      \"localTarget\": " << (package.localTarget ? "true" : "false") << ",\n";
        stream << "      \"ref\": \"" << rqp_json_escape(audit_exporter_internal::package_component_ref(package)) << "\"\n";
        stream << "    }";
        if (index + 1 < vertices.size()) {
            stream << ',';
        }
        stream << '\n';
    }
    stream << "  ],\n";
    stream << "  \"findings\": [\n";
    for (std::size_t index = 0; index < findings.size(); ++index) {
        const ValidationFinding& finding = findings[index];
        stream << "    {\n";
        stream << "      \"id\": \"" << rqp_json_escape(finding.id) << "\",\n";
        stream << "      \"kind\": \"" << rqp_json_escape(finding.kind) << "\",\n";
        stream << "      \"source\": \"" << rqp_json_escape(finding.source) << "\",\n";
        stream << "      \"severity\": \"" << rqp_json_escape(finding.severity) << "\",\n";
        stream << "      \"score\": " << finding.score << ",\n";
        stream << "      \"message\": \"" << rqp_json_escape(message_for(finding)) << "\",\n";
        stream << "      \"package\": {\n";
        stream << "        \"system\": \"" << rqp_json_escape(finding.package.system) << "\",\n";
        stream << "        \"name\": \"" << rqp_json_escape(finding.package.name) << "\",\n";
        stream << "        \"version\": \"" << rqp_json_escape(finding.package.version) << "\"\n";
        stream << "      }\n";
        stream << "    }";
        if (index + 1 < findings.size()) {
            stream << ',';
        }
        stream << '\n';
    }
    stream << "  ]\n";
    stream << "}";
    return stream.str();
}

std::string AuditExporter::renderCycloneDxVex(const Graph& graph,
                                              const std::vector<ValidationFinding>& findings) const {
    return rqp_emit_cyclonedx_audit_vex_json(graph, findings, this->metadataProvider,
                                             this->config.sbom.includeDependencyEdges);
}

std::string AuditExporter::renderSarif(const Graph& graph, const std::vector<ValidationFinding>& findings) const {
    (void)graph;

    std::vector<std::string> orderedRuleIds;
    std::map<std::string, ValidationFinding> representativeFindings;
    for (const ValidationFinding& finding : findings) {
        const std::string ruleId = rule_id_for(finding);
        if (!representativeFindings.contains(ruleId)) {
            representativeFindings.emplace(ruleId, finding);
            orderedRuleIds.push_back(ruleId);
        }
    }

    std::ostringstream stream;
    stream << "{\n";
    stream << "  \"version\": \"2.1.0\",\n";
    stream << "  \"$schema\": \"https://json.schemastore.org/sarif-2.1.0.json\",\n";
    stream << "  \"runs\": [\n";
    stream << "    {\n";
    stream << "      \"tool\": {\n";
    stream << "        \"driver\": {\n";
    stream << "          \"name\": \"ReqPack\",\n";
    stream << "          \"rules\": [\n";
    for (std::size_t index = 0; index < orderedRuleIds.size(); ++index) {
        const ValidationFinding& finding = representativeFindings.at(orderedRuleIds[index]);
        stream << "            {\n";
        stream << "              \"id\": \"" << rqp_json_escape(orderedRuleIds[index]) << "\",\n";
        stream << "              \"shortDescription\": {\"text\": \"" << rqp_json_escape(message_for(finding)) << "\"},\n";
        stream << "              \"properties\": {\"kind\": \"" << rqp_json_escape(finding.kind) << "\"}\n";
        stream << "            }";
        if (index + 1 < orderedRuleIds.size()) {
            stream << ',';
        }
        stream << '\n';
    }
    stream << "          ]\n";
    stream << "        }\n";
    stream << "      },\n";
    stream << "      \"results\": [\n";
    for (std::size_t index = 0; index < findings.size(); ++index) {
        const ValidationFinding& finding = findings[index];
        stream << "        {\n";
        stream << "          \"ruleId\": \"" << rqp_json_escape(rule_id_for(finding)) << "\",\n";
        stream << "          \"level\": \"" << rqp_json_escape(sarif_level_for(finding)) << "\",\n";
        stream << "          \"message\": {\"text\": \"" << rqp_json_escape(message_for(finding)) << "\"},\n";
        stream << "          \"locations\": [{\n";
        stream << "            \"physicalLocation\": {\n";
        stream << "              \"artifactLocation\": {\"uri\": \"pkg:"
               << rqp_json_escape(audit_exporter_internal::package_component_ref(finding.package)) << "\"}\n";
        stream << "            }\n";
        stream << "          }],\n";
        stream << "          \"properties\": {\n";
        stream << "            \"system\": \"" << rqp_json_escape(finding.package.system) << "\",\n";
        stream << "            \"package\": \"" << rqp_json_escape(finding.package.name) << "\",\n";
        stream << "            \"version\": \"" << rqp_json_escape(finding.package.version) << "\",\n";
        stream << "            \"kind\": \"" << rqp_json_escape(finding.kind) << "\",\n";
        stream << "            \"score\": " << finding.score << "\n";
        stream << "          }\n";
        stream << "        }";
        if (index + 1 < findings.size()) {
            stream << ',';
        }
        stream << '\n';
    }
    stream << "      ]\n";
    stream << "    }\n";
    stream << "  ]\n";
    stream << "}";
    return stream.str();
}
