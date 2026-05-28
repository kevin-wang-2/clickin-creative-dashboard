#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace clickin {

// Fixed-size thread pool. The destructor drains the queue and joins all threads,
// so the pool must be destroyed before any objects referenced by posted tasks.
class WorkerPool {
public:
    explicit WorkerPool(size_t threads = std::thread::hardware_concurrency());
    ~WorkerPool();

    WorkerPool(const WorkerPool&)            = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void post(std::function<void()> task);

private:
    void workerLoop();

    std::vector<std::thread>          threads_;
    std::queue<std::function<void()>> queue_;
    std::mutex                        mu_;
    std::condition_variable           cv_;
    bool                              stopping_ = false;
};

} // namespace clickin
