#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include "McfAgent.h"

void simple_data_processor(const rayconnect::McfAgent::DataType& data) {
    std::cout << "[Processor] Received FINAL data (from RestAgent via McfAgent): " << data << " on thread: " << std::this_thread::get_id() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

int main() {
    std::cout << "Main thread ID: " << std::this_thread::get_id() << std::endl;

    rayconnect::McfAgent agent(simple_data_processor);

    std::cout << "McfAgent created and worker thread started." << std::endl;

    std::vector<std::string> messages_to_send = {
        "Msg1-Original",
        "Msg2-Original",
        "Msg3-Longer Original Data Packet",
        "Msg4-Test"
    };

    for (const auto& msg : messages_to_send) {
        std::cout << "[Main] Submitting initial data to McfAgent: " << msg << std::endl;
        agent.submit_data(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "[Main] All initial data submitted. McfAgent is processing and sending to RestAgent." << std::endl;
    std::cout << "[Main] Waiting for results to come back through RestAgent and be processed..." << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::cout << "[Main] Initiating McfAgent stop." << std::endl;

    std::cout << "[Main] Exiting. McfAgent destructor will now run and join its thread." << std::endl;
    return 0;
}
