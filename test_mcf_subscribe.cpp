#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <functional>
#include <future>
#include "McfAgent.h"

// --- Topic Handler Callbacks ---
void general_news_handler(const std::string& data) {
    std::cout << "[GeneralNewsHandler] Received on thread " << std::this_thread::get_id() << ": " << data << std::endl;
}

void sports_news_handler_1(const std::string& data) {
    std::cout << "[SportsNewsHandler_1] Received on thread " << std::this_thread::get_id() << ": " << data << std::endl;
}

void sports_news_handler_2(const std::string& data) {
    std::cout << "[SportsNewsHandler_2] Received on thread " << std::this_thread::get_id() << ": " << data << std::endl;
}

void weather_alert_handler(const std::string& data) {
    std::cout << "[WeatherAlertHandler] URGENT! Received on thread " << std::this_thread::get_id() << ": " << data << std::endl;
}

int main() {
    std::cout << "[Main] Test program for McfAgent subscription API. Main thread: " << std::this_thread::get_id() << std::endl;

    rayconnect::McfAgent agent;
    std::cout << "[Main] McfAgent instance created." << std::endl;

    // --- Subscribe to topics ---
    std::cout << "\n[Main] Subscribing to topics..." << std::endl;
    rayconnect::McfAgent::SubscriptionHandle news_handle = agent.subscribe("news", general_news_handler);
    rayconnect::McfAgent::SubscriptionHandle sports_handle_1 = agent.subscribe("news/sports", sports_news_handler_1);
    rayconnect::McfAgent::SubscriptionHandle sports_handle_2 = agent.subscribe("news/sports", sports_news_handler_2); // Second subscriber for the same topic
    rayconnect::McfAgent::SubscriptionHandle weather_handle = agent.subscribe("alerts/weather", weather_alert_handler);

    if (news_handle != static_cast<rayconnect::McfAgent::SubscriptionHandle>(-1)) {
        std::cout << "[Main] Subscribed to 'news' with handle: " << news_handle << std::endl;
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

    // --- Simulate RestAgent sending topic messages (and also some RPCs) ---
    // McfAgent will parse these if RestAgent echoes them.
    // The future returned will also contain this string.
    std::cout << "\n[Main] Simulating messages from RestAgent (via submit_data)..." << std::endl;
    rpc_futures.push_back(agent.submit_data("TOPIC:news:Global শান্তি চুক্তি স্বাক্ষরিত")); // Global peace treaty signed
    rpc_futures.push_back(agent.submit_data("TOPIC:news/sports:Local team wins championship!"));
    rpc_futures.push_back(agent.submit_data("TOPIC:alerts/weather:Cyclone approaching coastal areas."));
    rpc_futures.push_back(agent.submit_data("RPC:Regular RPC call for user details.")); // This should not trigger topic handlers
    rpc_futures.push_back(agent.submit_data("TOPIC:news/sports:Player X scores a hat-trick!"));
    rpc_futures.push_back(agent.submit_data("TOPIC:unknown_topic:This topic has no subscribers."));

    std::cout << "[Main] Messages submitted. Giving some time for async processing..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2)); // Allow time for topics to be processed

    // --- Unsubscribe from one of the sports handlers ---
    std::cout << "\n[Main] Unsubscribing sports_handle_1 (" << sports_handle_1 << ") from 'news/sports'..." << std::endl;
    agent.unsubscribe(sports_handle_1);

    std::cout << "[Main] Simulating another sports message after unsubscription..." << std::endl;
    rpc_futures.push_back(agent.submit_data("TOPIC:news/sports:Post-unsubscription sports update."));

    std::cout << "[Main] Giving some time for async processing after unsubscription..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));


    // --- Process RPC futures (which also contain the topic strings if they were echoed) ---
    std::cout << "\n[Main] Checking RPC futures (results of submit_data calls)..." << std::endl;
    for (size_t i = 0; i < rpc_futures.size(); ++i) {
        try {
            std::string result = rpc_futures[i].get(); // Blocks until the future is ready
            std::cout << "[Main] Future " << i << " resolved: " << result << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[Main] Exception for future " << i << ": " << e.what() << std::endl;
        }
    }

    std::cout << "\n[Main] Initiating McfAgent stop..." << std::endl;
    // agent.stop(); // Optional: Explicit stop. Destructor will also call stop.

    std::cout << "[Main] Test program finished. McfAgent will be destroyed." << std::endl;
    return 0;
}