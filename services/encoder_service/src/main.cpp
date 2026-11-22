#include "encoder/encoder_app.hpp"

#include <iostream>
#include <string>
#include <csignal>

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    uav::encoder::EncoderServiceConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-s" && i + 1 < argc) {
            config.socket_path = argv[++i];
        } else if (arg == "-w" && i + 1 < argc) {
            config.width = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "-h" && i + 1 < argc) {
            config.height = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "-f" && i + 1 < argc) {
            config.fps = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "-b" && i + 1 < argc) {
            config.bitrate_kbps = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "-g" && i + 1 < argc) {
            config.gop_size = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--codec" && i + 1 < argc) {
            std::string c = argv[++i];
            if (c == "h265" || c == "hevc") {
                config.codec = HAL_CODEC_H265;
            } else if (c == "jpeg" || c == "mjpeg") {
                config.codec = HAL_CODEC_JPEG;
            } else {
                config.codec = HAL_CODEC_H264;
            }
        } else if (arg == "--no-auto-start") {
            config.auto_start = false;
        } else if (arg == "--help") {
            std::cout << "UAV Hardware VPU Encoder Service (Rockchip RV1106)\n"
                      << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  -s <path>        Manager EIPC socket path (default: /tmp/uav_eipc.sock)\n"
                      << "  -w <width>       Frame width (default: 1920)\n"
                      << "  -h <height>      Frame height (default: 1080)\n"
                      << "  -f <fps>         Framerate (default: 30)\n"
                      << "  -b <kbps>        Bitrate in Kbps (default: 4000)\n"
                      << "  -g <gop>         GOP / I-frame interval (default: 30)\n"
                      << "  --codec <name>   Video codec: h264, h265, jpeg (default: h264)\n"
                      << "  --no-auto-start  Do not start encoder on launch\n"
                      << "  --help           Show this help\n";
            return 0;
        }
    }

    std::cout << "Starting UAV Hardware Encoder Service...\n";
    uav::encoder::EncoderApp app(config);

    if (!app.init()) {
        std::cerr << "[Main] Failed to initialize Encoder Service\n";
        return 1;
    }

    return app.run();
}
