#pragma once

#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace clickin {

// MVP: synchronous eager future — the value is already resolved on construction.
// Interface is shaped for async extension: .then() chaining is preserved.
// Future implementation may swap the backing store for std::future / Qt signals
// without changing caller code.
template <typename T>
class CapabilityFuture {
public:
    explicit CapabilityFuture(T value) : value_(std::move(value)) {}

    const T& get() const { return value_; }

    template <typename F>
    auto then(F&& fn) -> CapabilityFuture<std::invoke_result_t<F, const T&>> {
        using R = std::invoke_result_t<F, const T&>;
        return CapabilityFuture<R>(std::forward<F>(fn)(value_));
    }

private:
    T value_;
};

} // namespace clickin
