#pragma once

#include "sdk/IPlugin.h"
#include <memory>
#include <string>
#include <vector>

namespace clickin {

class CapabilityRegistry;
struct CoreContext;

class PluginManager {
public:
    explicit PluginManager(CapabilityRegistry& registry);
    ~PluginManager() = default;

    PluginManager(const PluginManager&)            = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // Register a plugin instance before activateAll().
    void addPlugin(std::unique_ptr<IPlugin> plugin);

    // Upsert all plugins into plugin_registry, then activate each enabled one:
    //   initialize() → createCapabilityHandlers() → register with CapabilityRegistry.
    // If initialize() throws, the plugin is marked load_status='failed' and
    // startup continues — never propagates exceptions.
    void activateAll(CoreContext& ctx);

    // Call shutdown() on all active plugins in reverse activation order.
    void shutdownAll();

    struct PluginState {
        std::string              pluginId;
        std::string              name;
        std::string              version;
        bool                     critical      = false;
        bool                     builtin       = true;
        std::string              loadStatus;   // active | failed | disabled
        std::string              failReason;
        std::vector<std::string> dependencies; // from manifest
    };
    std::vector<PluginState> states() const;

private:
    struct Entry {
        std::unique_ptr<IPlugin> plugin;
        std::string loadStatus{"active"};
        std::string failReason;
        bool active = false;
    };

    void upsertRegistry(CoreContext& ctx, const PluginManifest& m,
                        std::string_view loadStatus) const;
    bool isEnabled(CoreContext& ctx, std::string_view pluginId) const;

    CapabilityRegistry& registry_;
    std::vector<Entry>  entries_;
};

} // namespace clickin
