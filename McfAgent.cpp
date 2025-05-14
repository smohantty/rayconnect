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
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <sstream>

namespace rayconnect {

class McfAgent::Impl {
public:
    using DataType = McfAgent::DataType;
    using TopicCallback = McfAgent::TopicCallback;
    using SubscriptionHandle = McfAgent::SubscriptionHandle;

    struct MessageFromRest {
        enum class Type { RPC_RESPONSE, TOPIC_BROADCAST };
        Type type;
        DataType payload;
        std::string topic_name_if_broadcast;
    };

private:
    struct SubscriptionInfo {
        TopicCallback callback;
        std::string topic_name;
    };
    std::map<SubscriptionHandle, SubscriptionInfo> mSubscriptions;
    std::map<std::string, std::set<SubscriptionHandle>> mTopicToHandlesMap;
    std::mutex mSubscriptionMutex;
    std::atomic<SubscriptionHandle> mNextSubscriptionHandle{0};

    std::thread mWorkerThread;
    MpscQueue<DataType> mIncomingDataQueue;
    SpscQueue<MessageFromRest> mResultsQueue;
    SpscQueue<std::promise<DataType>> mPromiseFulfillmentQueue;
    RestAgent mRestAgent;
    std::atomic<bool> mStopRequested;

    MessageFromRest parse_rest_message(const std::string& raw_data) {
        if (raw_data.rfind("TOPIC:", 0) == 0) {
            std::string TpcStr = "TOPIC:";
            size_t topic_prefix_len = TpcStr.length();
            size_t first_colon = raw_data.find(':', topic_prefix_len);
            if (first_colon != std::string::npos) {
                std::string topic_name = raw_data.substr(topic_prefix_len, first_colon - topic_prefix_len);
                std::string payload = raw_data.substr(first_colon + 1);
                return {MessageFromRest::Type::TOPIC_BROADCAST, std::move(payload), std::move(topic_name)};
            }
        }
        return {MessageFromRest::Type::RPC_RESPONSE, raw_data, ""};
    }

    void dispatch_topic_update(const std::string& topic_name, const DataType& data) {
        std::vector<TopicCallback> callbacks_to_call;
        {
            std::lock_guard<std::mutex> lock(mSubscriptionMutex);
            auto it_topic = mTopicToHandlesMap.find(topic_name);
            if (it_topic != mTopicToHandlesMap.end()) {
                for (SubscriptionHandle handle : it_topic->second) {
                    auto it_sub = mSubscriptions.find(handle);
                    if (it_sub != mSubscriptions.end()) {
                        callbacks_to_call.push_back(it_sub->second.callback);
                    }
                }
            }
        }

        for (const auto& cb : callbacks_to_call) {
            try {
                cb(data);
            } catch (const std::exception& e) {
                std::cerr << "[McfAgent::Impl] Exception in topic callback for topic '" << topic_name << "': " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[McfAgent::Impl] Unknown exception in topic callback for topic '" << topic_name << "'" << std::endl;
            }
        }
    }

public:
    Impl()
        : mWorkerThread(),
          mIncomingDataQueue(),
          mResultsQueue(),
          mPromiseFulfillmentQueue(),
          mRestAgent(),
          mStopRequested(false) {
        mRestAgent.register_callback(
            [this](std::string raw_result) {
                MessageFromRest message = this->parse_rest_message(raw_result);
                this->mResultsQueue.push(std::move(message));
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

    SubscriptionHandle subscribe(const std::string& topic_name, TopicCallback callback) {
        std::lock_guard<std::mutex> lock(mSubscriptionMutex);
        SubscriptionHandle handle = mNextSubscriptionHandle++;
        mSubscriptions[handle] = {callback, topic_name};
        mTopicToHandlesMap[topic_name].insert(handle);
        return handle;
    }

    void unsubscribe(SubscriptionHandle handle) {
        std::lock_guard<std::mutex> lock(mSubscriptionMutex);
        auto it_sub = mSubscriptions.find(handle);
        if (it_sub != mSubscriptions.end()) {
            const std::string& topic_name = it_sub->second.topic_name;
            mSubscriptions.erase(it_sub);

            auto it_topic_map = mTopicToHandlesMap.find(topic_name);
            if (it_topic_map != mTopicToHandlesMap.end()) {
                it_topic_map->second.erase(handle);
                if (it_topic_map->second.empty()) {
                    mTopicToHandlesMap.erase(it_topic_map);
                }
            }
        }
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
                std::optional<std::promise<DataType>> opt_promise_drain_remaining;
                while((opt_promise_drain_remaining = mPromiseFulfillmentQueue.wait_and_pop_for(std::chrono::milliseconds(0)))) {
                    try {
                        opt_promise_drain_remaining.value().set_exception(std::make_exception_ptr(std::runtime_error("McfAgent stopped; request not processed / result not received.")));
                    } catch (const std::future_error&) {
                    }
                }
                break;
            }

            std::optional<DataType> incoming_item = mIncomingDataQueue.wait_and_pop_for(std::chrono::milliseconds(10));
            if (mStopRequested.load(std::memory_order_acquire)) { continue; }

            if (incoming_item.has_value()) {
                activity_this_iteration = true;
                mRestAgent.send_request(std::move(incoming_item.value()));
            }

            std::optional<MessageFromRest> message_item = mResultsQueue.wait_and_pop_for(std::chrono::milliseconds(10));
            if (mStopRequested.load(std::memory_order_acquire)) { continue; }

            if (message_item.has_value()) {
                activity_this_iteration = true;
                const auto& msg = message_item.value();
                if (msg.type == MessageFromRest::Type::RPC_RESPONSE) {
                    std::optional<std::promise<DataType>> opt_promise = mPromiseFulfillmentQueue.wait_and_pop_for(std::chrono::milliseconds(0));
                    if (opt_promise.has_value()) {
                        try {
                            opt_promise.value().set_value(msg.payload);
                        } catch (const std::future_error& e) {
                            std::cerr << "McfAgent::Impl: std::future_error setting RPC promise value: " << e.what() << " code: " << e.code() << std::endl;
                        } catch (const std::exception& e) {
                            std::cerr << "McfAgent::Impl: Exception setting RPC promise value: " << e.what() << std::endl;
                        } catch (...) {
                            std::cerr << "McfAgent::Impl: Unknown exception setting RPC promise value." << std::endl;
                        }
                    } else {
                        std::cerr << "McfAgent::Impl: Received RPC response from RestAgent but no pending promise. Payload: " << msg.payload << std::endl;
                    }
                } else if (msg.type == MessageFromRest::Type::TOPIC_BROADCAST) {
                    dispatch_topic_update(msg.topic_name_if_broadcast, msg.payload);
                }
            }

            if (!activity_this_iteration) {
                if (!mStopRequested.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        }
    }

private:
    friend class McfAgent;
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

McfAgent::SubscriptionHandle McfAgent::subscribe(const std::string& topic_name, TopicCallback callback) {
    if (mPimpl) {
        return mPimpl->subscribe(topic_name, std::move(callback));
    }
    std::cerr << "[McfAgent] Warning: subscribe called on uninitialized or moved-from McfAgent." << std::endl;
    return static_cast<SubscriptionHandle>(-1);
}

void McfAgent::unsubscribe(SubscriptionHandle handle) {
    if (mPimpl) {
        mPimpl->unsubscribe(handle);
    } else {
        std::cerr << "[McfAgent] Warning: unsubscribe called on uninitialized or moved-from McfAgent." << std::endl;
    }
}

void McfAgent::stop() {
    if (mPimpl) {
        mPimpl->stop();
    }
}

} // namespace rayconnect