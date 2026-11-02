#include "ecosystem/registry/registry_database.h"

#include "ecosystem/storage/lmdb_store.hpp"
#include "registry_database_internal.h"

#include <filesystem>

std::optional<RegistryRecord> get_record_from_transaction(MDB_txn* transaction, MDB_dbi database,
                                                          const std::string& name) {
    const std::string normalized = registry_database_to_lower_copy(name);
    const std::optional<std::string> payload = storage::kv::get(transaction, database, normalized);
    if (!payload.has_value()) {
        return std::nullopt;
    }

    return registry_database_deserialize_record(normalized, payload.value());
}

bool put_record_into_transaction(MDB_txn* transaction, MDB_dbi database, const RegistryRecord& record) {
    const std::string payload = registry_database_serialize_record(record);
    const std::string normalizedName = registry_database_to_lower_copy(record.name);
    return storage::kv::put(transaction, database, normalizedName, payload);
}

bool delete_record_from_transaction(MDB_txn* transaction, MDB_dbi database, const std::string& name) {
    const std::string normalizedName = registry_database_to_lower_copy(name);
    return storage::kv::remove(transaction, database, normalizedName, true);
}

std::optional<std::string> get_string_from_transaction(MDB_txn* transaction, MDB_dbi database, const std::string& key) {
    return storage::kv::get(transaction, database, key);
}

bool put_string_into_transaction(MDB_txn* transaction, MDB_dbi database, const std::string& key,
                                 const std::string& value) {
    return storage::kv::put(transaction, database, key, value);
}

bool put_meta_values_into_transaction(MDB_txn* transaction, MDB_dbi database,
                                      const std::map<std::string, std::string>& values) {
    for (const auto& [key, value] : values) {
        if (!put_string_into_transaction(transaction, database, key, value)) {
            return false;
        }
    }
    return true;
}

RegistryDatabase::RegistryDatabase(const ReqPackConfig& config) : config(config) {}

bool RegistryDatabase::ensureReady() const {
    if (!this->initStorage()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->bootstrapped) {
            return true;
        }
    }

    const bool bootstrapped = this->bootstrap_registry();
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        this->bootstrapped = bootstrapped;
    }

    return bootstrapped;
}

bool RegistryDatabase::refreshMainRegistry(bool* changed, bool forceRefresh) const {
    if (changed != nullptr) {
        *changed = false;
    }

    if (!this->initStorage()) {
        return false;
    }

    bool needsBootstrap = false;
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        needsBootstrap = !this->bootstrapped;
    }

    if (needsBootstrap) {
        const bool bootstrapped = this->bootstrap_registry();
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->bootstrapped = bootstrapped;
        }
        if (changed != nullptr) {
            *changed = bootstrapped && is_json_registry_remote(this->config);
        }
        return bootstrapped;
    }

    return this->sync_main_registry(changed, forceRefresh);
}

std::optional<RegistryRecord> RegistryDatabase::getRecord(const std::string& name) const {
    if (!this->ensureReady()) {
        return std::nullopt;
    }

    return this->load_record(name);
}

std::optional<RegistryRecord> RegistryDatabase::resolveRecord(const std::string& name) const {
    std::optional<RegistryRecord> record = this->getRecord(name);
    if (!record.has_value()) {
        return std::nullopt;
    }

    std::size_t guard = 0;
    while (record->alias && !record->source.empty() && guard < 16) {
        record = this->getRecord(record->source);
        if (!record.has_value()) {
            return std::nullopt;
        }
        ++guard;
    }

    return record;
}

std::optional<RegistryRecord> RegistryDatabase::refreshRecord(const std::string& name, bool preferLatestTag) const {
    if (!this->ensureReady()) {
        return std::nullopt;
    }

    std::optional<RegistryRecord> record = this->getRecord(name);
    if (!record.has_value()) {
        return std::nullopt;
    }

    std::size_t guard = 0;
    while (record->alias && !record->source.empty() && guard < 16) {
        record = this->getRecord(record->source);
        if (!record.has_value()) {
            return std::nullopt;
        }
        ++guard;
    }

    const std::optional<RegistryRecord> refreshed =
        refreshed_record_payload(this->config, record.value(), preferLatestTag);
    if (!refreshed.has_value()) {
        return std::nullopt;
    }
    if (!this->put_record(refreshed.value())) {
        return std::nullopt;
    }
    return refreshed;
}

std::vector<RegistryRecord> RegistryDatabase::getAllRecords() const {
    if (!this->ensureReady()) {
        return {};
    }
    return this->load_all_records();
}

