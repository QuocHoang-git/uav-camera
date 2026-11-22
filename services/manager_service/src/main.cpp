#include "manager/app.hpp"
#include <iostream>
#include <string>
#include <csignal>

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    uav::manager::ManagerConfig config;

    /* Simple command line argument parsing */
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-s" && i + 1 < argc) {
            config.socket_path = argv[++i];
        } else if (arg == "-m" && i + 1 < argc) {
            config.storage_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "UAV Camera Manager Service (Rockchip RV1106)\n"
                      << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  -s <path>    UNIX domain socket path (default: /tmp/uav_eipc.sock)\n"
                      << "  -m <path>    Storage mount path (default: /mnt/sd)\n"
                      << "  -h, --help   Show this help message\n";
            return 0;
        }
    }

    std::cout << "Starting UAV Manager Service...\n";
    uav::manager::App app(config);

    if (!app.init()) {
        std::cerr << "[Main] Failed to initialize Manager Service\n";
        return 1;
    }

    return app.run();
}
