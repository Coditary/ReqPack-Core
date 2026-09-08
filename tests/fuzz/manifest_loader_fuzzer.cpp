#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/manifest/manifest_loader.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0 || size > (1U << 18)) {
        return 0;
    }

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "reqpack-core-fuzz-manifest.lua";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    try {
        (void)ManifestLoader::load(path);
    } catch (const std::exception&) {
        // Invalid fuzz input is expected.
    }

    std::error_code error;
    std::filesystem::remove(path, error);
    return 0;
}
