#include "core/services/MetadataService.h"

namespace clickin {

std::optional<PluginMetadataRecord>
MetadataService::read(std::string_view, const ScopeRef&, std::string_view) { return {}; }

void MetadataService::write(std::string_view, const ScopeRef&, std::string_view,
                             std::span<const std::byte>, std::string_view, int) {}

void MetadataService::remove(std::string_view, const ScopeRef&, std::string_view) {}

} // namespace clickin
