#include <gtest/gtest.h>
#include "core/capability/CapabilityRegistry.h"
#include "core/capability/CapabilityBroker.h"
#include "core/capability/CapabilityTypes.h"
#include "sdk/TypedCapabilityHandler.h"

// ── Test contracts ─────────────────────────────────────────────────────────────

struct PingV1 {
    static constexpr std::string_view capability = "test.ping";
    static constexpr int version = 1;
    struct Request { std::string message; };
    struct Result  { std::string reply; };
};

// ── Codec ─────────────────────────────────────────────────────────────────────

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

// ── Handler helpers ───────────────────────────────────────────────────────────

class PingHandler : public clickin::TypedCapabilityHandler<PingV1> {
public:
    explicit PingHandler(std::string id, int priority = 10, bool available = true)
        : id_(std::move(id)), priority_(priority), available_(available) {}

    std::string_view providerId() const override { return id_; }

    clickin::CapabilityDescriptor describe(const clickin::CapabilityQuery&) override {
        clickin::CapabilityDescriptor d;
        d.available = available_;
        d.priority  = priority_;
        return d;
    }

protected:
    clickin::CapabilityFuture<PingV1::Result>
    invokeTyped(const PingV1::Request& req, clickin::CapabilityContext&) override {
        return clickin::CapabilityFuture<PingV1::Result>({"pong:" + id_ + ":" + req.message});
    }

private:
    std::string id_;
    int  priority_;
    bool available_;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

// Basic register + invoke path.
TEST(CapabilityBroker, RegisterAndInvoke) {
    clickin::CapabilityRegistry registry;
    registry.registerHandler(std::make_unique<PingHandler>("provider.a"));

    clickin::CapabilityBroker broker(registry);
    clickin::CapabilityQuery  q;

    auto ref = broker.findBest<PingV1>(q);
    ASSERT_TRUE(ref.valid());
    EXPECT_EQ(ref.providerId, "provider.a");

    auto future = broker.invoke<PingV1>(ref, PingV1::Request{"hello"});
    EXPECT_EQ(future.get().reply, "pong:provider.a:hello");
}

// invokeRaw on an unknown provider returns no_handler error.
TEST(CapabilityBroker, NoHandlerReturnsError) {
    clickin::CapabilityRegistry registry;
    clickin::CapabilityBroker   broker(registry);

    clickin::CapabilityRef ref{"missing.provider", "test.ping", 1};
    auto result = broker.invokeRaw(ref, {}).get();

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.errorCode, std::string(clickin::CapabilityError::no_handler));
}

// findBest picks the handler with the highest priority.
TEST(CapabilityBroker, PrioritySelection) {
    clickin::CapabilityRegistry registry;
    registry.registerHandler(std::make_unique<PingHandler>("provider.low",  5));
    registry.registerHandler(std::make_unique<PingHandler>("provider.high", 20));
    registry.registerHandler(std::make_unique<PingHandler>("provider.mid",  10));

    clickin::CapabilityBroker broker(registry);
    auto ref = broker.findBest<PingV1>(clickin::CapabilityQuery{});

    ASSERT_TRUE(ref.valid());
    EXPECT_EQ(ref.providerId, "provider.high");
}

// A disabled handler is skipped by findBest and returns handler_unavailable on invokeRaw.
TEST(CapabilityBroker, DisabledHandlerSkippedByFindBest) {
    clickin::CapabilityRegistry registry;
    registry.registerHandler(std::make_unique<PingHandler>("provider.a", 10));
    registry.setEnabled("provider.a", PingV1::capability, PingV1::version, false);

    clickin::CapabilityBroker broker(registry);
    auto ref = broker.findBest<PingV1>(clickin::CapabilityQuery{});

    EXPECT_FALSE(ref.valid());
}

TEST(CapabilityBroker, DisabledHandlerInvokeRawReturnsUnavailable) {
    clickin::CapabilityRegistry registry;
    registry.registerHandler(std::make_unique<PingHandler>("provider.a", 10));
    registry.setEnabled("provider.a", PingV1::capability, PingV1::version, false);

    clickin::CapabilityBroker broker(registry);
    clickin::CapabilityRef ref{"provider.a", std::string(PingV1::capability), PingV1::version};

    auto result = broker.invokeRaw(ref, {}).get();
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.errorCode, std::string(clickin::CapabilityError::handler_unavailable));
}

