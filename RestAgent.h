#ifndef REST_AGENT_H
#define REST_AGENT_H

#include "SpscQueue.h" // For SpscQueue member
#include <string>
#include <functional>
#include <thread>     // For std::thread member
#include <atomic>     // For std::atomic<bool> member
#include <mutex>      // For mCallbackMutex

namespace rayconnect {

class RestAgent {
public:
    RestAgent();
    ~RestAgent();

    // Simulates sending a request asynchronously and invoking a callback with the result.
    // RequestType is std::string, ResponseType is std::string.
    void send_request(std::string request_data);

    void register_callback(std::function<void(std::string result)> callback);

    // RestAgent is non-copyable and non-movable for simplicity in this example,
    // as it might manage internal resources (like a thread pool in a real scenario).
    RestAgent(const RestAgent&) = delete;
    RestAgent& operator=(const RestAgent&) = delete;
    RestAgent(RestAgent&&) = delete;
    RestAgent& operator=(RestAgent&&) = delete;

private:
    void worker_loop();

    SpscQueue<std::string> mRequestQueue;
    std::function<void(std::string result)> mRegisteredCallback;
    std::mutex mCallbackMutex; // To protect mRegisteredCallback during registration/updates, if needed

    std::thread mWorkerThread;
    std::atomic<bool> mStopRequested;
};

} // namespace rayconnect

#endif // REST_AGENT_H