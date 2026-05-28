#include "core/services/HierarchyService.h"
#include "core/db/Database.h"

#include <queue>
#include <unordered_set>

namespace clickin {

HierarchyService::HierarchyService(Database& db) : db_(db) {}

std::vector<AssetRecord> HierarchyService::getNodes(const std::string& pluginId) const {
    auto stmt = db_.prepare(
        "SELECT a.id, a.name, a.kind, a.status"
        " FROM asset_nodes n"
        " JOIN asset a ON a.id = n.asset_id"
        " WHERE n.plugin_id = ? AND a.status != 'deleted'"
        " ORDER BY a.name;"
    );
    if (!stmt) return {};
    stmt->bindText(1, pluginId);

    std::vector<AssetRecord> result;
    while (stmt->step()) {
        AssetRecord r;
        r.id     = stmt->columnText(0);
        r.name   = stmt->columnText(1);
        r.kind   = stmt->columnText(2);
        r.status = stmt->columnText(3);
        result.push_back(std::move(r));
    }
    return result;
}

std::vector<AssetRecord> HierarchyService::getChildren(const std::string& pluginId,
                                                        const std::string& parentId) const {
    auto stmt = db_.prepare(
        "SELECT a.id, a.name, a.kind, a.status"
        " FROM asset_parents p"
        " JOIN asset a ON a.id = p.child_id"
        " WHERE p.plugin_id = ? AND p.parent_id = ? AND a.status != 'deleted'"
        " ORDER BY a.name;"
    );
    if (!stmt) return {};
    stmt->bindText(1, pluginId).bindText(2, parentId);

    std::vector<AssetRecord> result;
    while (stmt->step()) {
        AssetRecord r;
        r.id     = stmt->columnText(0);
        r.name   = stmt->columnText(1);
        r.kind   = stmt->columnText(2);
        r.status = stmt->columnText(3);
        result.push_back(std::move(r));
    }
    return result;
}

bool HierarchyService::hasChildren(const std::string& pluginId,
                                    const std::string& assetId) const {
    auto stmt = db_.prepare(
        "SELECT 1 FROM asset_parents WHERE plugin_id = ? AND parent_id = ? LIMIT 1;"
    );
    if (!stmt) return false;
    stmt->bindText(1, pluginId).bindText(2, assetId);
    return stmt->step();
}

std::vector<AssetRecord> HierarchyService::getAllNodes() const {
    auto stmt = db_.prepare(
        "SELECT DISTINCT a.id, a.name, a.kind, a.status"
        " FROM asset_nodes n"
        " JOIN asset a ON a.id = n.asset_id"
        " WHERE a.status != 'deleted'"
        " ORDER BY a.name;"
    );
    if (!stmt) return {};
    std::vector<AssetRecord> result;
    while (stmt->step()) {
        AssetRecord r;
        r.id     = stmt->columnText(0);
        r.name   = stmt->columnText(1);
        r.kind   = stmt->columnText(2);
        r.status = stmt->columnText(3);
        result.push_back(std::move(r));
    }
    return result;
}

std::vector<AssetRecord> HierarchyService::getAllChildren(const std::string& parentId) const {
    auto stmt = db_.prepare(
        "SELECT DISTINCT a.id, a.name, a.kind, a.status"
        " FROM asset_parents p"
        " JOIN asset a ON a.id = p.child_id"
        " WHERE p.parent_id = ? AND a.status != 'deleted'"
        " ORDER BY a.kind DESC, a.name;"  // folders first (kind="folder" > "audio.*")
    );
    if (!stmt) return {};
    stmt->bindText(1, parentId);
    std::vector<AssetRecord> result;
    while (stmt->step()) {
        AssetRecord r;
        r.id     = stmt->columnText(0);
        r.name   = stmt->columnText(1);
        r.kind   = stmt->columnText(2);
        r.status = stmt->columnText(3);
        result.push_back(std::move(r));
    }
    return result;
}

bool HierarchyService::anyHasChildren(const std::string& assetId) const {
    auto stmt = db_.prepare(
        "SELECT 1 FROM asset_parents WHERE parent_id = ? LIMIT 1;"
    );
    if (!stmt) return false;
    stmt->bindText(1, assetId);
    return stmt->step();
}

void HierarchyService::markNode(const std::string& pluginId, const std::string& assetId) {
    auto stmt = db_.prepare(
        "INSERT OR IGNORE INTO asset_nodes (plugin_id, asset_id) VALUES (?, ?);"
    );
    if (!stmt) return;
    stmt->bindText(1, pluginId).bindText(2, assetId);
    stmt->step();
}

void HierarchyService::unmarkNode(const std::string& pluginId, const std::string& assetId) {
    auto stmt = db_.prepare(
        "DELETE FROM asset_nodes WHERE plugin_id = ? AND asset_id = ?;"
    );
    if (!stmt) return;
    stmt->bindText(1, pluginId).bindText(2, assetId);
    stmt->step();
}

void HierarchyService::addParent(const std::string& pluginId,
                                  const std::string& parentId,
                                  const std::string& childId) {
    auto stmt = db_.prepare(
        "INSERT OR IGNORE INTO asset_parents (plugin_id, parent_id, child_id)"
        " VALUES (?, ?, ?);"
    );
    if (!stmt) return;
    stmt->bindText(1, pluginId).bindText(2, parentId).bindText(3, childId);
    stmt->step();
}

void HierarchyService::removeParent(const std::string& pluginId,
                                     const std::string& parentId,
                                     const std::string& childId) {
    auto stmt = db_.prepare(
        "DELETE FROM asset_parents WHERE plugin_id = ? AND parent_id = ? AND child_id = ?;"
    );
    if (!stmt) return;
    stmt->bindText(1, pluginId).bindText(2, parentId).bindText(3, childId);
    stmt->step();
}

void HierarchyService::removeAllByPlugin(const std::string& pluginId,
                                          const std::string& rootId) {
    if (rootId.empty()) {
        // Wipe everything for this plugin.
        auto s1 = db_.prepare("DELETE FROM asset_parents WHERE plugin_id = ?;");
        if (s1) { s1->bindText(1, pluginId); s1->step(); }
        auto s2 = db_.prepare("DELETE FROM asset_nodes WHERE plugin_id = ?;");
        if (s2) { s2->bindText(1, pluginId); s2->step(); }
        return;
    }

    // BFS from rootId, collect all reachable nodes.
    std::unordered_set<std::string> visited;
    std::queue<std::string> queue;
    queue.push(rootId);

    while (!queue.empty()) {
        std::string cur = queue.front(); queue.pop();
        if (!visited.insert(cur).second) continue;

        auto stmt = db_.prepare(
            "SELECT child_id FROM asset_parents WHERE plugin_id = ? AND parent_id = ?;"
        );
        if (!stmt) continue;
        stmt->bindText(1, pluginId).bindText(2, cur);
        while (stmt->step())
            queue.push(stmt->columnText(0));
    }

    for (const auto& id : visited) {
        auto s1 = db_.prepare(
            "DELETE FROM asset_parents WHERE plugin_id = ? AND (parent_id = ? OR child_id = ?);"
        );
        if (s1) { s1->bindText(1, pluginId).bindText(2, id).bindText(3, id); s1->step(); }
        auto s2 = db_.prepare(
            "DELETE FROM asset_nodes WHERE plugin_id = ? AND asset_id = ?;"
        );
        if (s2) { s2->bindText(1, pluginId).bindText(2, id); s2->step(); }
    }
}

} // namespace clickin
