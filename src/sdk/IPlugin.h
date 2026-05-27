#pragma once

#include <memory>
#include <string>
#include <vector>

namespace clickin {

class IRawCapabilityHandler;
class PluginContext;

struct PluginManifest {
    std::string pluginId;
    std::string name;
    std::string version;
    bool builtin = true;
    bool critical = false;
};

class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual PluginManifest manifest() const = 0;
    virtual void initialize(PluginContext& context) = 0;
    virtual void shutdown() = 0;
    virtual std::vector<std::unique_ptr<IRawCapabilityHandler>> createCapabilityHandlers() = 0;
};

} // namespace clickin
