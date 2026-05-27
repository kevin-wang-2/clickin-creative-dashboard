#pragma once
#include "core/types/RawPayload.h"
#include <optional>
#include <span>
#include <string>

namespace clickin {

struct ScopeRef {
    std::string scope;   // asset, content_version, provider_binding
    std::string scopeId;
};

struct PluginMetadataRecord {
    std::vector<std::byte> data;
    std::string            dataFormat;
    int                    schemaVersion = 1;
};

class MetadataService {
public:
    std::optional<PluginMetadataRecord>
    read(std::string_view pluginId, const ScopeRef& scope, std::string_view ns);

    void write(std::string_view pluginId, const ScopeRef& scope, std::string_view ns,
               std::span<const std::byte> data, std::string_view dataFormat,
               int schemaVersion);

    void remove(std::string_view pluginId, const ScopeRef& scope, std::string_view ns);
};

} // namespace clickin
