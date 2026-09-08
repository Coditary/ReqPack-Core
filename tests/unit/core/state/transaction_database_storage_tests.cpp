#include <catch2/catch.hpp>

#include <chrono>
#include <filesystem>
#include <system_error>

#include "core/state/transaction_database.h"

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

ReqPackConfig make_transaction_config(const std::filesystem::path& root) {
    ReqPackConfig config;
    config.execution.transactionDatabasePath = (root / "transactions").string();
    return config;
}

} // namespace

TEST_CASE("transaction database initializes storage and creates active run", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-init"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);

    REQUIRE(database.ensureReady());

    const std::vector<Package> packages{
        Package{.action = ActionType::INSTALL, .system = "dnf", .name = "git"},
        Package{.action = ActionType::INSTALL, .system = "dnf", .name = "curl"},
    };
    const std::string runId = database.createRun(packages, {"--dry-run"});
    REQUIRE_FALSE(runId.empty());

    const std::optional<TransactionRunRecord> activeRun = database.getActiveRun();
    REQUIRE(activeRun.has_value());
    CHECK(activeRun->id == runId);
    CHECK(activeRun->state == "open");
    CHECK(activeRun->flags == std::vector<std::string>{"--dry-run"});

    const std::vector<TransactionItemRecord> items = database.getRunItems(runId);
    REQUIRE(items.size() == 2);
    CHECK(items[0].package.name == "git");
    CHECK(items[1].package.name == "curl");
    CHECK(items[0].status == "planned");
}

TEST_CASE("transaction database updates item status and commits run", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-update"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());

    const Package package{.action = ActionType::INSTALL, .system = "dnf", .name = "ripgrep"};
    const std::string runId = database.createRun({package});
    REQUIRE_FALSE(runId.empty());

    CHECK(database.updateItemStatus(runId, package, "success"));
    const std::vector<TransactionItemRecord> updatedItems = database.getRunItems(runId);
    REQUIRE(updatedItems.size() == 1);
    CHECK(updatedItems.front().status == "success");

    CHECK(database.markRunCommitted(runId));
    CHECK_FALSE(database.getActiveRun().has_value());
}

TEST_CASE("transaction database deletes committed run records", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-delete"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());

    const Package package{.action = ActionType::REMOVE, .system = "apt", .name = "legacy"};
    const std::string runId = database.createRun({package});
    REQUIRE(database.markRunCommitted(runId));
    CHECK(database.deleteRun(runId));
    CHECK(database.getRunItems(runId).empty());
}

TEST_CASE("transaction database batch-updates item statuses", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-batch"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());

    const std::vector<Package> packages{
        Package{.action = ActionType::INSTALL, .system = "dnf", .name = "one"},
        Package{.action = ActionType::INSTALL, .system = "dnf", .name = "two"},
    };
    const std::string runId = database.createRun(packages);
    CHECK(database.updateItemsStatus(runId, packages, "failed", "boom"));

    for (const TransactionItemRecord& item : database.getRunItems(runId)) {
        CHECK(item.status == "failed");
        CHECK(item.errorMessage == "boom");
    }
}

TEST_CASE("transaction database ensureReady is idempotent", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-idempotent"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);

    REQUIRE(database.ensureReady());
    CHECK(database.ensureReady());
}

TEST_CASE("transaction database reports no active run when idle", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-idle"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());
    CHECK_FALSE(database.getActiveRun().has_value());
}

TEST_CASE("transaction database markRunState updates active run", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-state"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());

    const Package package{.action = ActionType::INSTALL, .system = "dnf", .name = "jq"};
    const std::string runId = database.createRun({package});
    REQUIRE(database.markRunState(runId, "rolling-back"));
    const std::optional<TransactionRunRecord> activeRun = database.getActiveRun();
    REQUIRE(activeRun.has_value());
    CHECK(activeRun->state == "rolling-back");
}

TEST_CASE("transaction database loadString and prefixed entries round-trip", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-prefix"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());

    const Package package{.action = ActionType::INSTALL, .system = "dnf", .name = "prefixed"};
    const std::string runId = database.createRun({package});
    REQUIRE_FALSE(runId.empty());

    const std::optional<TransactionRunRecord> activeRun = database.getActiveRun();
    REQUIRE(activeRun.has_value());
    CHECK(activeRun->id == runId);

    const std::vector<TransactionItemRecord> items = database.getRunItems(runId);
    REQUIRE(items.size() == 1);
    CHECK(items.front().package.name == "prefixed");
    CHECK(database.markRunCommitted(runId));
    CHECK(database.deleteRun(runId));
    CHECK(database.getRunItems(runId).empty());
}

TEST_CASE("transaction database deleteRun returns false for unknown run", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-delete-missing"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());
    CHECK_FALSE(database.deleteRun("missing-run-id"));
}

TEST_CASE("transaction database createRun rejects second active run", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-active-conflict"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());

    const Package package{.action = ActionType::INSTALL, .system = "dnf", .name = "first"};
    const std::string firstRunId = database.createRun({package});
    REQUIRE_FALSE(firstRunId.empty());
    CHECK(database.createRun({package}).empty());
    CHECK(database.getActiveRun()->id == firstRunId);
}

TEST_CASE("transaction database markRunState returns false for unknown run", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-mark-missing"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());
    CHECK_FALSE(database.markRunState("missing-run-id", "open"));
}

TEST_CASE("transaction database updateItemStatus returns false for unknown run",
          "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-update-missing"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());

    const Package package{.action = ActionType::INSTALL, .system = "dnf", .name = "ghost"};
    CHECK_FALSE(database.updateItemStatus("missing-run-id", package, "failed"));
}

TEST_CASE("transaction database batch update aborts when item is missing", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-batch-missing-item"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());

    const Package first{.action = ActionType::INSTALL, .system = "dnf", .name = "one"};
    const Package missing{.action = ActionType::INSTALL, .system = "dnf", .name = "missing"};
    const std::string runId = database.createRun({first});
    REQUIRE_FALSE(runId.empty());
    CHECK_FALSE(database.updateItemsStatus(runId, {first, missing}, "failed", "boom"));
}

TEST_CASE("transaction database deleteRun clears active run marker", "[unit][transaction_database][storage]") {
    TempDir tempDir{"reqpack-transaction-db-delete-active"};
    ReqPackConfig config = make_transaction_config(tempDir.path());
    TransactionDatabase database(config);
    REQUIRE(database.ensureReady());

    const Package package{.action = ActionType::INSTALL, .system = "dnf", .name = "tracked"};
    const std::string runId = database.createRun({package});
    REQUIRE_FALSE(runId.empty());
    REQUIRE(database.getActiveRun().has_value());

    CHECK(database.deleteRun(runId));
    CHECK_FALSE(database.getActiveRun().has_value());
    CHECK(database.getRunItems(runId).empty());
}
