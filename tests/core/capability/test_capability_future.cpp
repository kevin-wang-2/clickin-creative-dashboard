#include <gtest/gtest.h>
#include "core/capability/CapabilityFuture.h"
#include "core/capability/CapabilityRegistry.h"
#include "core/capability/CapabilityBroker.h"
#include "core/capability/CapabilityTypes.h"
#include "core/worker/WorkerPool.h"
#include "sdk/TypedCapabilityHandler.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// All tests that call .get() on a future that may not resolve synchronously
// first call waitFor() with this timeout.  If the future hangs (deadlock or
// broken chain) the test fails quickly instead of blocking the whole suite.
static constexpr auto kTimeout = 5s;

// ══════════════════════════════════════════════════════════════════════════════
//  Section 1 — CapabilityFuture<T> unit tests
//  (No broker, no WorkerPool — pure future semantics.)
// ══════════════════════════════════════════════════════════════════════════════

TEST(CapabilityFuture, EagerResolution) {
    clickin::CapabilityFuture<int> f(42);
    ASSERT_TRUE(f.waitFor(kTimeout));
    EXPECT_EQ(f.get(), 42);
}

TEST(CapabilityFuture, MakeAsyncResolveFromThread) {
    auto [f, resolve] = clickin::CapabilityFuture<int>::makeAsync();
    std::thread([r = std::move(resolve)]() mutable { r(7); }).detach();
    ASSERT_TRUE(f.waitFor(kTimeout)) << "future never resolved";
    EXPECT_EQ(f.get(), 7);
}

// get() must block on the calling thread until the future is resolved.
TEST(CapabilityFuture, GetBlocksUntilResolved) {
    auto [f, resolve] = clickin::CapabilityFuture<int>::makeAsync();

    std::atomic<bool> getReturned{false};
    std::thread getter([&]() {
        (void)f.get();
        getReturned.store(true);
    });

    std::this_thread::sleep_for(15ms);
    EXPECT_FALSE(getReturned.load()) << "get() returned before resolve() was called";

    resolve(99);
    getter.join();
    EXPECT_TRUE(getReturned.load());
}

TEST(CapabilityFuture, ThenChains) {
    clickin::CapabilityFuture<int> f(10);
    auto g = f.then([](const int& v) -> std::string {
        return "val=" + std::to_string(v);
    });
    ASSERT_TRUE(g.waitFor(kTimeout));
    EXPECT_EQ(g.get(), "val=10");
}

// then() continuation fires on the thread that resolves the original future.
TEST(CapabilityFuture, ThenFiresOnResolvingThread) {
    auto [f, resolve] = clickin::CapabilityFuture<int>::makeAsync();
    std::thread::id thenThread{};
    std::thread::id resolveThread{};

    auto g = f.then([&thenThread](const int& v) -> int {
        thenThread = std::this_thread::get_id();
        return v;
    });

    std::thread resolver([&]() {
        resolveThread = std::this_thread::get_id();
        resolve(1);
    });
    resolver.join();

    ASSERT_TRUE(g.waitFor(kTimeout)) << "chained future never resolved";
    g.get();

    EXPECT_EQ(thenThread, resolveThread);
    EXPECT_NE(thenThread, std::this_thread::get_id());
}

// onReady() fires inline when the future is already resolved.
TEST(CapabilityFuture, OnReadyFiresWhenAlreadyResolved) {
    clickin::CapabilityFuture<int> f(5);
    std::atomic<int> captured{-1};
    f.onReady([&captured](const int& v) { captured.store(v); });
    EXPECT_EQ(captured.load(), 5);
}

// onReady() fires on the resolving thread when registered before resolution.
TEST(CapabilityFuture, OnReadyFiresOnResolvingThread) {
    auto [f, resolve] = clickin::CapabilityFuture<int>::makeAsync();
    std::thread::id callbackThread{};
    std::thread::id resolveThread{};

    f.onReady([&callbackThread](const int&) {
        callbackThread = std::this_thread::get_id();
    });

    std::thread resolver([&]() {
        resolveThread = std::this_thread::get_id();
        resolve(0);
    });
    resolver.join();

    EXPECT_EQ(callbackThread, resolveThread);
}

