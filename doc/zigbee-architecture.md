# Zigbee Architecture for ESP32-C6

## Overview
This document describes the architecture for MQTT-over-Zigbee communication with the ESP32-C6 adapter.

## Architecture Components

### 1. ESP32-C6 End Device (This Firmware)
- Acts as Zigbee End Device
- Reads data from GE Appliances via GEA3 protocol (serial)
- Encapsulates MQTT messages in Zigbee packets
- Sends data to Zigbee Coordinator
- Receives commands from coordinator

### 2. Zigbee Coordinator (Required, Not Included)
- Manages the Zigbee network
- Acts as gateway between Zigbee and MQTT broker
- Receives Zigbee packets from ESP32-C6
- Forwards messages to MQTT broker
- Receives MQTT messages and forwards to Zigbee devices

### 3. MQTT Broker (Home Assistant)
- Standard MQTT broker (Mosquitto)
- Receives messages from Zigbee coordinator
- Publishes commands that are forwarded through coordinator

## Message Flow

### Publishing Data (Appliance → Home Assistant)
```
GE Appliance → [Serial/GEA3] → ESP32-C6 → [Zigbee] → Coordinator → [TCP/IP] → MQTT Broker → Home Assistant
```

### Receiving Commands (Home Assistant → Appliance)
```
Home Assistant → MQTT Broker → [TCP/IP] → Coordinator → [Zigbee] → ESP32-C6 → [Serial/GEA3] → GE Appliance
```

## Current Implementation Status

### ✅ Completed
- ESP32-C6 hardware configuration
- Pin mapping for ESP32-C6
- ZigbeeClient stub implementation
- MQTT client integration (using PubSubClient)
- GEA3 bridge integration

### ⚠️ Stub Implementation
- Zigbee network joining (simulated)
- Zigbee message transmission (logged but not sent)
- Zigbee message reception (not implemented)

### 🔨 Requires Full Implementation
To make this fully functional, you need to:

1. **Integrate ESP-IDF Zigbee Stack**
   - Use ESP-IDF framework (not just Arduino)
   - Add ESP-Zigbee library
   - Implement proper Zigbee device initialization
   - Handle network steering and commissioning

2. **Implement ZigbeeClient with Real Zigbee**
   - Replace stub `sendZigbeeMessage()` with `esp_zigbee_zcl_custom_cluster_cmd_req()`
   - Implement message reception handler
   - Add Zigbee event callbacks
   - Handle network state changes

3. **Create Zigbee Coordinator Gateway**
   - Use another ESP32-C6 or ESP32-H2 as coordinator
   - Implement Zigbee network management
   - Create MQTT client on coordinator
   - Bridge between Zigbee and MQTT protocols

## Zigbee Coordinator Options

### Option 1: ESP32-H2 or ESP32-C6 Coordinator
Create a custom coordinator using ESP-IDF:
- Manages Zigbee network
- Acts as MQTT client
- Bridges messages between protocols

### Option 2: Zigbee2MQTT
If using standard Zigbee devices:
- Use Zigbee2MQTT with compatible coordinator
- Define custom device handlers
- May require protocol adaptation

### Option 3: Commercial Gateway
Some commercial Zigbee gateways support:
- Custom Zigbee devices
- MQTT bridging
- Custom message formats

## Development Steps

### Phase 1: Hardware Validation
1. Verify ESP32-C6 board works with Arduino framework
2. Test serial communication with GE appliance
3. Verify LED indicators function

### Phase 2: Zigbee Network Setup
1. Set up Zigbee coordinator
2. Implement network joining on ESP32-C6
3. Test basic Zigbee communication

### Phase 3: MQTT-Zigbee Bridge
1. Implement custom Zigbee clusters for MQTT
2. Create message encoding/decoding protocol
3. Implement coordinator bridge logic

### Phase 4: Integration Testing
1. Test end-to-end message flow
2. Verify GEA3 data reaches Home Assistant
3. Test command reception from Home Assistant

## Configuration

### ESP32-C6 Device
Edit `config/Config.h`:
```cpp
const char* ZIGBEE_MANUFACTURER = "GE Appliances";
const char* ZIGBEE_MODEL = "GEA3-MQTT-Bridge";
const uint8_t ZIGBEE_ENDPOINT = 1;
const char* deviceId = "your_device_id";
```

### Zigbee Coordinator
Configure coordinator with:
- MQTT broker address
- Zigbee network settings
- Device ID mapping

## Pin Mapping

### ESP32-C6 DevKit-C1
- GPIO 6: GEA3 RX (Serial1)
- GPIO 7: GEA3 TX (Serial1)
- GPIO 15: Heartbeat LED
- GPIO 16: MQTT Status LED
- GPIO 17: Zigbee Status LED

### LED Indicators
- **Heartbeat**: Blinks continuously when running
- **MQTT**: On when MQTT connected
- **Zigbee**: Blinks while joining, solid when connected

## References

- [ESP-IDF Zigbee Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/zigbee/index.html)
- [ESP-Zigbee SDK Examples](https://github.com/espressif/esp-zigbee-sdk)
- [Zigbee Specification](https://zigbeealliance.org/developer_resources/zigbee-specification/)
- [MQTT Protocol](https://mqtt.org/)

## Troubleshooting

### Serial Communication Issues
- Check GPIO pin assignments
- Verify baud rate (115200 for console, GEA3 baud for Serial1)
- Ensure proper GND connection

### Zigbee Connection Issues
- Verify coordinator is running and accessible
- Check Zigbee channel configuration
- Ensure device is in commissioning mode

### MQTT Issues
- Verify coordinator can reach MQTT broker
- Check MQTT credentials
- Review message encoding/decoding

## Support

For questions and support:
1. Check ESP-IDF Zigbee examples
2. Review GEA3 protocol documentation
3. Consult ESP32-C6 datasheet
4. Open GitHub issues for specific problems
