#pragma once
#include <string>

namespace clickin {

class SettingsService {
public:
    std::string get(const std::string& key, const std::string& defaultValue = {}) const;
    void        set(const std::string& key, const std::string& value);
};

} // namespace clickin
