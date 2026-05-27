#include "core/app/Application.h"

namespace clickin {

struct Application::Impl {
    // Phase 2: owns DatabaseService, services, registry, broker, plugin instances.
};

Application::Application() : impl_(std::make_unique<Impl>()) {}
Application::~Application() { shutdown(); }

bool Application::initialize() {
    // Phase 2: run full startup sequence from PRD §6.2.
    return true;
}

void Application::shutdown() {}

} // namespace clickin
