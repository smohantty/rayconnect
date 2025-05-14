#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <future>
#include "McfAgent.h"

int main() {
    std::cout << "Main thread ID: " << std::this_thread::get_id() << std::endl;

    rayconnect::McfAgent agent;

    std::cout << "McfAgent created and worker thread started." << std::endl;

    std::vector<std::string> messages_to_send = {
        "Msg1-Original",
        "Msg2-Original",
        "Msg3-Longer Original Data Packet",
        "Msg4-Test"
    };

    std::vector<std::future<rayconnect::McfAgent::DataType>> futures;
    futures.reserve(messages_to_send.size());

    std::cout << "[Main] Submitting data to McfAgent and collecting futures." << std::endl;
    for (const auto& msg : messages_to_send) {
        std::cout << "[Main] Submitting data to McfAgent: " << msg << std::endl;
        futures.push_back(agent.submit_data(msg));
    }

    std::cout << "[Main] All data submitted. Waiting for and processing results..." << std::endl;

    for (size_t i = 0; i < futures.size(); ++i) {
        try {
            std::cout << "[Main] Waiting for result for message: " << messages_to_send[i] << std::endl;
            rayconnect::McfAgent::DataType result = futures[i].get();
            std::cout << "[Main] Received result for \"" << messages_to_send[i] << "\": " << result << " on thread: " << std::this_thread::get_id() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[Main] Exception while getting result for \"" << messages_to_send[i] << "\": " << e.what() << std::endl;
        }
    }

    std::cout << "[Main] Initiating McfAgent stop." << std::endl;

    std::cout << "[Main] Exiting. McfAgent destructor will now run and join its thread." << std::endl;
    return 0;
}
