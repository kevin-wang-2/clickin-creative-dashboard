#pragma once
#include <string>

namespace clickin {

// Identifies a specific provider's view of an asset.
struct AssetRef {
    std::string assetId;
    std::string providerId;
};

} // namespace clickin
