#pragma once
#include "sdk/IPlugin.h"

namespace clickin {

class LocalAudioPlugin : public IPlugin {
public:
    PluginManifest manifest() const override;
    void initialize(PluginContext& context) override;
    void shutdown() override;
    std::vector<std::unique_ptr<IRawCapabilityHandler>> createCapabilityHandlers() override;
};

} // namespace clickin
