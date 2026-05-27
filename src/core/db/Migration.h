#pragma once
#include <functional>
#include <string>
#include <vector>

namespace clickin {

class Database;

struct Migration {
    int         version;
    std::string description;
    std::string sql;   // DDL to apply
};

class MigrationRunner {
public:
    explicit MigrationRunner(Database& db);

    void addMigration(Migration m);
    bool runPending();   // returns false if any migration fails

private:
    Database&              db_;
    std::vector<Migration> migrations_;
};

} // namespace clickin
