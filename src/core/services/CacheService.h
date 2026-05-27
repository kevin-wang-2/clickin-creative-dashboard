#pragma once

#include "core/services/MetadataService.h"   // ScopeRef
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace clickin {

class Database;

using CacheId = std::string;

// Stores only cache entry metadata (id, uri, size, status).
// Actual cache content lives on disk at the uri path.
struct CacheEntry {
    CacheId     id;
    std::string uri;
    int64_t     sizeBytes = 0;
    std::string status;   // ready | pending | missing
};

class CacheService {
public:
    explicit CacheService(Database& db);

    std::optional<CacheEntry>
    find(std::string_view pluginId, const ScopeRef& scope,
         std::string_view cacheType, std::string_view cacheKey,
         std::string_view cacheVersion = "") const;

    CacheEntry registerCache(std::string_view pluginId, const ScopeRef& scope,
                              std::string_view cacheType, std::string_view cacheKey,
                              std::string_view cacheVersion, std::string_view uri,
                              int64_t sizeBytes, std::string_view status = "ready");

    void markAccessed(const CacheId& id);
    void remove(const CacheId& id);

private:
    Database& db_;
};

} // namespace clickin
