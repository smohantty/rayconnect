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

namespace rayconnect {

class McfAgent::Impl {
public:
    using DataType = McfAgent::DataType;
    using ProcessingFunction = McfAgent::ProcessingFunction;

    Impl(ProcessingFunction processor)
        : mWorkerThread(),
          mIncomingDataQueue(),
          mResultsQueue(),
          mRestAgent(),
          mStopRequested(false),
          mProcessor(std::move(processor)) {
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

    void submit_data(DataType data) {
        if (mStopRequested.load(std::memory_order_acquire)) {
            return;
        }
        mIncomingDataQueue.push(std::move(data));
    }

    void stop() {
        mStopRequested.store(true, std::memory_order_release);
        mIncomingDataQueue.poke_consumer();
        mResultsQueue.poke_consumer();
    }

    void worker_loop() {
        while (true) {
            bool activity_this_iteration = false;

            if (mStopRequested.load(std::memory_order_acquire)) {
                break;
            }

            std::optional<DataType> incoming_item = mIncomingDataQueue.wait_and_pop_for(std::chrono::milliseconds(50));
            if (mStopRequested.load(std::memory_order_acquire)) break;

            if (incoming_item.has_value()) {
                activity_this_iteration = true;
                mRestAgent.send_request(std::move(incoming_item.value()));
            }
            if (mStopRequested.load(std::memory_order_acquire)) break;

            std::optional<std::string> result_item = mResultsQueue.wait_and_pop_for(std::chrono::milliseconds(50));
            if (mStopRequested.load(std::memory_order_acquire)) break;

            if (result_item.has_value()) {
                activity_this_iteration = true;
                if (mProcessor) {
                    try {
                        mProcessor(result_item.value());
                    } catch (const std::exception& e) {
                        std::cerr << "McfAgent::Impl: Exception in processor: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "McfAgent::Impl: Unknown exception in processor." << std::endl;
                    }
                }
            }

            if (!activity_this_iteration && mStopRequested.load(std::memory_order_acquire)) {
                break;
            }
        }
    }

private:
    friend class McfAgent;

    std::thread mWorkerThread;
    MpscQueue<DataType> mIncomingDataQueue;
    SpscQueue<std::string> mResultsQueue;
    RestAgent mRestAgent;
    std::atomic<bool> mStopRequested;
    ProcessingFunction mProcessor;
};

McfAgent::McfAgent(ProcessingFunction processor)
    : mPimpl(std::make_unique<Impl>(std::move(processor))) {}

McfAgent::~McfAgent() = default;

McfAgent::McfAgent(McfAgent&& other) noexcept = default;

McfAgent& McfAgent::operator=(McfAgent&& other) noexcept = default;

void McfAgent::submit_data(DataType data) {
    if (mPimpl) {
        mPimpl->submit_data(std::move(data));
    }
}

void McfAgent::stop() {
    if (mPimpl) {
        mPimpl->stop();
    }
}

} // namespace rayconnect