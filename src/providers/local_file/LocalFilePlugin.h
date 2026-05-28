#pragma once

#include "sdk/IPlugin.h"
#include <string>

namespace clickin {

class AssetService;
class MetadataService;
class HierarchyService;

class LocalFilePlugin : public IPlugin {
public:
    PluginManifest manifest() const override;
    void initialize(PluginContext& ctx) override;
    void shutdown() override;
    std::vector<std::unique_ptr<IRawCapabilityHandler>> createCapabilityHandlers() override;

    bool     hasPluginWindow() const override { return true; }
    QWidget* createPluginWindow(QWidget* parent) override;

private:
    std::string        pluginId_;
    AssetService*      assets_    = nullptr;
    MetadataService*   metadata_  = nullptr;
    HierarchyService*  hierarchy_ = nullptr;
};

} // namespace clickin
