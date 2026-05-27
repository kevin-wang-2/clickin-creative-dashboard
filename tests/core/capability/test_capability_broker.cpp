#include <gtest/gtest.h>
#include "core/capability/CapabilityRegistry.h"
#include "core/capability/CapabilityBroker.h"
#include "sdk/TypedCapabilityHandler.h"

// Minimal smoke-test contract
struct PingV1 {
    static constexpr std::string_view capability = "test.ping";
    static constexpr int version = 1;

    struct Request { std::string message; };
    struct Result  { std::string reply; };
};

// Codec specialisation for PingV1
namespace clickin {
template <>
struct CapabilityCodec<PingV1> {
    static RawRequest encodeRequest(const PingV1::Request& req) {
        RawRequest r;
        r.capability = std::string(PingV1::capability);
        r.version    = PingV1::version;
        r.payload    = RawPayload::makeInProcess(req);
        return r;
    }
    static PingV1::Request decodeRequest(const RawRequest& raw) {
        return raw.payload.get<PingV1::Request>();
    }
    static RawResult encodeResult(const PingV1::Result& res) {
        RawResult r;
        r.ok         = true;
        r.capability = std::string(PingV1::capability);
        r.version    = PingV1::version;
        r.payload    = RawPayload::makeInProcess(res);
        return r;
    }
    static PingV1::Result decodeResult(const RawResult& raw) {
        return raw.payload.get<PingV1::Result>();
    }
};
} // namespace clickin

class PingHandler : public clickin::TypedCapabilityHandler<PingV1> {
public:
    std::string_view providerId() const override { return "test.ping_provider"; }

    clickin::CapabilityDescriptor describe(const clickin::CapabilityQuery&) override {
        clickin::CapabilityDescriptor d;
        d.available = true;
        d.priority  = 10;
        return d;
    }

protected:
    clickin::CapabilityFuture<PingV1::Result>
    invokeTyped(const PingV1::Request& req, clickin::CapabilityContext&) override {
        return clickin::CapabilityFuture<PingV1::Result>({"pong: " + req.message});
    }
};

TEST(CapabilityBroker, RegisterAndInvoke) {
    clickin::CapabilityRegistry registry;
    registry.registerHandler(std::make_unique<PingHandler>());

    clickin::CapabilityBroker broker(registry);

    clickin::CapabilityQuery q;
    auto ref = broker.findBest<PingV1>(q);

    ASSERT_EQ(ref.providerId, "test.ping_provider");
    ASSERT_EQ(ref.capability, "test.ping");

    auto future = broker.invoke<PingV1>(ref, PingV1::Request{"hello"});
    EXPECT_EQ(future.get().reply, "pong: hello");
}

TEST(CapabilityBroker, NoHandlerReturnsError) {
    clickin::CapabilityRegistry registry;
    clickin::CapabilityBroker broker(registry);

    clickin::CapabilityRef ref{"missing.provider", "test.ping", 1};
    auto future = broker.invokeRaw(ref, {});

    EXPECT_FALSE(future.get().ok);
    EXPECT_EQ(future.get().errorCode, "no_handler");
}
