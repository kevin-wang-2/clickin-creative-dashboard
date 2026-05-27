#include "core/services/DatabaseService.h"
#include "core/db/CoreSchema.h"

namespace clickin {

DatabaseService::DatabaseService(std::string dbPath)
    : db_(std::move(dbPath)), runner_(db_) {
    for (auto& m : coreSchemaV1())
        runner_.addMigration(std::move(m));
}

void DatabaseService::addMigration(Migration m) {
    runner_.addMigration(std::move(m));
}

bool DatabaseService::initialize() {
    if (!db_.open()) return false;
    return runner_.runPending();
}

Database& DatabaseService::db() { return db_; }

} // namespace clickin
