#pragma once

// A pool for whole-frame pixel work: splitting a band of rows, reordering
// channels, converting a frame. Every one of those runs once per frame at
// 1.45 M pixels, so creating threads for each is a visible cost -- the pool is
// created once and the workers park between frames.

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace gtc::native {

class FramePool {
public:
    explicit FramePool(std::size_t threads) {
        if (threads < 2) return;
        workers_.reserve(threads - 1);
        for (std::size_t index = 1; index < threads; ++index) {
            workers_.emplace_back([this] { work(); });
        }
    }

    ~FramePool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            ++generation_;
        }
        start_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    FramePool(const FramePool&) = delete;
    FramePool& operator=(const FramePool&) = delete;

    std::size_t size() const { return workers_.size() + 1; }

    // Same split, but each chunk is also told its own index, so results can be
    // collected per chunk without a shared counter or a lock.
    void rangesIndexed(std::size_t chunks, std::size_t total,
                       const std::function<void(std::size_t, std::size_t, std::size_t)>& body) {
        run(chunks, total, nullptr, &body);
    }

    // Splits [0, total) into `chunks` contiguous pieces and runs them. The
    // caller takes part, so a one-core machine still does the work.
    void ranges(std::size_t chunks, std::size_t total,
                const std::function<void(std::size_t, std::size_t)>& body) {
        run(chunks, total, &body, nullptr);
    }

private:
    void run(std::size_t chunks, std::size_t total,
             const std::function<void(std::size_t, std::size_t)>* body,
             const std::function<void(std::size_t, std::size_t, std::size_t)>* indexed) {
        if (chunks <= 1 || workers_.empty() || total == 0) {
            if (indexed) (*indexed)(0, total, 0); else (*body)(0, total);
            return;
        }
        chunks = std::min(chunks, size());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            body_ = body;
            indexed_ = indexed;
            chunks_ = chunks;
            total_ = total;
            next_.store(0, std::memory_order_relaxed);
            // One decrement per participant: every worker, plus the caller.
            // Counting chunks instead left this waiting for a decrement that
            // only ever came from the workers -- a deadlock on the first frame.
            remaining_.store(workers_.size() + 1, std::memory_order_relaxed);
            ++generation_;
        }
        start_.notify_all();
        drain();
        // The caller is a participant too, so it counts down like one.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) done_.notify_all();
        }
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [this] { return remaining_.load(std::memory_order_acquire) == 0; });
        body_ = nullptr;
        indexed_ = nullptr;
    }

public:

    void work() {
        unsigned seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                start_.wait(lock, [&] { return stopping_ || generation_ != seen; });
                if (stopping_) return;
                seen = generation_;
            }
            drain();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) done_.notify_all();
            }
        }
    }

    // Takes chunks until there are none left: whoever is free does the next one,
    // so a slow chunk does not hold up the others.
    void drain() {
        for (;;) {
            const std::size_t index = next_.fetch_add(1, std::memory_order_acq_rel);
            if (index >= chunks_) return;
            const std::size_t begin = total_ * index / chunks_;
            const std::size_t end = total_ * (index + 1) / chunks_;
            if (indexed_) (*indexed_)(begin, end, index);
            else (*body_)(begin, end);
        }
    }

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable start_;
    std::condition_variable done_;
    const std::function<void(std::size_t, std::size_t)>* body_ = nullptr;
    const std::function<void(std::size_t, std::size_t, std::size_t)>* indexed_ = nullptr;
    std::size_t chunks_ = 0;
    std::size_t total_ = 0;
    std::atomic<std::size_t> next_{0};
    std::atomic<std::size_t> remaining_{0};
    unsigned generation_ = 0;
    bool stopping_ = false;
};

// How many threads the client will use for one frame's worth of parallel work.
// GTC_THREADS overrides it; otherwise half the machine, because the rest of it
// is busy being the game.
inline std::size_t frameThreads() {
    static const std::size_t count = [] {
        if (const char* setting = std::getenv("GTC_THREADS")) {
            const int value = std::atoi(setting);
            if (value > 0) return static_cast<std::size_t>(value);
        }
        const unsigned hardware = std::thread::hardware_concurrency();
        return static_cast<std::size_t>(hardware == 0 ? 2 : std::max(2u, hardware / 2));
    }();
    return count;
}

// One pool for the whole client: geometry, band splits, channel reorders and
// frame conversions all take their turn in it rather than each sizeable piece
// of work owning its own set of parked threads. Inline so that every translation
// unit shares the one instance, including the test binaries that link only part
// of the client.
inline FramePool& framePool() {
    static FramePool pool(frameThreads());
    return pool;
}

} // namespace gtc::native
