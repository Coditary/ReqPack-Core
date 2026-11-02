#pragma once

#include <boost/property_tree/ptree.hpp>

#include <optional>
#include <stdexcept>
#include <string>

namespace storage::json_codec {

inline std::string required_string(const boost::property_tree::ptree& tree, const std::string& key) {
    const auto value = tree.get_optional<std::string>(key);
    if (!value.has_value() || value->empty()) {
        throw std::runtime_error("json field missing string value: " + key);
    }
    return value.value();
}

} // namespace storage::json_codec
