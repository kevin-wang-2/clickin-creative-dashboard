#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clickin {

class Database;

struct ScopeRef {
    std::string scope;    // "asset" | "content_version" | "provider_binding"
    std::string scopeId;
};

struct PluginMetadataRecord {
    std::vector<std::byte> data;
    std::string            dataFormat;
    int                    schemaVersion = 1;
};

class MetadataService {
public:
    explicit MetadataService(Database& db);

    std::optional<PluginMetadataRecord>
    read(std::string_view pluginId, const ScopeRef& scope, std::string_view ns) const;

    // Upsert. Ownership is enforced by the unique key
    // (plugin_id, scope_type, scope_id, namespace) — a plugin can only
    // read back its own records.
    void write(std::string_view pluginId, const ScopeRef& scope, std::string_view ns,
               std::span<const std::byte> data, std::string_view dataFormat,
               int schemaVersion = 1);

    void remove(std::string_view pluginId, const ScopeRef& scope, std::string_view ns);

private:
    Database& db_;
};

} // namespace clickin
