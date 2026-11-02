#include <catch2/catch.hpp>

#include "ecosystem/storage/lmdb_store.hpp"

#include <chrono>
#include <filesystem>
#include <system_error>

namespace {

class TempDir {
  public:
    explicit TempDir(const std::string& prefix)
        : path_(std::filesystem::temp_directory_path() /
                (prefix + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("lmdb store opens environment and reads written values", "[unit][storage][lmdb]") {
    TempDir tempDir {"reqpack-lmdb-store"};
    MDB_env* env = nullptr;
    MDB_dbi dbi = 0;
    std::vector<MDB_dbi> namedDbis;

    REQUIRE(storage::open_lmdb_environment(tempDir.path(), storage::LmdbOpenOptions {.mapSize = 1024 * 1024,
                                                                                     .maxDatabases = 2},
                                           &env, &dbi, {"meta"}, &namedDbis));
    REQUIRE(env != nullptr);
    REQUIRE(namedDbis.size() == 1);

    MDB_txn* transaction = nullptr;
    REQUIRE(mdb_txn_begin(env, nullptr, 0, &transaction) == MDB_SUCCESS);
    REQUIRE(storage::kv::put(transaction, dbi, "pkg:curl", "payload-a"));
    REQUIRE(storage::kv::put(transaction, namedDbis.front(), "schemaVersion", "1"));
    REQUIRE(mdb_txn_commit(transaction) == MDB_SUCCESS);

    REQUIRE(mdb_txn_begin(env, nullptr, MDB_RDONLY, &transaction) == MDB_SUCCESS);
    const std::optional<std::string> payload = storage::kv::get(transaction, dbi, "pkg:curl");
    REQUIRE(payload.has_value());
    CHECK(payload.value() == "payload-a");

    const auto prefixed = storage::kv::scan_prefix(transaction, dbi, "pkg:");
    REQUIRE(prefixed.size() == 1);
    CHECK(prefixed.front().first == "pkg:curl");

    const std::optional<std::string> meta = storage::kv::get(transaction, namedDbis.front(), "schemaVersion");
    REQUIRE(meta.has_value());
    CHECK(meta.value() == "1");
    mdb_txn_abort(transaction);

    mdb_dbi_close(env, namedDbis.front());
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}

TEST_CASE("lmdb store prefix scan stops at first non matching key", "[unit][storage][lmdb]") {
    TempDir tempDir {"reqpack-lmdb-prefix"};
    MDB_env* env = nullptr;
    MDB_dbi dbi = 0;

    REQUIRE(storage::open_lmdb_environment(tempDir.path(), storage::LmdbOpenOptions {.mapSize = 1024 * 1024,
                                                                                     .maxDatabases = 1},
                                           &env, &dbi, {}, nullptr));

    MDB_txn* transaction = nullptr;
    REQUIRE(mdb_txn_begin(env, nullptr, 0, &transaction) == MDB_SUCCESS);
    REQUIRE(storage::kv::put(transaction, dbi, "pkg:a", "1"));
    REQUIRE(storage::kv::put(transaction, dbi, "pkg:b", "2"));
    REQUIRE(storage::kv::put(transaction, dbi, "run:1", "3"));
    REQUIRE(mdb_txn_commit(transaction) == MDB_SUCCESS);

    REQUIRE(mdb_txn_begin(env, nullptr, MDB_RDONLY, &transaction) == MDB_SUCCESS);
    const auto keys = storage::kv::keys_with_prefix(transaction, dbi, "pkg:");
    REQUIRE(keys.size() == 2);
    CHECK(keys[0] == "pkg:a");
    CHECK(keys[1] == "pkg:b");
    mdb_txn_abort(transaction);

    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}
