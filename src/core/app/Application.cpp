#include "core/app/Application.h"
#include "core/app/CoreContext.h"
#include "core/app/PluginManager.h"

#include <memory>
#include <string>
#include <vector>

namespace clickin {

struct Application::Impl {
    // Plugins staged here before initialize() creates PluginManager.
    std::vector<std::unique_ptr<IPlugin>> stagedPlugins;

    std::unique_ptr<DatabaseService>    dbService;
    std::unique_ptr<AssetService>       assets;
    std::unique_ptr<MetadataService>    metadata;
    std::unique_ptr<CacheService>       cache;
    std::unique_ptr<JobService>         jobs;
    std::unique_ptr<SettingsService>    settings;
    std::unique_ptr<CapabilityRegistry> capRegistry;
    std::unique_ptr<CapabilityBroker>   broker;
    std::unique_ptr<PluginManager>      plugins;
};

Application::Application() : impl_(std::make_unique<Impl>()) {}
Application::~Application() { shutdown(); }

void Application::addPlugin(std::unique_ptr<IPlugin> plugin) {
    impl_->stagedPlugins.push_back(std::move(plugin));
}

bool Application::initialize(const std::string& dbPath) {
    // 1. Open DB and run migrations.
    impl_->dbService = std::make_unique<DatabaseService>(dbPath);
    if (!impl_->dbService->initialize()) return false;

    // 2. Construct core services backed by the DB.
    Database& db = impl_->dbService->db();
    impl_->assets   = std::make_unique<AssetService>(db);
    impl_->metadata = std::make_unique<MetadataService>(db);
    impl_->cache    = std::make_unique<CacheService>(db);
    impl_->jobs     = std::make_unique<JobService>();
    impl_->settings = std::make_unique<SettingsService>(db);

    // 3. Capability infrastructure.
    impl_->capRegistry = std::make_unique<CapabilityRegistry>();
    impl_->broker      = std::make_unique<CapabilityBroker>(*impl_->capRegistry);

    // 4. Plugin activation.
    impl_->plugins = std::make_unique<PluginManager>(*impl_->capRegistry);
    for (auto& p : impl_->stagedPlugins)
        impl_->plugins->addPlugin(std::move(p));
    impl_->stagedPlugins.clear();

    auto ctx = coreContext();
    impl_->plugins->activateAll(ctx);

    return true;
}

void Application::shutdown() {
    if (impl_->plugins) impl_->plugins->shutdownAll();

    impl_->plugins.reset();
    impl_->broker.reset();
    impl_->capRegistry.reset();
    impl_->settings.reset();
    impl_->jobs.reset();
    impl_->cache.reset();
    impl_->metadata.reset();
    impl_->assets.reset();
    impl_->dbService.reset();
}

CoreContext Application::coreContext() {
    return CoreContext{
        *impl_->dbService,
        *impl_->assets,
        *impl_->metadata,
        *impl_->cache,
        *impl_->jobs,
        *impl_->settings,
        *impl_->broker
    };
}

} // namespace clickin
