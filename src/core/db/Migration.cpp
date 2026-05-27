#include "core/db/Migration.h"
#include "core/db/Database.h"

#include <algorithm>

namespace clickin {

MigrationRunner::MigrationRunner(Database& db) : db_(db) {}

void MigrationRunner::addMigration(Migration m) {
    migrations_.push_back(std::move(m));
}

bool MigrationRunner::runPending() {
    if (!ensureTable()) return false;

    std::sort(migrations_.begin(), migrations_.end(),
              [](const Migration& a, const Migration& b) { return a.version < b.version; });

    for (const auto& m : migrations_) {
        if (isApplied(m.version)) continue;
        if (!apply(m)) return false;
    }
    return true;
}

bool MigrationRunner::ensureTable() {
    return db_.execute(
        "CREATE TABLE IF NOT EXISTS schema_migration ("
        "  version     INTEGER PRIMARY KEY,"
        "  description TEXT    NOT NULL,"
        "  applied_at  TEXT    NOT NULL DEFAULT (datetime('now'))"
        ");"
    );
}

bool MigrationRunner::isApplied(int version) {
    auto stmt = db_.prepare(
        "SELECT 1 FROM schema_migration WHERE version = ? LIMIT 1;"
    );
    if (!stmt) return false;
    stmt->bindInt64(1, version);
    return stmt->step();
}

bool MigrationRunner::apply(const Migration& m) {
    if (!db_.beginTransaction()) return false;

    if (!db_.execute(m.sql)) {
        db_.rollback();
        return false;
    }

    auto stmt = db_.prepare(
        "INSERT INTO schema_migration (version, description) VALUES (?, ?);"
    );
    if (!stmt) { db_.rollback(); return false; }
    stmt->bindInt64(1, m.version).bindText(2, m.description);
    stmt->step();

    return db_.commit();
}

} // namespace clickin
