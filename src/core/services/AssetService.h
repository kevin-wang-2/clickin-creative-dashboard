#pragma once

#include <optional>
#include <string>
#include <vector>

namespace clickin {

class Database;

using AssetId = std::string;

struct AssetRecord {
    AssetId     id;
    std::string name;
    std::string status;   // active | missing | deleted
};

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
    AssetId                  createAsset(const std::string& name);
    void                     deleteAsset(const AssetId& id);

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
