#include "core/services/CacheService.h"
#include "core/db/Database.h"

namespace clickin {

CacheService::CacheService(Database& db) : db_(db) {}

std::optional<CacheEntry>
CacheService::find(std::string_view pluginId, const ScopeRef& scope,
                   std::string_view cacheType, std::string_view cacheKey,
                   std::string_view cacheVersion) const {
    auto stmt = db_.prepare(
        "SELECT id, uri, size_bytes, status"
        "  FROM asset_plugin_cache"
        " WHERE plugin_id = ? AND scope_type = ? AND scope_id = ?"
        "   AND cache_type = ? AND cache_key = ? AND cache_version = ?"
        " LIMIT 1;"
    );
    if (!stmt) return std::nullopt;
    stmt->bindText(1, pluginId)
         .bindText(2, scope.scope)
         .bindText(3, scope.scopeId)
         .bindText(4, cacheType)
         .bindText(5, cacheKey)
         .bindText(6, cacheVersion);

    if (!stmt->step()) return std::nullopt;

    CacheEntry e;
    e.id        = stmt->columnText(0);
    e.uri       = stmt->columnText(1);
    e.sizeBytes = stmt->columnInt64(2);
    e.status    = stmt->columnText(3);
    return e;
}

CacheEntry CacheService::registerCache(std::string_view pluginId, const ScopeRef& scope,
                                        std::string_view cacheType, std::string_view cacheKey,
                                        std::string_view cacheVersion, std::string_view uri,
                                        int64_t sizeBytes, std::string_view status) {
    std::string id = Database::generateId();
    auto stmt = db_.prepare(
        "INSERT INTO asset_plugin_cache"
        "  (id, plugin_id, scope_type, scope_id, cache_type, cache_key,"
        "   cache_version, uri, size_bytes, status)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(plugin_id, scope_type, scope_id, cache_type, cache_key, cache_version)"
        " DO UPDATE SET"
        "   uri              = excluded.uri,"
        "   size_bytes       = excluded.size_bytes,"
        "   status           = excluded.status,"
        "   last_accessed_at = datetime('now')"
        " RETURNING id, uri, size_bytes, status;"
    );
    if (!stmt) return {id, std::string(uri), sizeBytes, std::string(status)};

    stmt->bindText (1, id)
         .bindText (2, pluginId)
         .bindText (3, scope.scope)
         .bindText (4, scope.scopeId)
         .bindText (5, cacheType)
         .bindText (6, cacheKey)
         .bindText (7, cacheVersion)
         .bindText (8, uri)
         .bindInt64(9, sizeBytes)
         .bindText (10, status);

    if (stmt->step()) {
        CacheEntry e;
        e.id        = stmt->columnText(0);
        e.uri       = stmt->columnText(1);
        e.sizeBytes = stmt->columnInt64(2);
        e.status    = stmt->columnText(3);
        return e;
    }
    return {id, std::string(uri), sizeBytes, std::string(status)};
}

void CacheService::markAccessed(const CacheId& id) {
    auto stmt = db_.prepare(
        "UPDATE asset_plugin_cache SET last_accessed_at = datetime('now') WHERE id = ?;"
    );
    if (!stmt) return;
    stmt->bindText(1, id);
    stmt->step();
}

void CacheService::remove(const CacheId& id) {
    auto stmt = db_.prepare(
        "DELETE FROM asset_plugin_cache WHERE id = ?;"
    );
    if (!stmt) return;
    stmt->bindText(1, id);
    stmt->step();
}

} // namespace clickin
