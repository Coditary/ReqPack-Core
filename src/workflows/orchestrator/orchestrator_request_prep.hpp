#pragma once

#include "platform/common/types.h"
#include "platform/config/configuration.h"
#include "ecosystem/registry/registry.h"
#include "ecosystem/registry/registry_database.h"

#include "output/run_result_json.h"

#include <filesystem>
#include <string>
#include <vector>

namespace orchestrator_request_prep {

void cleanup_temp_files(const std::vector<std::filesystem::path>& tempFiles);

bool prepare_requests_for_run(std::vector<Request>& requests, Registry* registry, const ReqPackConfig& config,
                              std::vector<std::filesystem::path>& tempFiles);

void rewrite_registry_package_requests(std::vector<Request>& requests, const RegistryDatabase* database);
void rewrite_security_gateway_package_requests(std::vector<Request>& requests, Registry* registry,
                                               const ReqPackConfig& config);

bool requests_target_plugin_install(const std::vector<Request>& requests);
bool requests_target_plugin_remove(const std::vector<Request>& requests);

void ensure_request_plugins_discovered(const std::vector<Request>& requests, Registry* registry,
                                       const ReqPackConfig& config);
void expand_update_all_requests(std::vector<Request>& requests, Registry* registry, const ReqPackConfig& config);

std::vector<std::string> collect_request_item_ids(const std::vector<Request>& requests, const Registry& registry);
void display_already_satisfied_session(const std::vector<Request>& requests, const Registry& registry,
                                       ActionType action, int skippedCount);
RunResultJsonDocument build_already_satisfied_json(const std::vector<Request>& requests, const Registry& registry,
                                                   ActionType action, bool dryRun);

} // namespace orchestrator_request_prep
