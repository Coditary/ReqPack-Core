#include "core/common/sha256_digest.h"

#include <openssl/sha.h>

#include <array>
#include <iomanip>
#include <sstream>

std::string reqpack_sha256_hex(const std::string& bytes) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest {};
    SHA256(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), digest.data());

    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (unsigned char value : digest) {
        stream << std::setw(2) << static_cast<int>(value);
    }
    return stream.str();
}
