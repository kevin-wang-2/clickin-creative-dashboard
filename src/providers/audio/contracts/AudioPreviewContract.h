#pragma once
#include "sdk/contracts/builtin/AssetRef.h"
#include <string>
#include <string_view>

namespace clickin {

struct PreviewSessionRef {
    std::string sessionId;
    bool supportsSeek  = true;
    bool supportsPause = true;
    bool supportsLoop  = false;
};

struct AudioPreviewContract {
    static constexpr std::string_view capability = "media.audio.preview";
    static constexpr int version = 1;
    using Request = AssetRef;
    using Result  = PreviewSessionRef;
};

} // namespace clickin
