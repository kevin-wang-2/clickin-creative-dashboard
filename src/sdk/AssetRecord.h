#pragma once
#include <string>

namespace clickin {

using AssetId = std::string;

struct AssetRecord {
    AssetId     id;
    std::string name;
    std::string kind;     // e.g. "audio.wav", "" if unknown
    std::string status;   // active | missing | deleted
};

} // namespace clickin
