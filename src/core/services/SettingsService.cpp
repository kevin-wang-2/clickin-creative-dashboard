#include "core/services/SettingsService.h"
#include "core/db/Database.h"

namespace clickin {

SettingsService::SettingsService(Database& db) : db_(db) {}

std::string SettingsService::get(const std::string& key,
                                  const std::string& defaultValue) const {
    auto stmt = db_.prepare(
        "SELECT value FROM settings WHERE key = ? LIMIT 1;"
    );
    if (!stmt) return defaultValue;
    stmt->bindText(1, key);
    if (!stmt->step()) return defaultValue;
    return stmt->columnText(0);
}

void SettingsService::set(const std::string& key, const std::string& value) {
    auto stmt = db_.prepare(
        "INSERT INTO settings (key, value, updated_at) VALUES (?, ?, datetime('now'))"
        " ON CONFLICT(key) DO UPDATE SET value = excluded.value,"
        "   updated_at = excluded.updated_at;"
    );
    if (!stmt) return;
    stmt->bindText(1, key).bindText(2, value);
    stmt->step();
}

} // namespace clickin
