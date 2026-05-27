#pragma once
#include "sdk/contracts/builtin/AssetRef.h"
#include <string>
#include <string_view>

namespace clickin {

struct AssetThumbnailDescriptor {
    enum class Kind {
        None,
        Icon,       // iconKey names a theme icon (e.g. "generic_asset", "audio_file")
        Image,      // uri points to an image file
        CacheRef,   // cacheId references a CacheService entry (e.g. waveform thumbnail)
        TextBadge,  // text is displayed as a label overlay
        ColorBlock  // iconKey is a CSS-style hex color
    };

    Kind        kind    = Kind::None;
    std::string iconKey;
    std::string cacheId;
    std::string uri;
    std::string text;
};

struct AssetThumbnailContract {
    static constexpr std::string_view capability = "builtin.asset.thumbnail";
    static constexpr int version = 1;
    using Request = AssetRef;
    using Result  = AssetThumbnailDescriptor;
};

} // namespace clickin
