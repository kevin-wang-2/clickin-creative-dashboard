#pragma once

#include <memory>
#include <string>

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace clickin