bool RegistryDatabase::cacheScript(const std::string& name, const std::string& script) const {
    if (!this->ensureReady()) {
        return false;
    }

    if (!registry_database_is_valid_plugin_script(script)) {
        return false;
    }

    std::optional<RegistryRecord> record = this->getRecord(name);
    if (!record.has_value()) {
        return false;
    }

    record->script = script;
    if (!registry_record_matches_expected_hashes(record.value())) {
        return false;
    }
    return this->put_record(record.value());
}

RegistryDatabase::~RegistryDatabase() {
    std::lock_guard<std::mutex> lock(this->mutex);
    if (this->initialized && this->env != nullptr) {
        if (this->metaDbi != 0) {
            mdb_dbi_close(this->env, this->metaDbi);
            this->metaDbi = 0;
        }
        if (this->dbi != 0) {
            mdb_dbi_close(this->env, this->dbi);
            this->dbi = 0;
        }
        mdb_env_close(this->env);
        this->env = nullptr;
        this->initialized = false;
        this->bootstrapped = false;
    }
}

bool RegistryDatabase::initStorage() const {
    std::lock_guard<std::mutex> lock(this->mutex);
    if (this->initialized) {
        return true;
    }

    const std::filesystem::path dbDirectory = registry_database_directory(this->config.registry.databasePath);
    std::vector<MDB_dbi> namedDbis;
    if (!storage::open_lmdb_environment(
            dbDirectory, storage::LmdbOpenOptions {.mapSize = LMDB_MAP_SIZE, .maxDatabases = LMDB_MAX_DATABASES},
            &this->env, &this->dbi, {REGISTRY_META_DATABASE_NAME}, &namedDbis)) {
        this->env = nullptr;
        this->dbi = 0;
        this->metaDbi = 0;
        return false;
    }

    this->metaDbi = namedDbis.empty() ? 0 : namedDbis.front();
    this->initialized = true;
    return true;
}

std::vector<RegistryRecord> RegistryDatabase::load_all_records() const {
    std::vector<RegistryRecord> records;
    if (!this->initialized) {
        return records;
    }

    std::lock_guard<std::mutex> lock(this->mutex);
    MDB_txn* transaction = nullptr;
    if (mdb_txn_begin(this->env, nullptr, MDB_RDONLY, &transaction) != MDB_SUCCESS) {
        return records;
    }

    for (const auto& [name, payload] : storage::kv::scan_entire(transaction, this->dbi)) {
        if (const std::optional<RegistryRecord> record = registry_database_deserialize_record(name, payload)) {
            records.push_back(record.value());
        }
    }

    mdb_txn_abort(transaction);
    return records;
}

std::optional<std::string> RegistryDatabase::load_meta_value(const std::string& key) const {
    if (key.empty() || !this->initialized) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(this->mutex);
    MDB_txn* transaction = nullptr;
    if (mdb_txn_begin(this->env, nullptr, MDB_RDONLY, &transaction) != MDB_SUCCESS) {
        return std::nullopt;
    }

    const std::optional<std::string> value = get_string_from_transaction(transaction, this->metaDbi, key);
    mdb_txn_abort(transaction);
    return value;
}

std::optional<std::string> RegistryDatabase::getMetaValue(const std::string& key) const {
    if (key.empty() || !this->ensureReady()) {
        return std::nullopt;
    }
    return this->load_meta_value(key);
}

bool RegistryDatabase::putMetaValue(const std::string& key, const std::string& value) const {
    if (key.empty() || !this->ensureReady()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(this->mutex);
    MDB_txn* transaction = nullptr;
    if (mdb_txn_begin(this->env, nullptr, 0, &transaction) != MDB_SUCCESS) {
        return false;
    }

    if (!put_string_into_transaction(transaction, this->metaDbi, key, value)) {
        mdb_txn_abort(transaction);
        return false;
    }

    return mdb_txn_commit(transaction) == MDB_SUCCESS;
}

std::optional<RegistryRecord> RegistryDatabase::load_record(const std::string& name) const {
    if (!this->initialized) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(this->mutex);
    MDB_txn* transaction = nullptr;
    if (mdb_txn_begin(this->env, nullptr, MDB_RDONLY, &transaction) != MDB_SUCCESS) {
        return std::nullopt;
    }

    const std::optional<RegistryRecord> record = get_record_from_transaction(transaction, this->dbi, name);
    mdb_txn_abort(transaction);
    return record;
}

bool RegistryDatabase::put_record(const RegistryRecord& record) const {
    if (!this->initialized) {
        return false;
    }

    std::lock_guard<std::mutex> lock(this->mutex);
    MDB_txn* transaction = nullptr;
    if (mdb_txn_begin(this->env, nullptr, 0, &transaction) != MDB_SUCCESS) {
        return false;
    }

    if (!put_record_into_transaction(transaction, this->dbi, record)) {
        mdb_txn_abort(transaction);
        return false;
    }

    return mdb_txn_commit(transaction) == MDB_SUCCESS;
}
