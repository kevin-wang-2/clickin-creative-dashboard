#include "core/services/AssetService.h"

namespace clickin {

std::vector<AssetRecord> AssetService::listAssets() { return {}; }
AssetRecord AssetService::getAsset(const AssetId&) { return {}; }
AssetId AssetService::createAsset(const std::string&) { return {}; }
void AssetService::deleteAsset(const AssetId&) {}

} // namespace clickin
