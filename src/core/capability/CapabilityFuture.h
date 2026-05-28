#pragma once

#include <condition_variable>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

namespace clickin {

// ── Shared state ─────────────────────────────────────────────────────────────

namespace detail {

template <typename T>
struct FutureState {
    mutable std::mutex              mu;
    mutable std::condition_variable cv;
    std::optional<T>                value;
    std::exception_ptr              exception;
    std::vector<std::function<void(const T&)>>          callbacks;
    std::vector<std::function<void(std::exception_ptr)>> errCallbacks;

    // Resolve with a value. Idempotent: further calls are silently ignored.
    void resolve(T v) {
        std::vector<std::function<void(const T&)>> cbs;
        {
            std::lock_guard lock(mu);
            if (value || exception) return;
            value = std::move(v);
            cbs   = std::move(callbacks);
            errCallbacks.clear();
        }
        cv.notify_all();
        for (auto& cb : cbs) cb(*value);
    }

    // Settle with an exception. Propagates to any chained then() continuations.
    void fail(std::exception_ptr ep) {
        std::vector<std::function<void(std::exception_ptr)>> ecbs;
        {
            std::lock_guard lock(mu);
            if (value || exception) return;
            exception = std::move(ep);
            ecbs      = std::move(errCallbacks);
            callbacks.clear();
        }
        cv.notify_all();
        for (auto& cb : ecbs) cb(exception);
    }

    bool isReady() const noexcept {
        std::lock_guard lock(mu);
        return value.has_value() || exception != nullptr;
    }

    // Block until resolved. Throws if settled with an exception.
    const T& wait() const {
        std::unique_lock lock(mu);
        cv.wait(lock, [this] { return value.has_value() || exception != nullptr; });
        if (exception) std::rethrow_exception(exception);
        return *value;
    }

    // Block until resolved or until `dur` elapses. Returns true if resolved.
    template<typename Rep, typename Period>
    bool waitFor(std::chrono::duration<Rep, Period> dur) const {
        std::unique_lock lock(mu);
        return cv.wait_for(lock, dur,
            [this] { return value.has_value() || exception != nullptr; });
    }

    // Register callbacks for both success and failure.
    //   onVal  — called (outside lock) if/when the future resolves with a value.
    //   onErr  — called (outside lock) if/when the future fails; may be empty.
    // If already settled, the appropriate callback fires inline.
    void addCallback(std::function<void(const T&)>          onVal,
                     std::function<void(std::exception_ptr)> onErr = {}) {
        const T*           vp = nullptr;
        std::exception_ptr ep;
        {
            std::lock_guard lock(mu);
            if (value)          vp = &(*value);
            else if (exception) ep = exception;
            else {
                callbacks.push_back(std::move(onVal));
                if (onErr) errCallbacks.push_back(std::move(onErr));
                return;
            }
        }
        if (vp)             onVal(*vp);
        else if (ep && onErr) onErr(ep);
    }
};

} // namespace detail

// ── CapabilityFuture<T> ───────────────────────────────────────────────────────
//
// A single-assignment future that supports:
//   • Synchronous eager resolution: CapabilityFuture<T>(value)
//   • Blocking get():               future.get()          — safe on worker threads
//   • Timed wait:                   future.waitFor(dur)   — returns bool (for tests)
//   • Non-blocking then():          future.then(fn)       — fires on resolving thread;
//                                                           exceptions propagate
//   • Void callback onReady():      future.onReady(fn)    — fires on resolving thread
//   • C++20 co_await:              co_await future        — resumes on resolving thread
//   • Coroutine return type:        co_return T            — requires promise_type below
//   • Async factory:               makeAsync()            — returns {future, resolver}
//
// Threading: .get() may be called from any thread but must NOT be called on the UI
// thread for Async capabilities (use co_await onUI() or .then(fn, QObject*) instead).
// See CONTRIBUTING.md "Threading model and execution policy".

template <typename T>
class CapabilityFuture {
public:
    // ── promise_type: enables `co_return T` in coroutine functions ───────────
    struct promise_type {
        std::shared_ptr<detail::FutureState<T>> state =
            std::make_shared<detail::FutureState<T>>();

