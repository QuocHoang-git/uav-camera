#include "streaming/streaming_app.hpp"

#include <iostream>
#include <string>
#include <csignal>

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    uav::streaming::StreamingServiceConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-s" && i + 1 < argc) {
            config.socket_path = argv[++i];
        } else if (arg == "-p" && i + 1 < argc) {
            config.rtsp_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "-m" && i + 1 < argc) {
            config.mount_point = argv[++i];
        } else if (arg == "-w" && i + 1 < argc) {
            config.width = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "-h" && i + 1 < argc) {
            config.height = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "-f" && i + 1 < argc) {
            config.fps = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "-c" && i + 1 < argc) {
            config.max_clients = static_cast<uint8_t>(std::stoi(argv[++i]));
        } else if (arg == "--no-auto-start") {
            config.auto_start = false;
        } else if (arg == "--help") {
            std::cout << "UAV RTSP/WebRTC Streaming Distribution Service\n"
                      << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  -s <path>        Manager EIPC socket path (default: /tmp/uav_eipc.sock)\n"
                      << "  -p <port>        RTSP listening port (default: 8554)\n"
                      << "  -m <path>        RTSP stream mount point (default: /live/main)\n"
                      << "  -w <width>       Frame width (default: 1920)\n"
                      << "  -h <height>      Frame height (default: 1080)\n"
                      << "  -f <fps>         Framerate (default: 30)\n"
                      << "  -c <count>       Max concurrent client sessions (default: 4)\n"
                      << "  --no-auto-start  Do not start RTSP listening socket on launch\n"
                      << "  --help           Show this help\n";
            return 0;
        }
    }

    std::cout << "Starting UAV Streaming Service on rtsp://0.0.0.0:" << config.rtsp_port
              << config.mount_point << "...\n";
    uav::streaming::StreamingApp app(config);

    if (!app.init()) {
        std::cerr << "[Main] Failed to initialize Streaming Service\n";
        return 1;
    }

    return app.run();
}
