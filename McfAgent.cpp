#include "McfAgent.h"
#include "MpscQueue.h"
#include "SpscQueue.h"
#include "RestAgent.h"

#include <thread>
#include <atomic>
#include <iostream>
#include <utility>
#include <optional>
#include <chrono>
#include <future>

namespace rayconnect {

class McfAgent::Impl {
public:
    using DataType = McfAgent::DataType;

    Impl()
        : mWorkerThread(),
          mIncomingDataQueue(),
          mResultsQueue(),
          mPromiseFulfillmentQueue(),
          mRestAgent(),
          mStopRequested(false) {
        mRestAgent.register_callback(
            [this](std::string result) {
                this->mResultsQueue.push(std::move(result));
            }
        );

        mWorkerThread = std::thread(&McfAgent::Impl::worker_loop, this);
    }

    ~Impl() {
        stop();
        if (mWorkerThread.joinable()) {
            mWorkerThread.join();
        }
    }

    std::future<DataType> submit_data(DataType data) {
        if (mStopRequested.load(std::memory_order_acquire)) {
            std::promise<DataType> p;
            p.set_exception(std::make_exception_ptr(std::runtime_error("McfAgent is stopping/stopped.")));
            return p.get_future();
        }
        std::promise<DataType> promise;
        std::future<DataType> future = promise.get_future();
        mIncomingDataQueue.push(std::move(data));
        mPromiseFulfillmentQueue.push(std::move(promise));
        return future;
    }

    void stop() {
        mStopRequested.store(true, std::memory_order_release);
        mIncomingDataQueue.poke_consumer();
        mResultsQueue.poke_consumer();
        mPromiseFulfillmentQueue.poke_consumer();
    }

    void worker_loop() {
        while (true) {
            bool activity_this_iteration = false;

            if (mStopRequested.load(std::memory_order_acquire)) {
                std::optional<DataType> incoming_item_drain;
                while((incoming_item_drain = mIncomingDataQueue.wait_and_pop_for(std::chrono::milliseconds(0)))) {
                    std::optional<std::promise<DataType>> opt_promise_drain = mPromiseFulfillmentQueue.wait_and_pop_for(std::chrono::milliseconds(0));
                    if (opt_promise_drain.has_value()) {
                        try {
                            opt_promise_drain.value().set_exception(std::make_exception_ptr(std::runtime_error("McfAgent stopped before processing.")));
                        } catch (const std::future_error& e) {
                        }
                    }
                }
                std::optional<std::string> result_item_drain;
                while((result_item_drain = mResultsQueue.wait_and_pop_for(std::chrono::milliseconds(0)))) {
                     std::optional<std::promise<DataType>> opt_promise_drain = mPromiseFulfillmentQueue.wait_and_pop_for(std::chrono::milliseconds(0));
                    if (opt_promise_drain.has_value()) {
                        try {
                             opt_promise_drain.value().set_exception(std::make_exception_ptr(std::runtime_error("McfAgent stopped while result was pending.")));
                        } catch (const std::future_error& e) {
                        }
                    }
                }
                 std::optional<std::promise<DataType>> opt_promise_drain_remaining;
                 while((opt_promise_drain_remaining = mPromiseFulfillmentQueue.wait_and_pop_for(std::chrono::milliseconds(0)))) {
                    try {
                        opt_promise_drain_remaining.value().set_exception(std::make_exception_ptr(std::runtime_error("McfAgent stopped; request not processed.")));
                    } catch (const std::future_error& e) {
                    }
                 }
                break;
            }

            std::optional<DataType> incoming_item = mIncomingDataQueue.wait_and_pop_for(std::chrono::milliseconds(50));
            if (mStopRequested.load(std::memory_order_acquire)) { continue; }

            if (incoming_item.has_value()) {
                activity_this_iteration = true;
                mRestAgent.send_request(std::move(incoming_item.value()));
            }
            if (mStopRequested.load(std::memory_order_acquire)) { continue; }

            std::optional<std::string> result_item = mResultsQueue.wait_and_pop_for(std::chrono::milliseconds(50));
            if (mStopRequested.load(std::memory_order_acquire)) { continue; }

            if (result_item.has_value()) {
                activity_this_iteration = true;
                std::optional<std::promise<DataType>> opt_promise = mPromiseFulfillmentQueue.wait_and_pop_for(std::chrono::milliseconds(1));
                if (opt_promise.has_value()) {
                    try {
                        opt_promise.value().set_value(result_item.value());
                    } catch (const std::future_error& e) {
                        std::cerr << "McfAgent::Impl: std::future_error setting promise value: " << e.what() << " code: " << e.code() << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "McfAgent::Impl: Exception setting promise value: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "McfAgent::Impl: Unknown exception setting promise value." << std::endl;
                    }
                } else {
                    std::cerr << "McfAgent::Impl: Received result from RestAgent but no pending promise in mPromiseFulfillmentQueue. Result: " << result_item.value() << std::endl;
                }
            }

            if (!activity_this_iteration && mStopRequested.load(std::memory_order_acquire)) {
            }
        }
    }

private:
    friend class McfAgent;

    std::thread mWorkerThread;
    MpscQueue<DataType> mIncomingDataQueue;
    SpscQueue<std::string> mResultsQueue;
    SpscQueue<std::promise<DataType>> mPromiseFulfillmentQueue;
    RestAgent mRestAgent;
    std::atomic<bool> mStopRequested;
};

McfAgent::McfAgent()
    : mPimpl(std::make_unique<Impl>()) {}

McfAgent::~McfAgent() = default;

McfAgent::McfAgent(McfAgent&& other) noexcept = default;

McfAgent& McfAgent::operator=(McfAgent&& other) noexcept = default;

std::future<McfAgent::DataType> McfAgent::submit_data(DataType data) {
    if (mPimpl) {
        return mPimpl->submit_data(std::move(data));
    }
    std::promise<DataType> p;
    p.set_exception(std::make_exception_ptr(std::runtime_error("McfAgent not initialized or moved from.")));
    return p.get_future();
}

void McfAgent::stop() {
    if (mPimpl) {
        mPimpl->stop();
    }
}

} // namespace rayconnect