#ifndef MPSC_QUEUE_H
#define MPSC_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <utility>
#include <chrono>

namespace rayconnect {

template<typename T>
class MpscQueue {
public:
    MpscQueue() = default;

    void push(T item) {
        std::lock_guard<std::mutex> lock(mMutex);
        mQueue.push(std::move(item));
        mCondVar.notify_one();
    }

    std::optional<T> wait_and_pop_for(std::chrono::milliseconds timeout_duration) {
        std::unique_lock<std::mutex> lock(mMutex);
        if (mCondVar.wait_for(lock, timeout_duration, [this] { return !mQueue.empty(); })) {
            T item = std::move(mQueue.front());
            mQueue.pop();
            return item;
        }
        return std::nullopt;
    }

    void poke_consumer() {
        mCondVar.notify_one();
    }

private:
    std::queue<T> mQueue;
    mutable std::mutex mMutex;
    std::condition_variable mCondVar;
};

} // namespace rayconnect

#endif // MPSC_QUEUE_H