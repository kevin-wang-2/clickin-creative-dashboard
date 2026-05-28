#pragma once

#include "core/app/PluginManager.h"
#include <memory>
#include <string>
#include <vector>

class QWidget;

namespace clickin {

class IPlugin;
struct CoreContext;

class Application {
public:
    Application();
    ~Application();

    // Register a plugin before initialize(). Built-ins are added in main().
    void addPlugin(std::unique_ptr<IPlugin> plugin);

    // Open DB, run migrations, activate plugins. Returns false on critical failure.
    bool initialize(const std::string& dbPath);
    void shutdown();

    CoreContext coreContext();
    std::vector<PluginManager::PluginState> pluginStates() const;

    // Create (and return) the plugin window for the given plugin ID.
    // Returns nullptr if the plugin has no window or is not active.
    QWidget* createPluginWindowFor(const std::string& pluginId, QWidget* parent);

    std::vector<std::string> autoStartWindowPluginIds() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace clickin
