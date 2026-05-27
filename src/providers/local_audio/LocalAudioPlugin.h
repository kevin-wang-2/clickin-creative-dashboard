#pragma once
#include "sdk/IPlugin.h"
#include <string>

namespace clickin {

class MetadataService;
class CacheService;

class LocalAudioPlugin : public IPlugin {
public:
    PluginManifest manifest() const override;
    void initialize(PluginContext& ctx) override;
    void shutdown() override;
    std::vector<std::unique_ptr<IRawCapabilityHandler>> createCapabilityHandlers() override;

private:
    std::string      pluginId_;
    MetadataService* metadata_ = nullptr;
    CacheService*    cache_    = nullptr;
};

} // namespace clickin
