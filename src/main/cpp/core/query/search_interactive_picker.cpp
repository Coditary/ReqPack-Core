#include "core/query/search_interactive_picker.h"

#include <cctype>
#include <iostream>
#include <string>

namespace {

std::string to_type_badge(const std::string& packageType) {
    std::string normalized;
    for (char character : packageType) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    if (normalized.empty() || normalized == "package" || normalized == "binary" || normalized == "app") {
        return "pkg";
    }
    if (normalized == "package-manager" || normalized == "packagemanager") {
        return "pkg-mgr";
    }
    if (normalized == "alias") {
        return "alias";
    }
    return normalized.size() <= 8 ? normalized : normalized.substr(0, 7);
}

std::string basename_name(const std::string& rawName) {
    const std::size_t slash = rawName.find_last_of("/\\");
    if (slash == std::string::npos || slash + 1 >= rawName.size()) {
        return rawName;
    }
    return rawName.substr(slash + 1);
}

std::string install_specifier_for(const PackageInfo& info) {
    const std::string displayName = basename_name(info.name);
    const std::string badge = to_type_badge(info.packageType);
    if (badge == "pkg" || badge.empty()) {
        return displayName;
    }
    return badge + "/" + displayName;
}

std::optional<SearchInteractiveSelection> pick_with_numbered_list(const std::vector<PackageInfo>& results) {
    if (results.empty()) {
        return std::nullopt;
    }

    std::cout << "\nSelect a package (number), or press Enter to cancel:\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const PackageInfo& info = results[index];
        const std::string name = basename_name(info.name);
        const std::string badge = to_type_badge(info.packageType);
        std::cout << "  " << (index + 1) << ") " << name;
        if (!info.version.empty()) {
            std::cout << "  " << info.version;
        }
        if (!badge.empty()) {
            std::cout << "  [" << badge << "]";
        }
        if (!info.summary.empty()) {
            std::cout << "  " << info.summary;
        } else if (!info.description.empty()) {
            std::cout << "  " << info.description;
        }
        std::cout << '\n';
    }
    std::cout << "> ";
    std::cout.flush();

    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::nullopt;
    }
    if (line.empty()) {
        return std::nullopt;
    }

    std::size_t choice = 0;
    try {
        choice = static_cast<std::size_t>(std::stoul(line));
    } catch (...) {
        return std::nullopt;
    }
    if (choice < 1 || choice > results.size()) {
        return std::nullopt;
    }

    const PackageInfo& selected = results[choice - 1];
    SearchInteractiveSelection selection;
    selection.installSpecifier = install_specifier_for(selected);
    selection.label = basename_name(selected.name);
    return selection;
}

}  // namespace

std::optional<SearchInteractiveSelection> pick_search_result_interactive(const std::vector<PackageInfo>& results,
                                                                         const std::string& system,
                                                                         const bool stdinIsInteractive) {
    (void)system;
    if (!stdinIsInteractive || results.empty()) {
        return std::nullopt;
    }

    if (results.size() == 1) {
        SearchInteractiveSelection selection;
        selection.installSpecifier = install_specifier_for(results.front());
        selection.label = basename_name(results.front().name);
        std::cout << "Only one match: " << selection.label << " — press Enter to install, or Ctrl+C to cancel.\n> ";
        std::cout.flush();
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        }
        return selection;
    }

    return pick_with_numbered_list(results);
}