// All registered onReady() callbacks fire exactly once.
TEST(CapabilityFuture, MultipleCallbacksAllFire) {
    auto [f, resolve] = clickin::CapabilityFuture<int>::makeAsync();
    std::atomic<int> sum{0};
    f.onReady([&sum](const int& v) { sum.fetch_add(v); });
    f.onReady([&sum](const int& v) { sum.fetch_add(v); });
    f.onReady([&sum](const int& v) { sum.fetch_add(v); });
    resolve(10);
    ASSERT_TRUE(f.waitFor(kTimeout));
    f.get();
    EXPECT_EQ(sum.load(), 30);
}

// A coroutine that throws — exception surfaces through get().
static clickin::CapabilityFuture<int> throwingCoroutine() {
    throw std::runtime_error("coroutine error");
    co_return 0;
}

TEST(CapabilityFuture, CoroutineExceptionPropagatesViaGet) {
    auto f = throwingCoroutine();
    ASSERT_TRUE(f.waitFor(kTimeout)) << "failed future should be immediately ready";
    EXPECT_THROW(f.get(), std::runtime_error);
}

// Exception from a failed source future propagates through then() chains.
// Without errCallbacks propagation this deadlocks because the chained future
// never resolves or fails.
TEST(CapabilityFuture, ThenChainPropagatesException) {
    auto f = throwingCoroutine();
    auto g = f.then([](const int& v) -> int { return v * 2; });
    ASSERT_TRUE(g.waitFor(kTimeout)) << "exception must propagate to chained future";
    EXPECT_THROW(g.get(), std::runtime_error);
}

