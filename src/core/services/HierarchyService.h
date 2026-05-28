#pragma once

#include "sdk/AssetRecord.h"
#include <string>
#include <vector>

namespace clickin {

class Database;

class HierarchyService {
public:
    explicit HierarchyService(Database& db);

    // Returns top-level assets contributed by pluginId (those in asset_nodes with no parent).
    std::vector<AssetRecord> getNodes(const std::string& pluginId) const;

    // Returns direct children of parentId contributed by pluginId.
    std::vector<AssetRecord> getChildren(const std::string& pluginId,
                                         const std::string& parentId) const;

    // Returns true if assetId has any children under pluginId.
    bool hasChildren(const std::string& pluginId, const std::string& assetId) const;

    // Cross-plugin queries for the UI navigation layer.
    std::vector<AssetRecord> getAllNodes() const;
    std::vector<AssetRecord> getAllChildren(const std::string& parentId) const;
    bool anyHasChildren(const std::string& assetId) const;

    // Marks assetId as a root node for pluginId (idempotent).
    void markNode(const std::string& pluginId, const std::string& assetId);

    // Removes assetId from the root node list for pluginId.
    void unmarkNode(const std::string& pluginId, const std::string& assetId);

    // Adds a parent→child edge for pluginId (idempotent).
    void addParent(const std::string& pluginId,
                   const std::string& parentId,
                   const std::string& childId);

    // Removes a single parent→child edge for pluginId.
    void removeParent(const std::string& pluginId,
                      const std::string& parentId,
                      const std::string& childId);

    // Removes all hierarchy data contributed by pluginId that is reachable
    // from rootId (BFS), then removes rootId itself. Safe to call with an
    // empty rootId to wipe everything for the plugin.
    void removeAllByPlugin(const std::string& pluginId, const std::string& rootId = {});

private:
    Database& db_;
};

} // namespace clickin
