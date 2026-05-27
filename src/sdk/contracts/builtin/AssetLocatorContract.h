#pragma once
#include "sdk/contracts/builtin/AssetRef.h"
#include <string>
#include <string_view>

namespace clickin {

struct AssetLocatorContract {
    static constexpr std::string_view capability = "builtin.provide_locator";
    static constexpr int version = 1;

    using Request = AssetRef;

    struct Result {
        std::string scheme;       // "file"
        std::string uri;          // "file:///absolute/path/to/file.wav"
        bool        local    = true;
        bool        seekable = true;
    };
};

} // namespace clickin
