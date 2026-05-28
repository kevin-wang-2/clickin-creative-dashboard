#include "core/worker/WorkerPool.h"

#include <algorithm>

namespace clickin {

WorkerPool::WorkerPool(size_t threads) {
    size_t n = std::max(threads, size_t(1));
    threads_.reserve(n);
    for (size_t i = 0; i < n; ++i)
        threads_.emplace_back([this] { workerLoop(); });
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard lock(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
}

void WorkerPool::post(std::function<void()> task) {
    {
        std::lock_guard lock(mu_);
        queue_.push(std::move(task));
    }
    cv_.notify_one();
}

void WorkerPool::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(mu_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            task = std::move(queue_.front());
            queue_.pop();
        }
        task();
    }
}

} // namespace clickin
