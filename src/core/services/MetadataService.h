#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clickin {

class Database;

// Scope conventions for plugin-stored data:
//   "plugin"        — data owned by the plugin itself (e.g. the plugin's own adjacency list);
//                     scopeId = pluginId.
//   "plugin.asset"  — plugin's private data *about* a specific asset (e.g. file path, tags);
//                     scopeId = assetId. Use this instead of "asset" for all plugin writes
//                     so that two plugins can store the same namespace for the same asset
//                     without semantic ambiguity (isolation is already enforced by plugin_id
//                     in the unique key, but "plugin.asset" makes the ownership explicit).
//   Core-reserved scope types ("asset", "content_version", "provider_binding") are for
//   future Core-managed records only; plugins must not use them.
struct ScopeRef {
    std::string scope;
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