        CapabilityFuture<T> get_return_object() { return CapabilityFuture<T>(state); }
        std::suspend_never  initial_suspend()   noexcept { return {}; }
        std::suspend_never  final_suspend()     noexcept { return {}; }
        void return_value(T v) { state->resolve(std::move(v)); }
        void unhandled_exception() { state->fail(std::current_exception()); }
    };

    // ── Eagerly-resolved construction (synchronous / backward-compatible) ────
    explicit CapabilityFuture(T value)
        : state_(std::make_shared<detail::FutureState<T>>()) {
        state_->resolve(std::move(value));
    }

    // ── Blocking get — safe on worker threads; forbidden on UI thread ────────
    const T& get() const { return state_->wait(); }

    // ── Timed wait — returns true if resolved/failed within dur; does not throw ─
    // Use in tests: ASSERT_TRUE(future.waitFor(5s)) << "timed out";
    template<typename Rep, typename Period>
    bool waitFor(std::chrono::duration<Rep, Period> dur) const {
        return state_->waitFor(dur);
    }

    // ── Non-blocking then — fn(const T&) -> R; returns CapabilityFuture<R> ──
    // The continuation fires on the thread that resolves this future.
    // Exceptions thrown by fn — or exceptions from a failed source future —
    // propagate into the returned future and surface through its .get().
    // Fn must return a non-void type; for void side-effects use onReady().
    template <typename Fn>
    auto then(Fn&& fn) -> CapabilityFuture<std::invoke_result_t<Fn, const T&>> {
        using R    = std::invoke_result_t<Fn, const T&>;
        auto next  = CapabilityFuture<R>::makeUnresolved();
        auto ns    = next.state_;
        auto fn_cp = std::forward<Fn>(fn);
        state_->addCallback(
            [fn_cp, ns](const T& v) mutable {
                try { ns->resolve(fn_cp(v)); }
                catch (...) { ns->fail(std::current_exception()); }
            },
            [ns](std::exception_ptr ep) mutable { ns->fail(std::move(ep)); }
        );
        return next;
    }

    // ── Void callback — fires on resolving thread, no new future ─────────────
    // Used internally by the broker to forward results without creating an extra future.
    // onReady does NOT propagate failures — use then() when chaining is required.
    template <typename Fn>
    void onReady(Fn&& fn) {
        static_assert(std::is_invocable_v<Fn, const T&>);
        state_->addCallback(std::forward<Fn>(fn));
    }

    // ── C++20 co_await support ───────────────────────────────────────────────
    bool await_ready() const noexcept { return state_->isReady(); }

    void await_suspend(std::coroutine_handle<> h) const {
        // Resume the coroutine whether the future resolves or fails; await_resume
        // calls wait() which rethrows any stored exception.
        state_->addCallback(
            [h](const T&) mutable          { h.resume(); },
            [h](std::exception_ptr) mutable { h.resume(); }
        );
    }

    T await_resume() const { return state_->wait(); }

    // ── Async factory: {future, resolver} pair ───────────────────────────────
    // Used by the broker to create an outer future and pass the resolver to a
    // WorkerPool task that calls it once the handler completes.
    static std::pair<CapabilityFuture<T>, std::function<void(T)>> makeAsync() {
        CapabilityFuture<T> f;
        auto s = f.state_;
        return { std::move(f), [s](T v) { s->resolve(std::move(v)); } };
    }

private:
    CapabilityFuture() : state_(std::make_shared<detail::FutureState<T>>()) {}

    explicit CapabilityFuture(std::shared_ptr<detail::FutureState<T>> s)
        : state_(std::move(s)) {}

    static CapabilityFuture<T> makeUnresolved() { return CapabilityFuture<T>{}; }

    std::shared_ptr<detail::FutureState<T>> state_;

    // All CapabilityFuture instantiations are mutual friends so then() can access
    // CapabilityFuture<R>::makeUnresolved() and state_.
    template <typename U> friend class CapabilityFuture;
};

} // namespace clickin
