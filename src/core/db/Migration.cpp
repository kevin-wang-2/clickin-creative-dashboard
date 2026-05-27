#include "core/db/Migration.h"

namespace clickin {

MigrationRunner::MigrationRunner(Database& db) : db_(db) {}

void MigrationRunner::addMigration(Migration m) {
    migrations_.push_back(std::move(m));
}

bool MigrationRunner::runPending() {
    // Phase 2 will implement schema_migration table tracking and SQL execution.
    return true;
}

} // namespace clickin
