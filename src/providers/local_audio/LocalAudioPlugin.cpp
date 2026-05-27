#include "providers/local_audio/LocalAudioPlugin.h"

namespace clickin {

PluginManifest LocalAudioPlugin::manifest() const {
    return {"builtin.local_audio", "Local Audio", "0.1.0", true, false};
}
void LocalAudioPlugin::initialize(PluginContext&) {}
void LocalAudioPlugin::shutdown() {}
std::vector<std::unique_ptr<IRawCapabilityHandler>> LocalAudioPlugin::createCapabilityHandlers() {
    return {};   // Phase 3b: metadata, waveform, preview handlers
}

} // namespace clickin
