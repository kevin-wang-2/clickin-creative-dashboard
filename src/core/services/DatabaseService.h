#pragma once

#include "core/db/Database.h"
#include "core/db/Migration.h"
#include <string>

namespace clickin {

// Owns the Database connection and MigrationRunner.
// Must be initialized before any other service that takes a Database&.
class DatabaseService {
public:
    explicit DatabaseService(std::string dbPath);
    ~DatabaseService() = default;

    DatabaseService(const DatabaseService&)            = delete;
    DatabaseService& operator=(const DatabaseService&) = delete;

    // Callers (plugins) may add migrations before initialize() is called.
    void addMigration(Migration m);

    // Opens the DB file and runs all pending migrations (core + plugin).
    // Returns false on any failure.
    bool initialize();

    Database& db();

private:
    Database        db_;
    MigrationRunner runner_;
};

} // namespace clickin
