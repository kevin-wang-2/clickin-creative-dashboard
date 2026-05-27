#include "providers/local_file/LocalFilePlugin.h"

namespace clickin {

PluginManifest LocalFilePlugin::manifest() const {
    return {"builtin.local_file", "Local File", "0.1.0", true, false};
}
void LocalFilePlugin::initialize(PluginContext&) {}
void LocalFilePlugin::shutdown() {}
std::vector<std::unique_ptr<IRawCapabilityHandler>> LocalFilePlugin::createCapabilityHandlers() {
    return {};   // Phase 3a: discovery, locator, name, kind, open_actions
}

} // namespace clickin
