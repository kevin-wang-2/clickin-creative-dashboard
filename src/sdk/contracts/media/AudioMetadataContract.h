#pragma once
#include "sdk/contracts/builtin/AssetRef.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace clickin {

struct AudioMetadata {
    double   durationSeconds = 0.0;
    int      sampleRate      = 0;
    int      channelCount    = 0;
    int64_t  bitRate         = 0;
    std::string codec;
    std::string container;
};

struct AudioMetadataContract {
    static constexpr std::string_view capability = "media.audio.metadata";
    static constexpr int version = 1;
    using Request = AssetRef;
    using Result  = AudioMetadata;
};

} // namespace clickin
