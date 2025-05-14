#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <functional>
#include <future>
#include <memory>
#include "McfAgent.h"

// --- Listener Class Definitions ---
class GeneralNewsListener : public rayconnect::McfAgent::ITopicListener,
                            public std::enable_shared_from_this<GeneralNewsListener> {
public:
    std::string name;
    GeneralNewsListener(std::string n) : name(std::move(n)) {}
    void on_topic_data(const std::string& topic_name, const rayconnect::McfAgent::DataType& data) override {
        std::cout << "[" << name << "@" << this << " on topic '" << topic_name << "'] Received on thread "
                  << std::this_thread::get_id() << ": " << data << std::endl;
    }
};

class SportsNewsListener : public rayconnect::McfAgent::ITopicListener,
                           public std::enable_shared_from_this<SportsNewsListener> {
public:
    std::string id;
    SportsNewsListener(std::string i) : id(std::move(i)) {}
    void on_topic_data(const std::string& topic_name, const rayconnect::McfAgent::DataType& data) override {
        std::cout << "[SportsListener-" << id << "@" << this << " on topic '" << topic_name << "'] Received on thread "
                  << std::this_thread::get_id() << ": " << data << std::endl;
    }
};

class WeatherAlertListener : public rayconnect::McfAgent::ITopicListener,
                             public std::enable_shared_from_this<WeatherAlertListener> {
public:
    void on_topic_data(const std::string& topic_name, const rayconnect::McfAgent::DataType& data) override {
        std::cout << "[WeatherAlertListener@" << this << " on topic '" << topic_name << "'] URGENT! Received on thread "
                  << std::this_thread::get_id() << ": " << data << std::endl;
    }
};

int main() {
    std::cout << "[Main] Test program for McfAgent ITopicListener API. Main thread: " << std::this_thread::get_id() << std::endl;

    rayconnect::McfAgent agent;
    std::cout << "[Main] McfAgent instance created." << std::endl;

    // --- Create Listener Instances (as shared_ptr) ---
    auto news_listener = std::make_shared<GeneralNewsListener>("MainNewsFeed");
    auto sports_listener_1 = std::make_shared<SportsNewsListener>("SL1");
    auto sports_listener_2 = std::make_shared<SportsNewsListener>("SL2");
    auto weather_listener = std::make_shared<WeatherAlertListener>();

    // --- Subscribe to topics using listener instances ---
    std::cout << "\n[Main] Subscribing to topics with listener objects..." << std::endl;
    // Pass the shared_ptr; it will be converted to weak_ptr by McfAgent::subscribe
    rayconnect::McfAgent::SubscriptionHandle news_handle = agent.subscribe("news", news_listener);
    rayconnect::McfAgent::SubscriptionHandle sports_handle_1 = agent.subscribe("news/sports", sports_listener_1);
    rayconnect::McfAgent::SubscriptionHandle sports_handle_2 = agent.subscribe("news/sports", sports_listener_2);
    rayconnect::McfAgent::SubscriptionHandle weather_handle = agent.subscribe("alerts/weather", weather_listener);

    if (news_handle != static_cast<rayconnect::McfAgent::SubscriptionHandle>(-1)) {
        std::cout << "[Main] Subscribed 'news_listener' to 'news' with handle: " << news_handle << std::endl;
    }
    if (sports_handle_1 != static_cast<rayconnect::McfAgent::SubscriptionHandle>(-1)) {
        std::cout << "[Main] Subscribed to 'news/sports' (1) with handle: " << sports_handle_1 << std::endl;
    }
     if (sports_handle_2 != static_cast<rayconnect::McfAgent::SubscriptionHandle>(-1)) {
        std::cout << "[Main] Subscribed to 'news/sports' (2) with handle: " << sports_handle_2 << std::endl;
    }
    if (weather_handle != static_cast<rayconnect::McfAgent::SubscriptionHandle>(-1)) {
        std::cout << "[Main] Subscribed to 'alerts/weather' with handle: " << weather_handle << std::endl;
    }

    std::vector<std::future<std::string>> rpc_futures;

    // --- Simulate RestAgent sending topic messages ---
    std::cout << "\n[Main] Simulating messages from RestAgent (via submit_data)..." << std::endl;
    rpc_futures.push_back(agent.submit_data("TOPIC:news:Global peace treaty signed"));
    rpc_futures.push_back(agent.submit_data("TOPIC:news/sports:Local team wins championship!"));
    rpc_futures.push_back(agent.submit_data("TOPIC:alerts/weather:Cyclone approaching coastal areas."));
    rpc_futures.push_back(agent.submit_data("RPC:Regular RPC call for user details."));
    rpc_futures.push_back(agent.submit_data("TOPIC:news/sports:Player X scores a hat-trick!"));
    rpc_futures.push_back(agent.submit_data("TOPIC:unknown_topic:This topic has no subscribers."));

    std::cout << "[Main] Messages submitted. Giving some time for async processing..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // --- Unsubscribe from one of the sports listeners ---
    std::cout << "\n[Main] Unsubscribing sports_listener_1 (handle " << sports_handle_1 << ") from 'news/sports'..." << std::endl;
    agent.unsubscribe(sports_handle_1);
    // sports_listener_1 itself is still alive here, but McfAgent should no longer call it for "news/sports"

    std::cout << "[Main] Simulating another sports message after unsubscription..." << std::endl;
    rpc_futures.push_back(agent.submit_data("TOPIC:news/sports:Post-unsubscription sports update.")); // sports_listener_2 should get this

    // --- Test listener lifecycle: news_listener goes out of scope ---
    // To demonstrate weak_ptr mechanism, let's make one listener go out of scope before a relevant message
    std::cout << "\n[Main] Releasing shared_ptr to news_listener. Next 'news' topic should not be delivered to it." << std::endl;
    auto news_listener_handle_for_unsubscribe_test = news_handle; // Save handle for potential later unsubscribe if needed
    news_listener.reset(); // Release the shared_ptr. McfAgent only holds a weak_ptr.

    rpc_futures.push_back(agent.submit_data("TOPIC:news:News after main news_listener was reset."));

    std::cout << "[Main] Giving some time for async processing after unsubscription and listener reset..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // --- Process RPC futures ---
    std::cout << "\n[Main] Checking RPC futures (results of submit_data calls)..." << std::endl;
    for (size_t i = 0; i < rpc_futures.size(); ++i) {
        try {
            std::string result = rpc_futures[i].get();
            std::cout << "[Main] Future " << i << " resolved: " << result << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[Main] Exception for future " << i << ": " << e.what() << std::endl;
        }
    }

    std::cout << "\n[Main] Initiating McfAgent stop..." << std::endl;

    std::cout << "[Main] Test program finished. McfAgent will be destroyed." << std::endl;
    return 0;
}