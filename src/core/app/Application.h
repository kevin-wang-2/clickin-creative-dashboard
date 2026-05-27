#pragma once

#include <memory>

namespace clickin {

// Owns all core services and orchestrates the MVP startup sequence (PRD §6.2).
class Application {
public:
    Application();
    ~Application();

    bool initialize();  // Returns false if a critical service fails
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace clickin
