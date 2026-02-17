# home-assistant-adapter
Example firmware for the ESP32-C6-based Home Assistant adapter with Zigbee support.

## Hardware
This firmware is designed for ESP32-C6 development boards and provides MQTT-over-Zigbee communication for GE Appliances.

**Note**: This is an updated version that uses ESP32-C6 instead of ESP32-C3, and Zigbee instead of WiFi for network connectivity.

### Original Hardware
The original Home Assistant adapter consists of a [Xiao ESP32C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) and [carrier board](doc/schematic-v1.0.pdf) that breaks out the serial interface of the Xiao to an RJ45 jack.

### ESP32-C6 Version
This version uses ESP32-C6 which includes:
- Native Zigbee support (IEEE 802.15.4)
- Improved performance
- Better power efficiency
- Compatible pin mappings for GEA3 serial communication

## Architecture
This firmware implements MQTT-over-Zigbee architecture:
- ESP32-C6 acts as Zigbee End Device
- Communicates with Zigbee Coordinator
- Coordinator bridges messages to MQTT broker
- See [Zigbee Architecture Guide](doc/zigbee-architecture.md) for details

## Setup
- Install [PlatformIO](https://platformio.org/)
- Copy `config/Config.h.sample` to `config/Config.h` and configure your device ID and Zigbee settings
- Note: This version uses Zigbee instead of WiFi, so WiFi credentials are not needed

For detailed instructions, see the [Zigbee Architecture Guide](doc/zigbee-architecture.md).

## Usage
### Build
Builds the firmware into `.pio/build/xiao_c6/firmware.bin`.

```shell
make
```

### Clean
Deletes all build artifacts.

```shell
make clean
```

### Upload
Uploads/flashes the firmware to the ESP32-C6. Note that the board may need to be reset into boot loader mode.

```shell
make upload
```

### (Serial) Monitor
Opens the PlatformIO serial monitor to view a connected ESP32-C6's serial output.

```shell
make monitor
```

## Requirements

### For Full Zigbee Functionality
This firmware provides a framework for MQTT-over-Zigbee. To use it, you need:

1. **Zigbee Coordinator**: A separate device that:
   - Manages the Zigbee network
   - Bridges between Zigbee and MQTT protocols
   - Connects to your MQTT broker

2. **MQTT Broker**: Standard MQTT broker (like Mosquitto in Home Assistant)

See [Zigbee Architecture Guide](doc/zigbee-architecture.md) for implementation details.

## Current Status

✅ **Implemented**:
- ESP32-C6 hardware support
- Pin mappings for ESP32-C6
- GEA3 serial communication
- MQTT client framework
- Zigbee client stub/framework

⚠️ **In Progress**:
- Full ESP-IDF Zigbee stack integration
- Zigbee network joining and communication
- Coordinator gateway implementation

## Pin Mapping
- GPIO 6: GEA3 RX (Serial1)
- GPIO 7: GEA3 TX (Serial1)
- GPIO 15: Heartbeat LED
- GPIO 16: MQTT Status LED
- GPIO 17: Zigbee Status LED

## Example Home Assistant Configuration
Sample yaml can be found in https://github.com/geappliances/home-assistant-examples

Note: When using Zigbee, messages will be routed through the Zigbee coordinator to the MQTT broker.

## Migration from ESP32-C3 WiFi Version
If you're migrating from the ESP32-C3 WiFi version:
- Hardware change: Use ESP32-C6 board instead of C3
- Network: Set up Zigbee network instead of WiFi
- Configuration: Update Config.h for Zigbee parameters
- Add coordinator: Need Zigbee coordinator device
- See [Zigbee Architecture Guide](doc/zigbee-architecture.md) for migration details
