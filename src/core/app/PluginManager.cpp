#include "core/app/PluginManager.h"
#include "core/app/CoreContext.h"
#include "core/capability/CapabilityRegistry.h"
#include "core/db/Database.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace clickin {

PluginManager::PluginManager(CapabilityRegistry& registry)
    : registry_(registry) {}

void PluginManager::addPlugin(std::unique_ptr<IPlugin> plugin) {
    entries_.push_back(Entry{std::move(plugin)});
}

void PluginManager::activateAll(CoreContext& ctx) {
    // Phase 1: upsert all plugins as 'pending'.
    for (auto& e : entries_)
        upsertRegistry(ctx, e.plugin->manifest(), "pending");

    // Phase 2: topological sort via Kahn's algorithm.
    //   Build index: pluginId → position in entries_.
    std::unordered_map<std::string, size_t> indexById;
    for (size_t i = 0; i < entries_.size(); ++i)
        indexById[entries_[i].plugin->manifest().pluginId] = i;

    //   dependents[id] = list of entry indices that depend on 'id'.
    std::unordered_map<std::string, std::vector<size_t>> dependents;
    std::vector<int> inDegree(entries_.size(), 0);

    for (size_t i = 0; i < entries_.size(); ++i) {
        for (const auto& dep : entries_[i].plugin->manifest().dependencies) {
            if (indexById.count(dep)) {
                dependents[dep].push_back(i);
                ++inDegree[i];
            }
            // Unknown deps are caught in Phase 3 when we check each entry.
        }
    }

    std::queue<size_t> q;
    for (size_t i = 0; i < entries_.size(); ++i)
        if (inDegree[i] == 0) q.push(i);

    std::vector<size_t> order;
    order.reserve(entries_.size());
    while (!q.empty()) {
        size_t cur = q.front(); q.pop();
        order.push_back(cur);
        const auto& id = entries_[cur].plugin->manifest().pluginId;
        auto it = dependents.find(id);
        if (it != dependents.end()) {
            for (size_t dep : it->second)
                if (--inDegree[dep] == 0) q.push(dep);
        }
    }

    // Anything with inDegree > 0 after the sort is part of a cycle.
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (inDegree[i] > 0) {
            entries_[i].loadStatus = "failed";
            entries_[i].failReason = "circular dependency detected";
            upsertRegistry(ctx, entries_[i].plugin->manifest(), "failed");
        }
    }

    // Phase 3: activate in dependency order.
    for (size_t idx : order) {
        auto& e        = entries_[idx];
        const auto& m  = e.plugin->manifest();

        if (!isEnabled(ctx, m.pluginId)) {
            e.loadStatus = "disabled";
            upsertRegistry(ctx, m, "disabled");
            continue;
        }

        // Verify all declared dependencies loaded successfully.
        bool depsFailed = false;
        for (const auto& dep : m.dependencies) {
            auto it = indexById.find(dep);
            if (it == indexById.end()) {
                e.loadStatus = "failed";
                e.failReason = "dependency '" + dep + "' not found";
                depsFailed   = true;
                break;
            }
            if (entries_[it->second].loadStatus != "active") {
                e.loadStatus = "failed";
                e.failReason = "dependency '" + dep + "' failed to load";
                depsFailed   = true;
                break;
            }
        }
        if (depsFailed) {
            upsertRegistry(ctx, m, "failed");
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

QWidget* PluginManager::createPluginWindow(const std::string& pluginId, QWidget* parent) const {
    for (const auto& e : entries_) {
        if (e.plugin->manifest().pluginId == pluginId && e.active)
            return e.plugin->createPluginWindow(parent);
    }
    return nullptr;
}

std::vector<std::string> PluginManager::autoStartWindowPluginIds() const {
    std::vector<std::string> ids;
    for (const auto& e : entries_) {
        if (e.active && e.plugin->manifest().autoStartWindow)
            ids.push_back(e.plugin->manifest().pluginId);
    }
    return ids;
}

std::vector<PluginManager::PluginState> PluginManager::states() const {
    std::vector<PluginState> result;
    result.reserve(entries_.size());
    for (const auto& e : entries_) {
        const auto& m = e.plugin->manifest();
        result.push_back({m.pluginId, m.name, m.version,
                          m.critical, m.builtin,
                          m.autoStartWindow, e.plugin->hasPluginWindow(),
                          e.loadStatus, e.failReason,
                          m.dependencies});
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