// Re-enabling a disabled handler restores it.
TEST(CapabilityBroker, ReEnableHandler) {
    clickin::CapabilityRegistry registry;
    registry.registerHandler(std::make_unique<PingHandler>("provider.a", 10));
    registry.setEnabled("provider.a", PingV1::capability, PingV1::version, false);
    registry.setEnabled("provider.a", PingV1::capability, PingV1::version, true);

    clickin::CapabilityBroker broker(registry);
    auto ref = broker.findBest<PingV1>(clickin::CapabilityQuery{});
    EXPECT_TRUE(ref.valid());
}

// When describe() returns available=false the handler is skipped.
TEST(CapabilityBroker, UnavailableHandlerSkippedByFindBest) {
    clickin::CapabilityRegistry registry;
    // priority 10, but describe() will return available=false
    registry.registerHandler(std::make_unique<PingHandler>("provider.a", 10, /*available=*/false));

    clickin::CapabilityBroker broker(registry);
    auto ref = broker.findBest<PingV1>(clickin::CapabilityQuery{});
    EXPECT_FALSE(ref.valid());
}

// findBest prefers available over unavailable regardless of priority order.
TEST(CapabilityBroker, SkipsUnavailablePicksAvailable) {
    clickin::CapabilityRegistry registry;
    registry.registerHandler(std::make_unique<PingHandler>("provider.unavail", 100, false));
    registry.registerHandler(std::make_unique<PingHandler>("provider.avail",    5, true));

    clickin::CapabilityBroker broker(registry);
    auto ref = broker.findBest<PingV1>(clickin::CapabilityQuery{});

    ASSERT_TRUE(ref.valid());
    EXPECT_EQ(ref.providerId, "provider.avail");
}

// Duplicate provider registration is rejected.
TEST(CapabilityRegistry, DuplicateRegistrationRejected) {
    clickin::CapabilityRegistry registry;
    EXPECT_TRUE( registry.registerHandler(std::make_unique<PingHandler>("provider.a")));
    EXPECT_FALSE(registry.registerHandler(std::make_unique<PingHandler>("provider.a")));
}

// listRegistered reflects enabled state.
TEST(CapabilityRegistry, ListRegistered) {
    clickin::CapabilityRegistry registry;
    registry.registerHandler(std::make_unique<PingHandler>("provider.a", 10));
    registry.registerHandler(std::make_unique<PingHandler>("provider.b", 5));
    registry.setEnabled("provider.b", PingV1::capability, PingV1::version, false);

    auto list = registry.listRegistered();
    ASSERT_EQ(list.size(), 2u);

    int enabledCount = 0;
    for (const auto& info : list)
        if (info.enabled) ++enabledCount;
    EXPECT_EQ(enabledCount, 1);
}

// CapabilityContext carries broker reference through to handlers.
TEST(CapabilityBroker, ContextCarriesBrokerRef) {
    clickin::CapabilityBroker* capturedBroker = nullptr;

    struct ContextCheckHandler : public clickin::TypedCapabilityHandler<PingV1> {
        clickin::CapabilityBroker** captured;
        explicit ContextCheckHandler(clickin::CapabilityBroker** p) : captured(p) {}
        std::string_view providerId() const override { return "provider.ctx"; }
        clickin::CapabilityDescriptor describe(const clickin::CapabilityQuery&) override {
            return {true, 1};
        }
    protected:
        clickin::CapabilityFuture<PingV1::Result>
        invokeTyped(const PingV1::Request&, clickin::CapabilityContext& ctx) override {
            *captured = ctx.broker();
            return clickin::CapabilityFuture<PingV1::Result>({});
        }
    };

    clickin::CapabilityRegistry registry;
    registry.registerHandler(std::make_unique<ContextCheckHandler>(&capturedBroker));

    clickin::CapabilityBroker broker(registry);
    auto ref = broker.findBest<PingV1>(clickin::CapabilityQuery{});
    broker.invoke<PingV1>(ref, PingV1::Request{});

    EXPECT_EQ(capturedBroker, &broker);
}
