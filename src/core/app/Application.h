#pragma once

#include <memory>
#include <string>

namespace clickin {

struct CoreContext;

class Application {
public:
    Application();
    ~Application();

    bool initialize(const std::string& dbPath);
    void shutdown();

    CoreContext coreContext();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace clickin
