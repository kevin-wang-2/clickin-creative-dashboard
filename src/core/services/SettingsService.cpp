#include "core/services/SettingsService.h"

namespace clickin {

std::string SettingsService::get(const std::string&, const std::string& def) const { return def; }
void SettingsService::set(const std::string&, const std::string&) {}

} // namespace clickin
