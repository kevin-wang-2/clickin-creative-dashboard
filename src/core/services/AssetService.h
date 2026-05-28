#pragma once

#include "sdk/AssetRecord.h"
#include <optional>
#include <string>
#include <vector>

namespace clickin {

class Database;

struct AssetProviderRecord {
    std::string id;
    std::string assetId;
    std::string providerId;
    std::string uri;
};

class AssetService {
public:
    explicit AssetService(Database& db);

    std::vector<AssetRecord> listAssets() const;
    AssetRecord              getAsset(const AssetId& id) const;
    AssetId                  createAsset(const std::string& name,
                                         const std::string& kind = {});
    void                     setAssetKind(const AssetId& id, const std::string& kind);
    void                     deleteAsset(const AssetId& id);

    // Returns the asset ID that owns the given URI, or empty string if none.
    // Stable across the multi-binding refactor (#6) — only the backing table changes.
    AssetId findAssetByUri(const std::string& uri) const;

    // asset_provider table
    std::string                      createAssetProvider(const std::string& assetId,
                                                          const std::string& providerId,
                                                          const std::string& uri);
    std::optional<AssetProviderRecord> getAssetProvider(const std::string& assetId,
                                                         const std::string& providerId) const;
    std::vector<AssetProviderRecord>   listAssetProviders(const std::string& assetId) const;

private:
    Database& db_;
};

} // namespace clickin
