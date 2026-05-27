#pragma once
#include "sdk/contracts/builtin/AssetRef.h"
#include <string>
#include <string_view>

namespace clickin {

struct AssetKindContract {
    static constexpr std::string_view capability = "builtin.asset.kind";
    static constexpr int version = 1;

    using Request = AssetRef;

    struct Result {
        std::string kind;        // "audio.wav" | "audio.aiff" | "audio" | "unknown"
        int         confidence = 0;
    };
};

} // namespace clickin
