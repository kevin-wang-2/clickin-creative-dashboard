#pragma once

#include <string>

namespace clickin {

class Database;

class SettingsService {
public:
    explicit SettingsService(Database& db);

    std::string get(const std::string& key, const std::string& defaultValue = {}) const;
    void        set(const std::string& key, const std::string& value);

private:
    Database& db_;
};

} // namespace clickin
