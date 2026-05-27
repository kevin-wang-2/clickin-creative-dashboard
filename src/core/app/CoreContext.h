#pragma once

#include "core/services/DatabaseService.h"
#include "core/services/AssetService.h"
#include "core/services/MetadataService.h"
#include "core/services/CacheService.h"
#include "core/services/JobService.h"
#include "core/services/SettingsService.h"
#include "core/capability/CapabilityBroker.h"

namespace clickin {

struct CoreContext {
    DatabaseService&   database;
    AssetService&      assets;
    MetadataService&   metadata;
    CacheService&      cache;
    JobService&        jobs;
    SettingsService&   settings;
    CapabilityBroker&  capabilities;
};

// Scoped context handed to a plugin — bakes in the plugin_id so services
// automatically enforce ownership boundaries.
struct PluginContext {
    std::string  pluginId;
    CoreContext& core;
};

} // namespace clickin
