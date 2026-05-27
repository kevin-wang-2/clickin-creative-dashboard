#pragma once

#include <string_view>

namespace clickin {

struct CapabilityQuery;
struct CapabilityDescriptor;
struct RawRequest;
struct RawResult;
class CapabilityContext;

template <typename T>
class CapabilityFuture;

class IRawCapabilityHandler {
public:
    virtual ~IRawCapabilityHandler() = default;

    virtual std::string_view capabilityName() const = 0;
    virtual int capabilityVersion() const = 0;
    virtual std::string_view providerId() const = 0;

    virtual CapabilityDescriptor describe(const CapabilityQuery& query) = 0;

    virtual CapabilityFuture<RawResult>
    invokeRaw(const RawRequest& request, CapabilityContext& ctx) = 0;
};

} // namespace clickin
