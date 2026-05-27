#include "core/services/CacheService.h"

namespace clickin {

std::optional<CacheEntry>
CacheService::find(const ScopeRef&, std::string_view, std::string_view, std::string_view) {
    return {};
}
CacheEntry CacheService::registerCache(const ScopeRef&, std::string_view, std::string_view,
                                        std::string_view, const std::string&, int64_t) {
    return {};
}
void CacheService::markAccessed(const CacheId&) {}
void CacheService::remove(const CacheId&) {}

} // namespace clickin
