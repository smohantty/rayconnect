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
    using SubscriptionHandle = McfAgent::SubscriptionHandle;
    using ITopicListener = McfAgent::ITopicListener;

    struct MessageFromRest {
        DataType full_raw_data_from_rest; // Always store the full string from RestAgent
        bool is_topic_broadcast;
        std::string topic_name_if_broadcast;
        DataType topic_payload_if_broadcast; // Parsed payload specifically for the topic
    };

private:
    struct SubscriptionInfo {
        std::weak_ptr<ITopicListener> listener;
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

    MessageFromRest parse_rest_message(const std::string& raw_data_from_rest) {
        const std::string topic_marker = "TOPIC:";
        size_t topic_marker_pos = raw_data_from_rest.find(topic_marker);

        if (topic_marker_pos != std::string::npos) {
            size_t topic_name_start = topic_marker_pos + topic_marker.length();
            size_t first_colon_after_topic_name = raw_data_from_rest.find(':', topic_name_start);

            if (first_colon_after_topic_name != std::string::npos) {
                std::string topic_name = raw_data_from_rest.substr(topic_name_start, first_colon_after_topic_name - topic_name_start);
                std::string topic_payload = raw_data_from_rest.substr(first_colon_after_topic_name + 1);
                // For topic broadcasts, the specific topic payload is parsed out.
                return {raw_data_from_rest, true, std::move(topic_name), std::move(topic_payload)};
            }
        }
        // Default: not a (validly formatted) topic broadcast. topic_payload_if_broadcast will be empty.
        return {raw_data_from_rest, false, "", ""};
    }

    void dispatch_topic_update(const std::string& topic_name, const DataType& topic_data) {
        std::vector<std::shared_ptr<ITopicListener>> listeners_to_call;
        {
            std::lock_guard<std::mutex> lock(mSubscriptionMutex);
            auto it_topic = mTopicToHandlesMap.find(topic_name);
            if (it_topic != mTopicToHandlesMap.end()) {
                std::vector<SubscriptionHandle> handles_to_cleanup;
                for (SubscriptionHandle handle : it_topic->second) {
                    auto it_sub = mSubscriptions.find(handle);
                    if (it_sub != mSubscriptions.end()) {
                        if (auto listener_shared = it_sub->second.listener.lock()) {
                            listeners_to_call.push_back(listener_shared);
                        } else {
                            handles_to_cleanup.push_back(handle);
                        }
                    }
                }
                for (SubscriptionHandle handle_to_remove : handles_to_cleanup) {
                    mSubscriptions.erase(handle_to_remove);
                    it_topic->second.erase(handle_to_remove);
                }
                if (it_topic->second.empty()) {
                    mTopicToHandlesMap.erase(it_topic);
                }
            }
        }

        for (const auto& listener_sp : listeners_to_call) {
            try {
                listener_sp->on_topic_data(topic_name, topic_data);
            } catch (const std::exception& e) {
                std::cerr << "[McfAgent::Impl] Exception in ITopicListener::on_topic_data for topic '" << topic_name << "': " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[McfAgent::Impl] Unknown exception in ITopicListener::on_topic_data for topic '" << topic_name << "'" << std::endl;
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

    SubscriptionHandle subscribe(const std::string& topic_name, std::weak_ptr<ITopicListener> listener) {
        if (listener.expired()) {
            std::cerr << "[McfAgent::Impl] Warning: Attempt to subscribe with an expired listener for topic: " << topic_name << std::endl;
            return static_cast<SubscriptionHandle>(-1);
        }
        std::lock_guard<std::mutex> lock(mSubscriptionMutex);
        SubscriptionHandle handle = mNextSubscriptionHandle++;
        mSubscriptions[handle] = {listener, topic_name};
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
                const auto& msg_from_rest = message_item.value();

                std::optional<std::promise<DataType>> opt_promise = mPromiseFulfillmentQueue.wait_and_pop_for(std::chrono::milliseconds(0));
                if (opt_promise.has_value()) {
                    try {
                        opt_promise.value().set_value(msg_from_rest.full_raw_data_from_rest);
                    } catch (const std::future_error& e) {
                        std::cerr << "McfAgent::Impl: std::future_error setting promise value: " << e.what() << " code: " << e.code() << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "McfAgent::Impl: Exception setting promise value: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "McfAgent::Impl: Unknown exception setting promise value." << std::endl;
                    }
                } else {
                    std::cerr << "McfAgent::Impl: Received data from RestAgent ('" << msg_from_rest.full_raw_data_from_rest
                              << "') but no pending promise in mPromiseFulfillmentQueue." << std::endl;
                }

                if (msg_from_rest.is_topic_broadcast) {
                    dispatch_topic_update(msg_from_rest.topic_name_if_broadcast, msg_from_rest.topic_payload_if_broadcast);
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

McfAgent::SubscriptionHandle McfAgent::subscribe(const std::string& topic_name, std::weak_ptr<ITopicListener> listener) {
    if (mPimpl) {
        return mPimpl->subscribe(topic_name, listener);
    }
    std::cerr << "[McfAgent] Warning: subscribe called on uninitialized or moved-from McfAgent for topic: " << topic_name << std::endl;
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