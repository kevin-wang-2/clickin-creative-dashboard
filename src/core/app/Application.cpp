#include "core/app/Application.h"
#include "core/app/CoreContext.h"
#include "core/app/PluginManager.h"
#include "core/services/HierarchyService.h"
#include "core/worker/WorkerPool.h"

#include <memory>
#include <string>
#include <vector>

namespace clickin {

struct Application::Impl {
    std::vector<std::unique_ptr<IPlugin>> stagedPlugins;

    std::unique_ptr<DatabaseService>    dbService;
    std::unique_ptr<AssetService>       assets;
    std::unique_ptr<MetadataService>    metadata;
    std::unique_ptr<CacheService>       cache;
    std::unique_ptr<JobService>         jobs;
    std::unique_ptr<SettingsService>    settings;
    std::unique_ptr<HierarchyService>   hierarchy;
    std::unique_ptr<WorkerPool>         workerPool;
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
    impl_->assets     = std::make_unique<AssetService>(db);
    impl_->metadata   = std::make_unique<MetadataService>(db);
    impl_->cache      = std::make_unique<CacheService>(db);
    impl_->jobs       = std::make_unique<JobService>();
    impl_->settings   = std::make_unique<SettingsService>(db);
    impl_->hierarchy  = std::make_unique<HierarchyService>(db);

    // 3. WorkerPool for async capability dispatch.
    impl_->workerPool = std::make_unique<WorkerPool>();

    // 4. Capability infrastructure.
    impl_->capRegistry = std::make_unique<CapabilityRegistry>();
    impl_->broker      = std::make_unique<CapabilityBroker>(*impl_->capRegistry);
    impl_->broker->setServices({
        .workerPool = impl_->workerPool.get(),
        .metadata   = impl_->metadata.get(),
        .cache      = impl_->cache.get(),
        .assets     = impl_->assets.get(),
        .settings   = impl_->settings.get()
    });

    // 5. Plugin activation.
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

    // Drain the WorkerPool BEFORE destroying handlers or services.
    // WorkerPool's destructor joins all threads, ensuring no tasks reference
    // destroyed objects.
    impl_->workerPool.reset();

    impl_->broker.reset();
    impl_->capRegistry.reset();
    impl_->settings.reset();
    impl_->hierarchy.reset();
    impl_->jobs.reset();
    impl_->cache.reset();
    impl_->metadata.reset();
    impl_->assets.reset();
    impl_->dbService.reset();
}

std::vector<PluginManager::PluginState> Application::pluginStates() const {
    if (!impl_->plugins) return {};
    return impl_->plugins->states();
}

QWidget* Application::createPluginWindowFor(const std::string& pluginId, QWidget* parent) {
    if (!impl_->plugins) return nullptr;
    return impl_->plugins->createPluginWindow(pluginId, parent);
}

std::vector<std::string> Application::autoStartWindowPluginIds() const {
    if (!impl_->plugins) return {};
    return impl_->plugins->autoStartWindowPluginIds();
}

CoreContext Application::coreContext() {
    return CoreContext{
        *impl_->dbService,
        *impl_->assets,
        *impl_->metadata,
        *impl_->cache,
        *impl_->jobs,
        *impl_->settings,
        *impl_->hierarchy,
        *impl_->broker
    };
}

} // namespace clickin
