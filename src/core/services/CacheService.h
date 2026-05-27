#pragma once
#include "core/types/RawPayload.h"
#include "core/services/MetadataService.h"   // for ScopeRef
#include <optional>
#include <string>

namespace clickin {

struct CacheEntry {
    CacheId     id;
    std::string uri;
    int64_t     sizeBytes = 0;
    std::string status;   // ready, pending, missing
};

class CacheService {
public:
    std::optional<CacheEntry>
    find(const ScopeRef& scope, std::string_view cacheType,
         std::string_view cacheKey, std::string_view cacheVersion);

    CacheEntry registerCache(const ScopeRef& scope, std::string_view cacheType,
                              std::string_view cacheKey, std::string_view cacheVersion,
                              const std::string& uri, int64_t sizeBytes);

    void markAccessed(const CacheId& id);
    void remove(const CacheId& id);
};

} // namespace clickin
