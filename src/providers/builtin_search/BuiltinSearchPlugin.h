#pragma once
#include "sdk/IPlugin.h"

namespace clickin {

class AssetService;

class BuiltinSearchPlugin : public IPlugin {
public:
    PluginManifest manifest() const override;
    void initialize(PluginContext& ctx) override;
    void shutdown() override;
    std::vector<std::unique_ptr<IRawCapabilityHandler>> createCapabilityHandlers() override;

private:
    std::string  pluginId_;
    AssetService* assets_ = nullptr;
};

} // namespace clickin
