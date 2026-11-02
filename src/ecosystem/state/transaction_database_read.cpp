#include "ecosystem/state/transaction_database_store.h"
#include "transaction_database_internal.h"

#include "ecosystem/storage/lmdb_store.hpp"

#include <algorithm>
#include <filesystem>

bool TransactionDatabase::initStorage() const {
    std::lock_guard<std::mutex> lock(this->mutex);
    if (this->initialized) {
        return true;
    }

    const std::filesystem::path dbDirectory(this->config.execution.transactionDatabasePath);
    if (!storage::open_lmdb_environment(
            dbDirectory,
            storage::LmdbOpenOptions {.mapSize = transaction_database_internal::LMDB_MAP_SIZE,
                                      .maxDatabases = transaction_database_internal::LMDB_MAX_DATABASES},
            &this->env, &this->dbi, {}, nullptr)) {
        this->env = nullptr;
        this->dbi = 0;
        return false;
    }

    this->initialized = true;
    return true;
}

std::optional<std::string> TransactionDatabase::loadString(const std::string& key) const {
    if (!this->ensureReady()) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(this->mutex);
    MDB_txn* transaction = nullptr;
    if (mdb_txn_begin(this->env, nullptr, MDB_RDONLY, &transaction) != MDB_SUCCESS) {
        return std::nullopt;
    }

    std::string value;
    const bool loaded = transaction_database_internal::load_value(transaction, this->dbi, key, value);
    mdb_txn_abort(transaction);
    if (!loaded) {
        return std::nullopt;
    }
    return value;
}

std::vector<std::pair<std::string, std::string>>
TransactionDatabase::loadPrefixedEntries(const std::string& prefix) const {
    std::vector<std::pair<std::string, std::string>> entries;
    if (!this->ensureReady()) {
        return entries;
    }

    std::lock_guard<std::mutex> lock(this->mutex);
    MDB_txn* transaction = nullptr;
    if (mdb_txn_begin(this->env, nullptr, MDB_RDONLY, &transaction) != MDB_SUCCESS) {
        return entries;
    }

    entries = storage::kv::scan_prefix(transaction, this->dbi, prefix);
    mdb_txn_abort(transaction);
    return entries;
}

std::optional<TransactionRunRecord> TransactionDatabase::getActiveRun() const {
    const std::optional<std::string> activeRunId =
        this->loadString(std::string(transaction_database_internal::ACTIVE_RUN_KEY));
    if (!activeRunId.has_value() || activeRunId->empty()) {
        return std::nullopt;
    }

    const std::optional<std::string> payload = this->loadString(transaction_database_run_key(activeRunId.value()));
    if (!payload.has_value()) {
        return std::nullopt;
    }

    return transaction_database_deserialize_run(activeRunId.value(), payload.value());
}

std::vector<TransactionItemRecord> TransactionDatabase::getRunItems(const std::string& runId) const {
    std::vector<TransactionItemRecord> items;
    for (const auto& [key, value] : this->loadPrefixedEntries(transaction_database_item_prefix(runId))) {
        if (const auto item = transaction_database_deserialize_item(key, value)) {
            items.push_back(item.value());
        }
    }

    std::sort(items.begin(), items.end(), [](const TransactionItemRecord& left, const TransactionItemRecord& right) {
        return left.sequence < right.sequence;
    });
    return items;
}
