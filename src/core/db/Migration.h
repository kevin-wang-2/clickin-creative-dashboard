#pragma once

#include <string>
#include <vector>

namespace clickin {

class Database;

struct Migration {
    int         version;
    std::string description;
    std::string sql;   // DDL executed inside a transaction
};

class MigrationRunner {
public:
    explicit MigrationRunner(Database& db);

    void addMigration(Migration m);

    // Creates schema_migration if absent, then runs any pending migrations
    // in version order inside individual transactions.
    // Returns false and stops on the first failure.
    bool runPending();

private:
    bool ensureTable();
    bool isApplied(int version);
    bool apply(const Migration& m);

    Database&              db_;
    std::vector<Migration> migrations_;
};

} // namespace clickin
