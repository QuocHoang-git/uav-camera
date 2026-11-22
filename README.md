# UAV Camera

[![Platform: Custom Board](https://img.shields.io/badge/Platform-Custom%20UAV%20Board-blue.svg)](docs/schematics/Drone_STM+Linux_ver0.1_Schematic.pdf)
[![SoC: Rockchip RV1106](https://img.shields.io/badge/SoC-Rockchip%20RV1106%20%2F%20RV1103-green.svg)](https://www.rock-chips.com)
![Standard: C++17](https://img.shields.io/badge/Standard-C%2B%2B17%20POSIX-orange.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A microservice-based embedded Linux vision payload developed for the [**Custom UAV board**](docs/schematics/Drone_STM+Linux_ver0.1_Schematic.pdf) (Rockchip [RV1106](docs/datasheets/AR4236-AR4237-Luckfox-Pico-Pro-Max-Datasheet.pdf), ARM Cortex-A7 @ 1.2GHz, 128MB DDR2 RAM, 0.5 TOPS NPU).

---

## 🎥 Demo
<p align="center">
  <img src="docs/assets/demo.gif" alt="Dual-Stream H.264 & Real-Time Face Detection Demo" width="50%">
</p>

---

## 1. System Architecture

```mermaid
flowchart LR
    subgraph QGroundControl
        Mavlink[Mavlink]
        Video[Video]
    end
    
    subgraph UAV
        subgraph Linux
            Camera[Camera Service]
            Manager[Manager Service]
            Streaming[Streaming Service]
            MavlinkRouter[Mavlink Router]
            Telemetry[Telemetry Service]
        end

        subgraph MCU
            PX4[PX4]
        end
    end

    Mavlink <-->|Socket| MavlinkRouter
    Video <-->|RTSP| Streaming

    MavlinkRouter <-->|UART| PX4

    MavlinkRouter <-->|Socket| Telemetry
    Streaming <-->|UDS / DMA BUF| Camera
    Camera & Telemetry & Streaming <-->|UDS| Manager
```

---

## 2. Directory Layout

```text
uav-camera/
├── cmake/                      # Toolchain definitions for RV1106/RV1103 uClibc-ng
│   └── toolchain-arm-rockchip830-uclibcgnueabihf.cmake
├── docs/                       # Technical specifications, architecture docs & diagrams
│   ├── ARCHITECTURE.md         # Comprehensive systems architecture specification
├── libs/
│   ├── camera_service/         # Sensor acquisition & VPU encoding
│   ├── manager_service/        # Process supervision & state machine
│   ├── streaming_service/      # RTSP/WebRTC video distribution
│   └── telemetry_service/      # MAVLink bridge & vehicle metrics
├── scripts/                    # Automated scripts
├── services/                   # All services
├── CMakeLists.txt              # Top-level CMake build configuration
├── LICENSE
└── README.md
```

---

## 3. Build, Cross-Compilation & Deployment

### Prerequisites
- **Host OS**: Linux Ubuntu 22.04 LTS (x86_64)
- **Cross Toolchain**: `arm-rockchip830-linux-uclibcgnueabihf` (Rockchip RV1106 Linux SDK / Buildroot Toolchain)
- **CMake**: Version >= 3.16

### Quick Start

1. **Cross-compile for Luckfox Pico Pro:**

```bash
./scripts/build_cross_rockchip.sh
# Or for Debug build:
./scripts/build_cross_rockchip.sh debug
```

## 4. License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
