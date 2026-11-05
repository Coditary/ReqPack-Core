#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

inline std::vector<std::string> fuzz_split_argv_tokens(const uint8_t* data, size_t size) {
    constexpr std::size_t kMaxArgs = 32;
    constexpr std::size_t kMaxArgLen = 4096;

    if (data == nullptr || size == 0) {
        return {"help"};
    }

    const std::string input(reinterpret_cast<const char*>(data), size);
    std::vector<std::string> tokens;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= input.size(); ++index) {
        if (index < input.size() && input[index] != '\0' && input[index] != '\n') {
            continue;
        }
        if (index > start) {
            const std::size_t length = std::min(index - start, kMaxArgLen);
            tokens.push_back(input.substr(start, length));
            if (tokens.size() >= kMaxArgs) {
                break;
            }
        }
        start = index + 1;
    }

    if (tokens.empty()) {
        tokens.push_back("help");
    }
    return tokens;
}

inline std::vector<char*> fuzz_argv_pointers(std::vector<std::string>& storage) {
    std::vector<char*> pointers;
    pointers.reserve(storage.size() + 1);
    pointers.push_back(const_cast<char*>("rqp"));
    for (std::string& token : storage) {
        pointers.push_back(token.data());
    }
    return pointers;
}
