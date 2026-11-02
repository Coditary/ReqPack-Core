#pragma once

#include "ecosystem/state/transaction_database.h"
#include "ecosystem/storage/lmdb_store.hpp"

#include <string_view>

namespace transaction_database_internal {

constexpr unsigned int LMDB_MAX_DATABASES = 1;
constexpr std::size_t LMDB_MAP_SIZE = 8 * 1024 * 1024;
constexpr std::string_view ACTIVE_RUN_KEY = "meta:active_run";

inline bool starts_with(const std::string& value, const std::string_view prefix) {
    return storage::starts_with(value, prefix);
}

std::string now_timestamp();

inline bool load_value(MDB_txn* transaction, MDB_dbi database, const std::string& key, std::string& value) {
    return storage::kv::load(transaction, database, key, value);
}

inline bool put_value(MDB_txn* transaction, MDB_dbi database, const std::string& key, const std::string& value) {
    return storage::kv::put(transaction, database, key, value);
}

inline bool delete_value(MDB_txn* transaction, MDB_dbi database, const std::string& key) {
    return storage::kv::remove(transaction, database, key, false);
}

} // namespace transaction_database_internal