// Multi-level chain — exception propagates through every hop.
TEST(CapabilityFuture, ThenMultiLevelExceptionPropagation) {
    auto f = throwingCoroutine();
    auto g = f.then([](const int& v) -> int { return v + 1; });
    auto h = g.then([](const int& v) -> int { return v + 1; });
    ASSERT_TRUE(h.waitFor(kTimeout)) << "exception must propagate through two hops";
    EXPECT_THROW(h.get(), std::runtime_error);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Section 2 — WorkerPool unit tests
// ══════════════════════════════════════════════════════════════════════════════

// Block the calling thread until `count` reaches `target` or 5 s elapses.
static bool waitForCount(const std::atomic<int>& count, int target) {
    auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (count.load() < target) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

TEST(WorkerPool, TasksExecute) {
    clickin::WorkerPool pool(2);
    std::atomic<int> count{0};
    for (int i = 0; i < 20; ++i)
        pool.post([&count]() { count.fetch_add(1); });
    EXPECT_TRUE(waitForCount(count, 20)) << "not all tasks completed within timeout";
}

TEST(WorkerPool, TasksRunOnNonCallerThread) {
    clickin::WorkerPool pool(1);
    std::promise<std::thread::id> tidPromise;
    auto tidFuture = tidPromise.get_future();
    pool.post([&tidPromise]() {
        tidPromise.set_value(std::this_thread::get_id());
    });
    ASSERT_EQ(tidFuture.wait_for(kTimeout), std::future_status::ready);
    EXPECT_NE(tidFuture.get(), std::this_thread::get_id());
}

TEST(WorkerPool, ConcurrentTasksAllComplete) {
    clickin::WorkerPool pool(4);
    std::atomic<int> count{0};
    for (int i = 0; i < 100; ++i)
        pool.post([&count]() { count.fetch_add(1); });
    EXPECT_TRUE(waitForCount(count, 100)) << "not all concurrent tasks completed";
}

// Pool destructor must join threads and drain the queue — no task is dropped.
TEST(WorkerPool, ShutdownDrainsInflightTasks) {
    std::atomic<bool> taskCompleted{false};
    {
        clickin::WorkerPool pool(1);
        pool.post([&taskCompleted]() {
            std::this_thread::sleep_for(20ms);
            taskCompleted.store(true);
        });
        // destructor runs here — must wait for the task above
    }
    EXPECT_TRUE(taskCompleted.load());
}

// ══════════════════════════════════════════════════════════════════════════════
//  Section 3 — Async broker dispatch
//
//  Three contracts:
//    EchoV1  — Sync;  returns request value as-is (nested call target)
//    SlowV1  — Async; waits on a Gate before resolving (long-task target)
//    NestV1  — Async; calls EchoV1 through ctx.broker() and doubles the value
// ══════════════════════════════════════════════════════════════════════════════

// ── Contracts ────────────────────────────────────────────────────────────────

struct EchoV1 {
    static constexpr std::string_view capability = "test.echo";
    static constexpr int version = 1;
    struct Request { int value; };
    struct Result  { int value; };
};

struct SlowV1 {
    static constexpr std::string_view capability = "test.slow";
    static constexpr int version = 1;
    struct Request { int id; };
    // threadId: which thread executed the handler body.
    struct Result  { int id; std::thread::id threadId; };
};

struct NestV1 {
    static constexpr std::string_view capability = "test.nest";
    static constexpr int version = 1;
    struct Request { int value; };
    struct Result  { int doubled; };
};

// ── Codecs ───────────────────────────────────────────────────────────────────

namespace clickin {

template<> struct CapabilityCodec<EchoV1> {
    static RawRequest encodeRequest(const EchoV1::Request& req) {
        RawRequest r;
        r.capability = std::string(EchoV1::capability);
        r.version    = EchoV1::version;
        r.payload    = RawPayload::makeInProcess(req);
        return r;
    }
    static EchoV1::Request decodeRequest(const RawRequest& raw) {
        return raw.payload.get<EchoV1::Request>();
    }
    static RawResult encodeResult(const EchoV1::Result& res) {
        RawResult r;
        r.ok         = true;
        r.capability = std::string(EchoV1::capability);
        r.version    = EchoV1::version;
        r.payload    = RawPayload::makeInProcess(res);
        return r;
    }
    static EchoV1::Result decodeResult(const RawResult& raw) {
        return raw.payload.get<EchoV1::Result>();
    }
};

template<> struct CapabilityCodec<SlowV1> {
    static RawRequest encodeRequest(const SlowV1::Request& req) {
        RawRequest r;
        r.capability = std::string(SlowV1::capability);
        r.version    = SlowV1::version;
        r.payload    = RawPayload::makeInProcess(req);
        return r;
    }
    static SlowV1::Request decodeRequest(const RawRequest& raw) {
        return raw.payload.get<SlowV1::Request>();
    }
    static RawResult encodeResult(const SlowV1::Result& res) {
        RawResult r;
        r.ok         = true;
        r.capability = std::string(SlowV1::capability);
        r.version    = SlowV1::version;
        r.payload    = RawPayload::makeInProcess(res);
        return r;
    }
    static SlowV1::Result decodeResult(const RawResult& raw) {
        return raw.payload.get<SlowV1::Result>();
    }
};

template<> struct CapabilityCodec<NestV1> {
    static RawRequest encodeRequest(const NestV1::Request& req) {
        RawRequest r;
        r.capability = std::string(NestV1::capability);
        r.version    = NestV1::version;
        r.payload    = RawPayload::makeInProcess(req);
        return r;
    }
    static NestV1::Request decodeRequest(const RawRequest& raw) {
        return raw.payload.get<NestV1::Request>();
    }
    static RawResult encodeResult(const NestV1::Result& res) {
        RawResult r;
        r.ok         = true;
        r.capability = std::string(NestV1::capability);
        r.version    = NestV1::version;
        r.payload    = RawPayload::makeInProcess(res);
        return r;
    }
    static NestV1::Result decodeResult(const RawResult& raw) {
        return raw.payload.get<NestV1::Result>();
    }
};

} // namespace clickin

// ── Gate: lets a test control when a handler is allowed to complete ──────────

struct Gate {
    std::mutex              mu;
    std::condition_variable cv;
    bool                    open = false;

    void waitUntilOpen() {
        std::unique_lock lk(mu);
        cv.wait(lk, [this] { return open; });
    }
    void openGate() {
        { std::lock_guard lk(mu); open = true; }
        cv.notify_all();
    }
};

// ── Handlers ─────────────────────────────────────────────────────────────────

class EchoHandler : public clickin::TypedCapabilityHandler<EchoV1> {
public:
    explicit EchoHandler(std::string id) : id_(std::move(id)) {}
    std::string_view providerId() const override { return id_; }
    clickin::ExecutionPolicy executionPolicy() const override {
        return clickin::ExecutionPolicy::Sync;
    }
    clickin::CapabilityDescriptor describe(const clickin::CapabilityQuery&) override {
        return {true, 10};
    }
protected:
    clickin::CapabilityFuture<EchoV1::Result>
    invokeTyped(const EchoV1::Request& req, clickin::CapabilityContext&) override {
        return clickin::CapabilityFuture<EchoV1::Result>(EchoV1::Result{req.value});
    }
private:
    std::string id_;
};

// SlowHandler blocks on a Gate before returning, allowing the test to verify
// that the caller thread is not blocked while the handler is pending.
class SlowHandler : public clickin::TypedCapabilityHandler<SlowV1> {
public:
    SlowHandler(std::string id, Gate& gate,
                std::atomic<bool>& started, std::atomic<bool>& completed)
        : id_(std::move(id)), gate_(gate), started_(started), completed_(completed) {}

    std::string_view providerId() const override { return id_; }
    clickin::ExecutionPolicy executionPolicy() const override {
        return clickin::ExecutionPolicy::Async;
    }
    clickin::CapabilityDescriptor describe(const clickin::CapabilityQuery&) override {
        return {true, 10};
    }
protected:
    clickin::CapabilityFuture<SlowV1::Result>
    invokeTyped(const SlowV1::Request& req, clickin::CapabilityContext&) override {
        started_.store(true);
        gate_.waitUntilOpen();
        completed_.store(true);
        return clickin::CapabilityFuture<SlowV1::Result>(
            SlowV1::Result{req.id, std::this_thread::get_id()});
    }
private:
    std::string          id_;
    Gate&                gate_;
    std::atomic<bool>&   started_;
    std::atomic<bool>&   completed_;
};

// NestHandler (Async) calls EchoV1 through ctx.broker() using then() — no .get()
// inside the handler, so it never deadlocks even on a single-thread pool.
class NestHandler : public clickin::TypedCapabilityHandler<NestV1> {
public:
    explicit NestHandler(std::string id) : id_(std::move(id)) {}
    std::string_view providerId() const override { return id_; }
    clickin::ExecutionPolicy executionPolicy() const override {
        return clickin::ExecutionPolicy::Async;
    }
    clickin::CapabilityDescriptor describe(const clickin::CapabilityQuery&) override {
        return {true, 10};
    }
protected:
    clickin::CapabilityFuture<NestV1::Result>
    invokeTyped(const NestV1::Request& req, clickin::CapabilityContext& ctx) override {
        auto echoRef = ctx.broker()->findBest<EchoV1>({});
        // Chain via then() — never blocks the worker thread.
        return ctx.broker()->invoke<EchoV1>(echoRef, EchoV1::Request{req.value})
            .then([](const EchoV1::Result& r) -> NestV1::Result {
                return NestV1::Result{r.value * 2};
            });
    }
private:
    std::string id_;
};

// AsyncEchoHandler: same as EchoHandler but Async — used in the
// Async-calls-Async deadlock test.
class AsyncEchoHandler : public clickin::TypedCapabilityHandler<EchoV1> {
public:
    explicit AsyncEchoHandler(std::string id) : id_(std::move(id)) {}
    std::string_view providerId() const override { return id_; }
    clickin::ExecutionPolicy executionPolicy() const override {
        return clickin::ExecutionPolicy::Async;
    }
    clickin::CapabilityDescriptor describe(const clickin::CapabilityQuery&) override {
        return {true, 10};
    }
protected:
    clickin::CapabilityFuture<EchoV1::Result>
    invokeTyped(const EchoV1::Request& req, clickin::CapabilityContext&) override {
        return clickin::CapabilityFuture<EchoV1::Result>(EchoV1::Result{req.value});
    }
private:
    std::string id_;
};

// IdEchoHandler: Async, returns the request id unchanged — used for the
// concurrent-invocations test.
class IdEchoHandler : public clickin::TypedCapabilityHandler<SlowV1> {
public:
    explicit IdEchoHandler(std::string id) : id_(std::move(id)) {}
    std::string_view providerId() const override { return id_; }
    clickin::ExecutionPolicy executionPolicy() const override {
        return clickin::ExecutionPolicy::Async;
    }
    clickin::CapabilityDescriptor describe(const clickin::CapabilityQuery&) override {
        return {true, 10};
    }
protected:
    clickin::CapabilityFuture<SlowV1::Result>
    invokeTyped(const SlowV1::Request& req, clickin::CapabilityContext&) override {
        std::this_thread::yield();
        return clickin::CapabilityFuture<SlowV1::Result>(
            SlowV1::Result{req.id, std::this_thread::get_id()});
    }
private:
    std::string id_;
};

// ── Fixture ───────────────────────────────────────────────────────────────────

class AsyncBrokerTest : public ::testing::Test {
protected:
    clickin::WorkerPool         pool_{4};
    clickin::CapabilityRegistry registry_;
    clickin::CapabilityBroker   broker_{registry_};

    void SetUp() override {
        broker_.setServices({.workerPool = &pool_});
    }
};

// ── Test: Async handler runs on a WorkerPool thread, not the test thread ─────

TEST_F(AsyncBrokerTest, AsyncHandlerRunsOnWorkerThread) {
    Gate gate;
    std::atomic<bool> started{false}, completed{false};
    registry_.registerHandler(
        std::make_unique<SlowHandler>("provider.slow", gate, started, completed));

    auto ref = broker_.findBest<SlowV1>({});
    ASSERT_TRUE(ref.valid());

    gate.openGate();
    auto future = broker_.invoke<SlowV1>(ref, SlowV1::Request{0});
    ASSERT_TRUE(future.waitFor(kTimeout)) << "async handler never resolved";

    EXPECT_NE(future.get().threadId, std::this_thread::get_id())
        << "Async handler must not run on the test thread";
}

// ── Test: long task — invoke() returns before the handler finishes ────────────
//
// Sequence:
//   1. invoke() posts to pool and returns an unresolved future immediately.
//   2. Handler starts, sets `started`, then blocks on gate.
//   3. Test verifies `completed` is still false (handler is waiting on gate).
//   4. Test opens the gate; handler finishes.
//   5. future resolves; waitFor() returns true; get() returns the correct result.

TEST_F(AsyncBrokerTest, LongTaskDoesNotBlockCaller) {
    Gate gate;
    std::atomic<bool> started{false}, completed{false};
    registry_.registerHandler(
        std::make_unique<SlowHandler>("provider.slow", gate, started, completed));

    auto ref = broker_.findBest<SlowV1>({});
    ASSERT_TRUE(ref.valid());

    auto future = broker_.invoke<SlowV1>(ref, SlowV1::Request{42});

    // Wait until the handler has actually started on the pool thread.
    auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (!started.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);

    ASSERT_TRUE(started.load()) << "handler never started within timeout";
    // Handler is running but waiting — the future must not be ready yet.
    EXPECT_FALSE(future.waitFor(0s)) << "future resolved before gate was opened";
    EXPECT_FALSE(completed.load())   << "handler completed before gate was opened";

    gate.openGate();

    ASSERT_TRUE(future.waitFor(kTimeout)) << "future never resolved after gate opened";
    auto result = future.get();
    EXPECT_TRUE(completed.load());
    EXPECT_EQ(result.id, 42);
}

// ── Test: nested Sync-in-Async (most common case) ─────────────────────────────
//
// NestHandler (Async, 1-thread pool) calls EchoHandler (Sync) via then().
// No blocking inside the handler body — must never deadlock.

TEST_F(AsyncBrokerTest, NestedAsyncCallsSyncNoDeadlock) {
    clickin::WorkerPool         singlePool(1);
    clickin::CapabilityRegistry reg2;
    clickin::CapabilityBroker   broker2(reg2);
    broker2.setServices({.workerPool = &singlePool});

    reg2.registerHandler(std::make_unique<EchoHandler>("provider.echo"));
    reg2.registerHandler(std::make_unique<NestHandler>("provider.nest"));

    auto ref = broker2.findBest<NestV1>({});
    ASSERT_TRUE(ref.valid());

    auto future = broker2.invoke<NestV1>(ref, NestV1::Request{7});
    ASSERT_TRUE(future.waitFor(kTimeout)) << "nested call deadlocked";
    EXPECT_EQ(future.get().doubled, 14);
}

// ── Test: nested Async-calls-Async on a single-thread pool ───────────────────
//
// NestHandler (Async) calls AsyncEchoHandler (also Async) via then().
// The key invariant: NestHandler must return BEFORE EchoHandler completes so
// the single pool thread is freed to run EchoHandler.  A .get() inside the
// handler body would deadlock here deterministically.

TEST_F(AsyncBrokerTest, NestedAsyncCallsAsyncNoDeadlock) {
    clickin::WorkerPool         singlePool(1);
    clickin::CapabilityRegistry reg2;
    clickin::CapabilityBroker   broker2(reg2);
    broker2.setServices({.workerPool = &singlePool});

    reg2.registerHandler(std::make_unique<AsyncEchoHandler>("provider.echo"));
    reg2.registerHandler(std::make_unique<NestHandler>("provider.nest"));

    auto ref = broker2.findBest<NestV1>({});
    ASSERT_TRUE(ref.valid());

    auto future = broker2.invoke<NestV1>(ref, NestV1::Request{5});
    ASSERT_TRUE(future.waitFor(kTimeout)) << "Async-calls-Async deadlocked on single-thread pool";
    EXPECT_EQ(future.get().doubled, 10);
}

// ── Test: concurrent invocations — results must not be interleaved ────────────
//
// Posts N invocations with distinct IDs.  Each Async handler captures its
// request ID and returns it.  Verifies that concurrent execution on the pool
// never mixes up which result belongs to which request.

TEST_F(AsyncBrokerTest, ConcurrentInvocationsPreserveResults) {
    registry_.registerHandler(std::make_unique<IdEchoHandler>("provider.idecho"));

    auto ref = broker_.findBest<SlowV1>({});
    ASSERT_TRUE(ref.valid());

    constexpr int N = 200;
    std::vector<clickin::CapabilityFuture<SlowV1::Result>> futures;
    futures.reserve(N);

    for (int i = 0; i < N; ++i)
        futures.push_back(broker_.invoke<SlowV1>(ref, SlowV1::Request{i}));

    for (int i = 0; i < N; ++i) {
        ASSERT_TRUE(futures[i].waitFor(kTimeout)) << "future " << i << " timed out";
        auto result = futures[i].get();
        EXPECT_EQ(result.id, i) << "result " << i << " has wrong id (thread safety failure)";
        EXPECT_NE(result.threadId, std::this_thread::get_id())
            << "result " << i << " ran on test thread instead of pool";
    }
}

// ── Test: without a WorkerPool, Async handlers fall back to inline execution ──

TEST(CapabilityBrokerAsyncFallback, NoPoolRunsInline) {
    Gate gate;
    std::atomic<bool> started{false}, completed{false};

    clickin::CapabilityRegistry registry;
    clickin::CapabilityBroker   broker(registry);
    // setServices not called — WorkerPool is null.

    gate.openGate();  // don't block
    registry.registerHandler(
        std::make_unique<SlowHandler>("provider.slow", gate, started, completed));

    auto ref = broker.findBest<SlowV1>({});
    ASSERT_TRUE(ref.valid());

    auto future = broker.invoke<SlowV1>(ref, SlowV1::Request{0});
    ASSERT_TRUE(future.waitFor(kTimeout)) << "inline fallback never resolved";

    auto result = future.get();
    EXPECT_TRUE(completed.load());
    EXPECT_EQ(result.threadId, std::this_thread::get_id())
        << "Without WorkerPool, handler must run inline on caller thread";
}
