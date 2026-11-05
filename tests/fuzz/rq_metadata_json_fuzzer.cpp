#include "ecosystem/rqp_format/packages/rq_package.h"

#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0 || size > (1U << 18)) {
        return 0;
    }

    const std::string content(reinterpret_cast<const char*>(data), size);
    try {
        (void)rq_parse_metadata_json(content);
    } catch (const std::exception&) {
        // Invalid fuzz input is expected.
    }

    return 0;
}
