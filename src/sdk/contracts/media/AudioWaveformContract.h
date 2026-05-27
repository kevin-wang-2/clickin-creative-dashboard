#pragma once
#include "sdk/contracts/builtin/AssetRef.h"
#include <string_view>
#include <vector>

namespace clickin {

struct StandardWaveform {
    double durationSeconds = 0.0;
    int    channelCount    = 0;
    int    framesPerPeak   = 0;

    // Parallel arrays, each of size (numPeaks * channelCount),
    // stored as contiguous per-channel blocks:
    //   [ch0_peak0, ch0_peak1, ..., ch1_peak0, ch1_peak1, ...]
    std::vector<float> minValues;
    std::vector<float> maxValues;
};

struct AudioWaveformContract {
    static constexpr std::string_view capability = "media.audio.waveform";
    static constexpr int version = 1;
    using Request = AssetRef;
    using Result  = StandardWaveform;
};

} // namespace clickin
