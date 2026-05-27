#include "core/app/PluginManager.h"
#include "core/app/CoreContext.h"
#include "core/capability/CapabilityRegistry.h"
#include "core/db/Database.h"

#include <algorithm>

namespace clickin {

PluginManager::PluginManager(CapabilityRegistry& registry)
    : registry_(registry) {}

void PluginManager::addPlugin(std::unique_ptr<IPlugin> plugin) {
    entries_.push_back(Entry{std::move(plugin)});
}

void PluginManager::activateAll(CoreContext& ctx) {
    // Phase 1: upsert every discovered plugin into plugin_registry as 'pending'.
    for (auto& e : entries_) {
        upsertRegistry(ctx, e.plugin->manifest(), "pending");
    }

    // Phase 2: activate enabled plugins.
    for (auto& e : entries_) {
        const auto& m = e.plugin->manifest();
        if (!isEnabled(ctx, m.pluginId)) {
            e.loadStatus = "disabled";
            upsertRegistry(ctx, m, "disabled");
            continue;
        }

        try {
            PluginContext pluginCtx{m.pluginId, ctx};
            e.plugin->initialize(pluginCtx);

            auto handlers = e.plugin->createCapabilityHandlers();
            for (auto& h : handlers)
                registry_.registerHandler(std::move(h));

            e.loadStatus = "active";
            e.active     = true;
        } catch (const std::exception& ex) {
            e.loadStatus = "failed";
            e.failReason = ex.what();
        } catch (...) {
            e.loadStatus = "failed";
            e.failReason = "unknown exception";
        }

        upsertRegistry(ctx, m, e.loadStatus);
    }
}

void PluginManager::shutdownAll() {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (!it->active) continue;
        try {
            it->plugin->shutdown();
        } catch (...) {
            // Shutdown must not propagate.
        }
        it->active = false;
    }
}

std::vector<PluginManager::PluginState> PluginManager::states() const {
    std::vector<PluginState> result;
    result.reserve(entries_.size());
    for (const auto& e : entries_) {
        result.push_back({e.plugin->manifest().pluginId, e.loadStatus, e.failReason});
    }
    return result;
}

// ── private helpers ───────────────────────────────────────────────────────────

void PluginManager::upsertRegistry(CoreContext& ctx, const PluginManifest& m,
                                    std::string_view loadStatus) const {
    auto stmt = ctx.database.db().prepare(
        "INSERT INTO plugin_registry (plugin_id, enabled, load_status)"
        " VALUES (?, 1, ?)"
        " ON CONFLICT(plugin_id) DO UPDATE SET"
        "   load_status = excluded.load_status;"
    );
    if (!stmt) return;
    stmt->bindText(1, m.pluginId).bindText(2, loadStatus);
    stmt->step();
}

bool PluginManager::isEnabled(CoreContext& ctx, std::string_view pluginId) const {
    auto stmt = ctx.database.db().prepare(
        "SELECT enabled FROM plugin_registry WHERE plugin_id = ? LIMIT 1;"
    );
    if (!stmt) return true;  // not yet in registry → enabled by default
    stmt->bindText(1, pluginId);
    if (!stmt->step()) return true;  // new plugin → enabled by default
    return stmt->columnInt64(0) != 0;
}

} // namespace clickin
