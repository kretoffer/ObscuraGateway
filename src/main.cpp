#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "obscuragateway/gateway.hpp"

static volatile bool running = true;

extern "C" void handle_signal(int) {
    running = false;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config.yml>" << std::endl;
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    try {
        auto config = obscuragateway::parse_config(argv[1]);
        obscuragateway::Gateway gateway(config);
        gateway.start();

        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        gateway.stop();
        gateway.wait();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
