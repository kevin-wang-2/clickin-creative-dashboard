#include "core/services/MetadataService.h"
#include "core/db/Database.h"

namespace clickin {

MetadataService::MetadataService(Database& db) : db_(db) {}

std::optional<PluginMetadataRecord>
MetadataService::read(std::string_view pluginId, const ScopeRef& scope,
                      std::string_view ns) const {
    auto stmt = db_.prepare(
        "SELECT data, data_format, schema_version"
        "  FROM asset_plugin_metadata"
        " WHERE plugin_id = ? AND scope_type = ? AND scope_id = ? AND namespace = ?"
        " LIMIT 1;"
    );
    if (!stmt) return std::nullopt;
    stmt->bindText(1, pluginId)
         .bindText(2, scope.scope)
         .bindText(3, scope.scopeId)
         .bindText(4, ns);

    if (!stmt->step()) return std::nullopt;

    PluginMetadataRecord rec;
    rec.data          = stmt->columnBlob(0);
    rec.dataFormat    = stmt->columnText(1);
    rec.schemaVersion = static_cast<int>(stmt->columnInt64(2));
    return rec;
}

void MetadataService::write(std::string_view pluginId, const ScopeRef& scope,
                             std::string_view ns, std::span<const std::byte> data,
                             std::string_view dataFormat, int schemaVersion) {
    auto stmt = db_.prepare(
        "INSERT INTO asset_plugin_metadata"
        "  (id, plugin_id, scope_type, scope_id, namespace,"
        "   data, data_format, schema_version, updated_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))"
        " ON CONFLICT(plugin_id, scope_type, scope_id, namespace) DO UPDATE SET"
        "   data           = excluded.data,"
        "   data_format    = excluded.data_format,"
        "   schema_version = excluded.schema_version,"
        "   updated_at     = excluded.updated_at;"
    );
    if (!stmt) return;
    stmt->bindText (1, Database::generateId())
         .bindText (2, pluginId)
         .bindText (3, scope.scope)
         .bindText (4, scope.scopeId)
         .bindText (5, ns)
         .bindBlob (6, data)
         .bindText (7, dataFormat)
         .bindInt64(8, schemaVersion);
    stmt->step();
}

void MetadataService::remove(std::string_view pluginId, const ScopeRef& scope,
                              std::string_view ns) {
    auto stmt = db_.prepare(
        "DELETE FROM asset_plugin_metadata"
        " WHERE plugin_id = ? AND scope_type = ? AND scope_id = ? AND namespace = ?;"
    );
    if (!stmt) return;
    stmt->bindText(1, pluginId)
         .bindText(2, scope.scope)
         .bindText(3, scope.scopeId)
         .bindText(4, ns);
    stmt->step();
}

} // namespace clickin
