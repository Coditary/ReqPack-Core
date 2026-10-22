#include "orchestrator_internal.h"

#include "core/execution/orchestrator.h"
#include "core/host/host_info.h"
#include "core/packages/rq_package.h"
#include "core/query/package_search_query.h"
#include "core/query/search_interactive_picker.h"

#include "output/command_output.h"
#include "output/logger.h"
#include "output/package_info_format.h"
#include "output/package_table_format.h"

#include <chrono>
#include <sstream>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

std::string request_session_label(const Request& request) {
    std::ostringstream label;
    if (!request.system.empty()) {
        label << request.system;
    }
    for (size_t index = 0; index < request.packages.size(); ++index) {
        if (index == 0 && !request.system.empty()) {
            label << ':';
        } else if (index > 0) {
            label << ',';
        }
        label << request.packages[index];
    }
    const std::string rendered = label.str();
    return rendered.empty() ? "all" : rendered;
}

std::string joined_search_prompt(const std::vector<Request>& requests) {
    std::ostringstream prompt;
    for (const Request& request : requests) {
        for (std::size_t index = 0; index < request.packages.size(); ++index) {
            if (prompt.tellp() > 0) {
                prompt << ' ';
            }
            prompt << request.packages[index];
        }
    }
    return prompt.str();
}

bool requests_want_grouped_list(const std::vector<Request>& requests) {
    for (const Request& request : requests) {
        if (orchestrator_internal::request_has_flag(request, "group")) {
            return true;
        }
    }
    return false;
}

bool package_info_has_details(const PackageInfo& item) {
    return !item.packageId.empty() || !item.version.empty() || !item.latestVersion.empty() || !item.status.empty() ||
           !item.installed.empty() || !item.summary.empty() || !item.description.empty() || !item.homepage.empty() ||
           !item.documentation.empty() || !item.sourceUrl.empty() || !item.repository.empty() ||
           !item.channel.empty() || !item.section.empty() || !item.packageType.empty() || !item.architecture.empty() ||
           !item.targetSystems.empty() || !item.license.empty() || !item.author.empty() || !item.maintainer.empty() ||
           !item.email.empty() || !item.publishedAt.empty() || !item.updatedAt.empty() || !item.size.empty() ||
           !item.installedSize.empty() || !item.dependencies.empty() || !item.optionalDependencies.empty() ||
           !item.provides.empty() || !item.conflicts.empty() || !item.replaces.empty() || !item.binaries.empty() ||
           !item.tags.empty() || !item.extraFields.empty();
}

std::string package_table_host_platform_label() {
    const std::shared_ptr<const HostInfoSnapshot> snapshot = HostInfoService::currentSnapshot();
    PackageInfo hostProbe;
    if (snapshot != nullptr) {
        hostProbe.targetSystems = snapshot->platform.osFamily;
    }
    hostProbe.architecture = rq_host_architecture();
    return package_table_platform_label(hostProbe);
}

bool search_requests_show_platform_column(const std::vector<Request>& requests,
                                          const std::vector<PackageInfo>& items) {
    for (const Request& request : requests) {
        if (orchestrator_internal::request_has_flag(request, "all-arch") ||
            orchestrator_internal::request_has_flag(request, "verbose")) {
            return true;
        }
    }
    return !package_table_platform_column_is_redundant(items, package_table_host_platform_label());
}

CommandOutput package_table_output(ActionType action, const std::vector<Request>& requests,
                                   const std::vector<PackageInfo>& items) {
    CommandOutput output;
    output.mode = action == ActionType::LIST       ? DisplayMode::LIST
                  : action == ActionType::SEARCH   ? DisplayMode::SEARCH
                  : action == ActionType::OUTDATED ? DisplayMode::OUTDATED
                                                   : DisplayMode::LIST;
    for (const auto& request : requests) {
        output.sessionItems.push_back(request_session_label(request));
    }
    const bool includeSystem = requests.size() > 1;
    std::vector<std::string> headers;
    if (includeSystem) {
        headers.push_back("System");
    }
    headers.push_back("Name");
    if (action == ActionType::OUTDATED) {
        headers.push_back("Installed");
        headers.push_back("Latest");
        headers.push_back("Type");
        headers.push_back("Platform");
        headers.push_back("Description");
        output.blocks.push_back(
            make_command_table_block(headers, package_outdated_infos_to_rows(items, includeSystem)));
    } else if (action == ActionType::LIST && requests_want_grouped_list(requests)) {
        output.blocks.push_back(make_command_raw_text_block(format_grouped_list_output(items)));
    } else if (action == ActionType::SEARCH || action == ActionType::LIST) {
        headers.push_back("Version");
        headers.push_back("Type");
        const bool showPlatform =
            action == ActionType::LIST ? true : search_requests_show_platform_column(requests, items);
        if (showPlatform) {
            headers.push_back("Platform");
        }
        headers.push_back("Description");
        if (action == ActionType::SEARCH) {
            output.blocks.push_back(
                make_command_table_block(headers, package_search_infos_to_rows(items, includeSystem, showPlatform)));
            output.packageTable.highlightTerms = package_search_terms_from_prompt(joined_search_prompt(requests));
        } else {
            output.blocks.push_back(
                make_command_table_block(headers, package_list_infos_to_rows(items, includeSystem)));
        }
    } else {
        headers.push_back("Version");
        headers.push_back("Summary");
        output.blocks.push_back(make_command_table_block(headers, package_list_infos_to_rows(items, includeSystem)));
    }
    if (items.empty()) {
        output.blocks.push_back(make_command_message_block("No results"));
    }
    output.success = true;
    output.succeeded = static_cast<int>(items.size());
    return output;
}

