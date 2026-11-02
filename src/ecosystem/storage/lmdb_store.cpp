#include "ecosystem/storage/lmdb_store.hpp"

#include <system_error>

namespace storage {

namespace {

MDB_val make_key(std::string_view key) {
    return MDB_val {key.size(), const_cast<char*>(key.data())};
}

MDB_val make_value(std::string_view value) {
    return MDB_val {value.size(), const_cast<char*>(value.data())};
}

} // namespace

bool ensure_lmdb_directory(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return !error;
}

bool open_lmdb_environment(const std::filesystem::path& directory, const LmdbOpenOptions& options, MDB_env** env,
                           MDB_dbi* primaryDbi, const std::vector<const char*>& namedDatabases,
                           std::vector<MDB_dbi>* namedDbis) {
    if (env == nullptr || primaryDbi == nullptr) {
        return false;
    }

    *env = nullptr;
    *primaryDbi = 0;
    if (namedDbis != nullptr) {
        namedDbis->clear();
        namedDbis->resize(namedDatabases.size(), 0);
    }

    if (options.createDirectory && !ensure_lmdb_directory(directory)) {
        return false;
    }

    if (mdb_env_create(env) != MDB_SUCCESS) {
        *env = nullptr;
        return false;
    }

    mdb_env_set_maxdbs(*env, options.maxDatabases);
    mdb_env_set_mapsize(*env, options.mapSize);
    if (mdb_env_open(*env, directory.string().c_str(), 0, 0664) != MDB_SUCCESS) {
        mdb_env_close(*env);
        *env = nullptr;
        return false;
    }

    MDB_txn* transaction = nullptr;
    if (mdb_txn_begin(*env, nullptr, 0, &transaction) != MDB_SUCCESS) {
        mdb_env_close(*env);
        *env = nullptr;
        return false;
    }

    if (mdb_dbi_open(transaction, nullptr, MDB_CREATE, primaryDbi) != MDB_SUCCESS) {
        mdb_txn_abort(transaction);
        mdb_env_close(*env);
        *env = nullptr;
        *primaryDbi = 0;
        return false;
    }

    for (std::size_t index = 0; index < namedDatabases.size(); ++index) {
        MDB_dbi namedDbi = 0;
        if (mdb_dbi_open(transaction, namedDatabases[index], MDB_CREATE, &namedDbi) != MDB_SUCCESS) {
            mdb_txn_abort(transaction);
            mdb_dbi_close(*env, *primaryDbi);
            mdb_env_close(*env);
            *env = nullptr;
            *primaryDbi = 0;
            return false;
        }
        if (namedDbis != nullptr) {
            (*namedDbis)[index] = namedDbi;
        }
    }

    if (mdb_txn_commit(transaction) != MDB_SUCCESS) {
        mdb_dbi_close(*env, *primaryDbi);
        if (namedDbis != nullptr) {
            for (MDB_dbi namedDbi : *namedDbis) {
                if (namedDbi != 0) {
                    mdb_dbi_close(*env, namedDbi);
                }
            }
            namedDbis->clear();
        }
        mdb_env_close(*env);
        *env = nullptr;
        *primaryDbi = 0;
        return false;
    }

    return true;
}

namespace kv {

std::optional<std::string> get(MDB_txn* transaction, MDB_dbi database, const std::string_view key) {
    MDB_val dbKey = make_key(key);
    MDB_val dbValue;
    if (mdb_get(transaction, database, &dbKey, &dbValue) != MDB_SUCCESS) {
        return std::nullopt;
    }
    return std::string(static_cast<const char*>(dbValue.mv_data), dbValue.mv_size);
}

bool put(MDB_txn* transaction, MDB_dbi database, const std::string_view key, const std::string_view value) {
    MDB_val dbKey = make_key(key);
    MDB_val dbValue = make_value(value);
    return mdb_put(transaction, database, &dbKey, &dbValue, 0) == MDB_SUCCESS;
}

bool remove(MDB_txn* transaction, MDB_dbi database, const std::string_view key, const bool ignoreMissing) {
    MDB_val dbKey = make_key(key);
    const int result = mdb_del(transaction, database, &dbKey, nullptr);
    if (ignoreMissing) {
        return result == MDB_SUCCESS || result == MDB_NOTFOUND;
    }
    return result == MDB_SUCCESS;
}

bool load(MDB_txn* transaction, MDB_dbi database, const std::string& key, std::string& value) {
    const std::optional<std::string> loaded = get(transaction, database, key);
    if (!loaded.has_value()) {
        return false;
    }
    value = loaded.value();
    return true;
}

std::vector<std::pair<std::string, std::string>> scan_entire(MDB_txn* transaction, MDB_dbi database) {
    std::vector<std::pair<std::string, std::string>> entries;
    MDB_cursor* cursor = nullptr;
    if (mdb_cursor_open(transaction, database, &cursor) != MDB_SUCCESS) {
        return entries;
    }

    MDB_val key;
    MDB_val value;
    int result = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
    while (result == MDB_SUCCESS) {
        entries.emplace_back(std::string(static_cast<const char*>(key.mv_data), key.mv_size),
                              std::string(static_cast<const char*>(value.mv_data), value.mv_size));
        result = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    return entries;
}

std::vector<std::pair<std::string, std::string>> scan_prefix(MDB_txn* transaction, MDB_dbi database,
                                                             const std::string_view prefix) {
    std::vector<std::pair<std::string, std::string>> entries;
    if (prefix.empty()) {
        return entries;
    }

    MDB_cursor* cursor = nullptr;
    if (mdb_cursor_open(transaction, database, &cursor) != MDB_SUCCESS) {
        return entries;
    }

    MDB_val key {prefix.size(), const_cast<char*>(prefix.data())};
    MDB_val value;
    int result = mdb_cursor_get(cursor, &key, &value, MDB_SET_RANGE);
    while (result == MDB_SUCCESS) {
        const std::string currentKey(static_cast<const char*>(key.mv_data), key.mv_size);
        if (!storage::starts_with(currentKey, prefix)) {
            break;
        }
        entries.emplace_back(currentKey, std::string(static_cast<const char*>(value.mv_data), value.mv_size));
        result = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    return entries;
}

std::vector<std::string> keys_with_prefix(MDB_txn* transaction, MDB_dbi database, const std::string_view prefix) {
    std::vector<std::string> keys;
    for (const auto& [key, _] : scan_prefix(transaction, database, prefix)) {
        keys.push_back(key);
    }
    return keys;
}

} // namespace kv

bool starts_with(const std::string_view value, const std::string_view prefix) {
    return value.rfind(prefix, 0) == 0;
}

} // namespace storage
