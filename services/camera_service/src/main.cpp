#include "camera/camera_app.hpp"

#include <iostream>
#include <string>
#include <csignal>

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    uav::camera::CameraServiceConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-d" && i + 1 < argc) {
            config.device_path = argv[++i];
        } else if (arg == "-s" && i + 1 < argc) {
            config.socket_path = argv[++i];
        } else if (arg == "-w" && i + 1 < argc) {
            config.width = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "-h" && i + 1 < argc) {
            config.height = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "-f" && i + 1 < argc) {
            config.fps = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--auto-start") {
            config.auto_start = true;
        } else if (arg == "--help") {
            std::cout << "UAV Camera Service (Rockchip RV1106)\n"
                      << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  -d <dev>       V4L2 device node (default: /dev/video0)\n"
                      << "  -s <path>      Manager EIPC socket path (default: /tmp/uav_eipc.sock)\n"
                      << "  -w <width>     Capture width (default: 1920)\n"
                      << "  -h <height>    Capture height (default: 1080)\n"
                      << "  -f <fps>       Capture framerate (default: 30)\n"
                      << "  --auto-start   Start streaming on launch\n"
                      << "  --help         Show this help\n";
            return 0;
        }
    }

    std::cout << "Starting UAV Camera Service...\n";
    uav::camera::CameraApp app(config);

    if (!app.init()) {
        std::cerr << "[Main] Failed to initialize Camera Service\n";
        return 1;
    }

    return app.run();
}