std::string format_search_result_footer(const std::vector<Request>& requests, const std::size_t itemCount,
                                        const int searchElapsedMs, const int totalElapsedMs) {
    std::ostringstream footer;
    footer << itemCount << (itemCount == 1 ? " package found" : " packages found") << " (search " << searchElapsedMs
           << "ms, total " << totalElapsedMs << "ms)";
    if (!requests.empty() && !requests.front().system.empty()) {
        footer << " • Run 'rqp install " << requests.front().system << " <name>' to install";
    }
    return footer.str();
}

CommandOutput package_search_output(const std::vector<Request>& requests, const std::vector<PackageInfo>& items,
                                    Executer* executor) {
    CommandOutput output = package_table_output(ActionType::SEARCH, requests, items);
    if (items.empty() && executor != nullptr && !requests.empty()) {
        Request listRequest = requests.front();
        listRequest.action = ActionType::LIST;
        listRequest.packages.clear();
        const std::vector<PackageInfo> catalog = executor->list(listRequest);
        if (const std::optional<std::string> suggestion =
                package_search_did_you_mean_suggestion(catalog, joined_search_prompt(requests));
            suggestion.has_value()) {
            output.blocks.push_back(
                make_command_message_block("Did you mean '" + suggestion.value() + "'?"));
        }
    }

    return output;
}

CommandOutput package_info_output(const Request& request, PackageInfo item) {
    CommandOutput output;
    output.mode = DisplayMode::INFO;
    output.sessionItems = {request_session_label(request)};
    if (!package_info_has_details(item)) {
        output.success = false;
        output.failed = 1;
        output.blocks.push_back(make_command_message_block("No package info found"));
        return output;
    }
    if (item.system.empty()) {
        item.system = request.system;
    }
    if (item.name.empty() && !request.packages.empty()) {
        item.name = request.packages.front();
    }
    output.blocks.push_back(make_command_raw_text_block(format_package_info(item)));
    output.success = true;
    output.succeeded = 1;
    return output;
}

std::vector<PackageInfo> collect_list_items(const std::vector<Request>& requests, Executer* executor) {
    std::vector<PackageInfo> items;
    for (const Request& request : requests) {
        auto listed = executor->list(request);
        for (auto& item : listed) {
            if (item.system.empty()) {
                item.system = request.system;
            }
            items.push_back(std::move(item));
        }
    }
    return items;
}

std::vector<PackageInfo> collect_outdated_items(const std::vector<Request>& requests, Executer* executor) {
    std::vector<PackageInfo> items;
    for (const Request& request : requests) {
        auto outdated = executor->outdated(request);
        for (auto& item : outdated) {
            if (item.system.empty()) {
                item.system = request.system;
            }
            items.push_back(std::move(item));
        }
    }
    return items;
}

std::vector<PackageInfo> collect_search_items(const std::vector<Request>& requests, Executer* executor) {
    std::vector<PackageInfo> items;
    for (const Request& request : requests) {
        auto searched = executor->search(request);
        for (auto& item : searched) {
            if (item.system.empty()) {
                item.system = request.system;
            }
            items.push_back(std::move(item));
        }
    }
    return items;
}

bool search_requests_are_interactive(const std::vector<Request>& requests) {
    for (const Request& request : requests) {
        if (orchestrator_internal::request_has_flag(request, "interactive")) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace orchestrator_internal {

int run_query_action(ActionType action, const std::vector<Request>& requests, Executer* executor,
                     const ReqPackConfig& config) {
    if (action == ActionType::LIST) {
        render_command_output(package_table_output(ActionType::LIST, requests, collect_list_items(requests, executor)));
        return 0;
    }

    if (action == ActionType::OUTDATED) {
        render_command_output(
            package_table_output(ActionType::OUTDATED, requests, collect_outdated_items(requests, executor)));
        return 0;
    }

    if (action == ActionType::SEARCH) {
        const auto wallStartedAt = std::chrono::steady_clock::now();
        const auto searchStartedAt = wallStartedAt;
        const std::vector<PackageInfo> items = collect_search_items(requests, executor);
        const int searchElapsedMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - searchStartedAt)
                .count());

        if (search_requests_are_interactive(requests) && isatty(STDIN_FILENO) != 0) {
            if (const std::optional<SearchInteractiveSelection> selection =
                    pick_search_result_interactive(items, requests.front().system, true);
                selection.has_value()) {
                Request installRequest;
                installRequest.action = ActionType::INSTALL;
                installRequest.system = requests.front().system;
                installRequest.packages = {selection->installSpecifier};
                Orchestrator installOrchestrator({installRequest}, config);
                return installOrchestrator.run();
            }
            return 0;
        }

        render_command_output(package_search_output(requests, items, executor));
        const int totalElapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                        std::chrono::steady_clock::now() - wallStartedAt)
                                                        .count());
        Logger::instance().logStdout(
            format_search_result_footer(requests, items.size(), searchElapsedMs, totalElapsedMs));
        Logger::instance().flushSync();
        return 0;
    }

    const PackageInfo item = executor->info(requests.front());
    const CommandOutput output = package_info_output(requests.front(), item);
    render_command_output(output);
    return output.success ? 0 : 1;
}

} // namespace orchestrator_internal
