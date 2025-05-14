#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <utility>
#include <chrono>

namespace rayconnect {

template<typename T>
class SpscQueue {
public:
    SpscQueue() = default;

    // Pushes an item into the queue.
    // Should only be called by the single producer.
    void push(T item) {
        std::lock_guard<std::mutex> lock(mMutex);
        mQueue.push(std::move(item));
        mCondVar.notify_one(); // Notify the consumer
    }

    // Waits until an item is available or the queue is closed, then pops it.
    // Returns std::nullopt if the queue is closed and empty.
    // Should only be called by the single consumer.
    std::optional<T> wait_and_pop_for(std::chrono::milliseconds timeout_duration) {
        std::unique_lock<std::mutex> lock(mMutex);
        if (mCondVar.wait_for(lock, timeout_duration, [this] { return !mQueue.empty(); })) {
            // Predicate met, queue is not empty
            T item = std::move(mQueue.front());
            mQueue.pop();
            return item;
        }
        // Timed out, queue might still be empty or predicate not met after timeout
        return std::nullopt;
    }

    // Allows an external agent (like McfAgent) to wake up a consumer thread
    // that might be blocked in wait_and_pop_for.
    void poke_consumer() {
        mCondVar.notify_one();
    }

private:
    std::queue<T> mQueue;
    mutable std::mutex mMutex;
    std::condition_variable mCondVar;
};

} // namespace rayconnect

#endif // SPSC_QUEUE_H