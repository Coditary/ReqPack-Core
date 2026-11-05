#include "ecosystem/plugin_host/api/plugin_bundle.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr const char* kStubReqpackLua = R"(return { apiVersion = 1 }
)";

constexpr const char* kStubRunLua = "return true\n";

void write_stub_file(const std::filesystem::path& path, const char* content) {
    std::ofstream output(path, std::ios::binary);
    output.write(content, static_cast<std::streamsize>(std::strlen(content)));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0 || size > (1U << 18)) {
        return 0;
    }

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "reqpack-core-fuzz-plugin-bundle";
    const std::filesystem::path scriptsDir = root / "scripts";
    std::error_code error;
    std::filesystem::create_directories(scriptsDir, error);

    write_stub_file(root / "reqpack.lua", kStubReqpackLua);
    write_stub_file(root / "run.lua", kStubRunLua);
    write_stub_file(scriptsDir / "install.lua", kStubRunLua);
    write_stub_file(scriptsDir / "remove.lua", kStubRunLua);

    {
        std::ofstream metadata(root / "metadata.json", std::ios::binary);
        metadata.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    (void)plugin_bundle_probe_directory(root);

    std::filesystem::remove_all(root, error);
    return 0;
}
