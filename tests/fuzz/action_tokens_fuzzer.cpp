#include <cstddef>
#include <cstdint>
#include <string>

#include "core/common/action_tokens.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0 || size > 4096) {
        return 0;
    }

    const std::string token(reinterpret_cast<const char*>(data), size);
    (void)parse_action_token(token);
    return 0;
}
