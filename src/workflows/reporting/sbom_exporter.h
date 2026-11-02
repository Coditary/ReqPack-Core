#pragma once

#include "platform/common/types.h"
#include "platform/config/configuration.h"
#include "ecosystem/plugin_host/api/plugin_metadata_provider.h"

#include "output/command_output.h"

#include <string>

class SbomExporter {
    ReqPackConfig config;
    PluginMetadataProvider* metadataProvider;

    SbomOutputFormat resolveFormat(const Request& request) const;
    std::string resolveOutputPath(const Request& request) const;
    CommandOutput buildTableCommandOutput(const Graph& graph, const Request& request) const;
    std::string renderJson(const Graph& graph, bool cyclonedx) const;

  public:
    explicit SbomExporter(PluginMetadataProvider* metadataProvider = nullptr,
                          const ReqPackConfig& config = default_reqpack_config());

    std::string renderGraph(const Graph& graph, const Request& request) const;
    bool exportGraph(const Graph& graph, const Request& request) const;
};
