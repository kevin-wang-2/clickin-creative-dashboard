#include "providers/core_asset/CoreAssetPlugin.h"

namespace clickin {

PluginManifest CoreAssetPlugin::manifest() const {
    return {"builtin.core_asset", "Core Asset", "0.1.0", true, true};
}
void CoreAssetPlugin::initialize(PluginContext&) {}
void CoreAssetPlugin::shutdown() {}
std::vector<std::unique_ptr<IRawCapabilityHandler>> CoreAssetPlugin::createCapabilityHandlers() {
    return {};   // Phase 3c: register builtin.asset.* fallback handlers
}

} // namespace clickin
