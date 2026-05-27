#include "core/app/Application.h"
#include "core/app/CoreContext.h"

#include <memory>
#include <string>

namespace clickin {

struct Application::Impl {
    std::unique_ptr<DatabaseService>   dbService;
    std::unique_ptr<AssetService>      assets;
    std::unique_ptr<MetadataService>   metadata;
    std::unique_ptr<CacheService>      cache;
    std::unique_ptr<JobService>        jobs;
    std::unique_ptr<SettingsService>   settings;
    std::unique_ptr<CapabilityRegistry> registry;
    std::unique_ptr<CapabilityBroker>   broker;
};

Application::Application() : impl_(std::make_unique<Impl>()) {}
Application::~Application() { shutdown(); }

bool Application::initialize(const std::string& dbPath) {
    impl_->dbService = std::make_unique<DatabaseService>(dbPath);
    if (!impl_->dbService->initialize()) return false;

    Database& db = impl_->dbService->db();
    impl_->assets   = std::make_unique<AssetService>(db);
    impl_->metadata = std::make_unique<MetadataService>(db);
    impl_->cache    = std::make_unique<CacheService>(db);
    impl_->jobs     = std::make_unique<JobService>();
    impl_->settings = std::make_unique<SettingsService>(db);

    impl_->registry = std::make_unique<CapabilityRegistry>();
    impl_->broker   = std::make_unique<CapabilityBroker>(*impl_->registry);

    return true;
}

void Application::shutdown() {
    impl_->broker.reset();
    impl_->registry.reset();
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
