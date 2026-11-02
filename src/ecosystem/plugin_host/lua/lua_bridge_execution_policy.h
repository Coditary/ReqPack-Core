#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ecosystem/plugin_host/api/iplugin.h"

class LuaBridgeExecutionPolicy {
  public:
    static std::optional<std::string> validate(const PluginSecurityMetadata& metadata, const std::string& pluginId,
                                               const std::string& pluginDirectory, const std::string& command,
                                               const std::vector<std::filesystem::path>& runtimeWriteRoots = {});
};
