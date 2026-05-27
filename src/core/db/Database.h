#pragma once
#include <string>

namespace clickin {

class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    bool open();
    void close();
    bool isOpen() const;

private:
    std::string path_;
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace clickin
