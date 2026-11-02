#pragma once

#include <lmdb.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace storage {

struct LmdbOpenOptions {
    std::size_t mapSize;
    unsigned maxDatabases;
    bool createDirectory {true};
};

bool ensure_lmdb_directory(const std::filesystem::path& directory);

bool open_lmdb_environment(const std::filesystem::path& directory, const LmdbOpenOptions& options, MDB_env** env,
                           MDB_dbi* primaryDbi, const std::vector<const char*>& namedDatabases,
                           std::vector<MDB_dbi>* namedDbis);

namespace kv {

std::optional<std::string> get(MDB_txn* transaction, MDB_dbi database, std::string_view key);
bool put(MDB_txn* transaction, MDB_dbi database, std::string_view key, std::string_view value);
bool remove(MDB_txn* transaction, MDB_dbi database, std::string_view key, bool ignoreMissing = false);
bool load(MDB_txn* transaction, MDB_dbi database, const std::string& key, std::string& value);

std::vector<std::pair<std::string, std::string>> scan_entire(MDB_txn* transaction, MDB_dbi database);
std::vector<std::pair<std::string, std::string>> scan_prefix(MDB_txn* transaction, MDB_dbi database,
                                                             std::string_view prefix);
std::vector<std::string> keys_with_prefix(MDB_txn* transaction, MDB_dbi database, std::string_view prefix);

} // namespace kv

bool starts_with(std::string_view value, std::string_view prefix);

} // namespace storage
