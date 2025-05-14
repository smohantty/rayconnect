#include "RestAgent.h"

#include <thread>   // For std::thread
#include <chrono>   // For std::chrono::seconds
#include <iostream> // For potential debug cout
#include <utility>  // For std::move

namespace rayconnect {

RestAgent::RestAgent()
    : mRequestQueue(),
      mRegisteredCallback(nullptr), // Initialize callback to null
      mStopRequested(false) {       // Initialize stop flag
    mWorkerThread = std::thread(&RestAgent::worker_loop, this);
}

RestAgent::~RestAgent() {
    mStopRequested.store(true, std::memory_order_release);
    mRequestQueue.poke_consumer(); // Wake up worker if it's waiting on the queue
    if (mWorkerThread.joinable()) {
        mWorkerThread.join();
    }
}

void RestAgent::register_callback(std::function<void(std::string result)> callback) {
    std::lock_guard<std::mutex> lock(mCallbackMutex); // Protect access to mRegisteredCallback
    mRegisteredCallback = std::move(callback);
}

void RestAgent::send_request(std::string request_data) {
    if (mStopRequested.load(std::memory_order_acquire)) {
        // Agent is stopped or stopping, do not accept new requests.
        // Optionally log this.
        return;
    }
    mRequestQueue.push(std::move(request_data)); // Push request to internal queue
}

void RestAgent::worker_loop() {
    while (true) {
        std::optional<std::string> request_data_opt;

        // Wait for a request from the queue or a stop signal
        request_data_opt = mRequestQueue.wait_and_pop_for(std::chrono::milliseconds(100));

        if (mStopRequested.load(std::memory_order_acquire)) {
            // If stop is requested, check if the queue is empty. If so, exit.
            // If an item was just popped, process it one last time before exiting.
            if (!request_data_opt.has_value()) {
                 // Attempt a non-blocking pop to clear any final item if stop was just set
                std::optional<std::string> final_item = mRequestQueue.wait_and_pop_for(std::chrono::milliseconds(0));
                if (final_item.has_value()) {
                    request_data_opt = std::move(final_item);
                } else {
                    break; // Stop requested and queue is confirmed empty
                }
            }
        }

        if (request_data_opt.has_value()) {
            std::string request_data = std::move(request_data_opt.value());

            // Simulate network latency or processing time
            // std::cout << "[RestAgent WORKER] Processing: " << request_data << " on thread: " << std::this_thread::get_id() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::string response = "[RestAgent-Response] Processed: " + request_data;

            // Get a local copy of the callback under lock
            std::function<void(std::string result)> cb_to_call;
            {
                std::lock_guard<std::mutex> lock(mCallbackMutex);
                cb_to_call = mRegisteredCallback;
            }

            if (cb_to_call) {
                // std::cout << "[RestAgent WORKER] Invoking callback for: " << request_data << std::endl;
                cb_to_call(response);
            }
        }
        // If request_data_opt has no value here, it means wait_and_pop_for timed out and mStopRequested might not be true yet.
        // The loop continues and will check mStopRequested again or wait for new data.
    }
    // std::cout << "[RestAgent WORKER] Exiting worker_loop." << std::endl;
}

} // namespace rayconnect